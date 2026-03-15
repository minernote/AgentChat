/**
 * @file src/core/network/mdns.cpp
 * @brief mDNS/DNS-SD zero-config LAN discovery for AgentChat.
 *
 * Implements RFC 6762 (mDNS) + RFC 6763 (DNS-SD) subset:
 *   - PTR query/response for _agentchat._tcp.local.
 *   - SRV + A records in additional section
 *   - Periodic unsolicited announcements (every 60s)
 *   - Goodbye packets on stop()
 */

#include <agentchat/mdns.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace agentchat::mdns {

// ── DNS wire format helpers ──────────────────────────────────────────────────

namespace dns {

static void push_u16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v >> 8));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

static void push_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
    buf.push_back(static_cast<uint8_t>( v        & 0xFF));
}

// Encode a DNS name label sequence from a dot-separated string.
// e.g. "_agentchat._tcp.local." → \x0a_agentchat\x04_tcp\x05local\x00
static void push_name(std::vector<uint8_t>& buf, const std::string& name) {
    size_t start = 0;
    std::string n = name;
    // strip trailing dot
    if (!n.empty() && n.back() == '.') n.pop_back();
    while (start < n.size()) {
        size_t dot = n.find('.', start);
        if (dot == std::string::npos) dot = n.size();
        size_t len = dot - start;
        buf.push_back(static_cast<uint8_t>(len));
        for (size_t i = start; i < dot; ++i)
            buf.push_back(static_cast<uint8_t>(n[i]));
        start = dot + 1;
    }
    buf.push_back(0); // root
}

constexpr uint16_t TYPE_PTR  = 12;
constexpr uint16_t TYPE_SRV  = 33;
constexpr uint16_t TYPE_A    = 1;
constexpr uint16_t CLASS_IN  = 1;
constexpr uint16_t CLASS_IN_FLUSH = CLASS_IN | 0x8000; // cache-flush bit
constexpr uint32_t TTL_DEFAULT  = 4500; // 75 minutes
constexpr uint32_t TTL_GOODBYE  = 0;

// Build a minimal mDNS response packet with PTR + SRV + A records.
std::vector<uint8_t> build_announcement(
    const std::string& instance_name,
    uint16_t           port,
    uint32_t           ipv4,
    uint32_t           ttl = TTL_DEFAULT)
{
    // Full names
    // instance: "<name>._agentchat._tcp.local."
    // service:  "_agentchat._tcp.local."
    // host:     "<name>.local."
    std::string svc  = "_agentchat._tcp.local.";
    std::string inst = instance_name + "." + svc;
    std::string host = instance_name + ".local.";

    std::vector<uint8_t> pkt;

    // DNS header: ID=0, QR=1(response), AA=1(authoritative), QDCOUNT=0,
    //             ANCOUNT=1(PTR), NSCOUNT=0, ARCOUNT=2(SRV+A)
    push_u16(pkt, 0x0000); // ID
    push_u16(pkt, 0x8400); // Flags: QR=1, AA=1
    push_u16(pkt, 0);      // QDCOUNT
    push_u16(pkt, 1);      // ANCOUNT (PTR)
    push_u16(pkt, 0);      // NSCOUNT
    push_u16(pkt, 2);      // ARCOUNT (SRV + A)

    // ── Answer: PTR _agentchat._tcp.local. → inst ────────────────────────────
    push_name(pkt, svc);
    push_u16(pkt, TYPE_PTR);
    push_u16(pkt, CLASS_IN);
    push_u32(pkt, ttl);
    // RDATA: name (length-prefixed by 2 bytes)
    std::vector<uint8_t> rdata_ptr;
    push_name(rdata_ptr, inst);
    push_u16(pkt, static_cast<uint16_t>(rdata_ptr.size()));
    pkt.insert(pkt.end(), rdata_ptr.begin(), rdata_ptr.end());

    // ── Additional: SRV inst → host:port ─────────────────────────────────────
    push_name(pkt, inst);
    push_u16(pkt, TYPE_SRV);
    push_u16(pkt, CLASS_IN_FLUSH);
    push_u32(pkt, ttl);
    std::vector<uint8_t> rdata_srv;
    push_u16(rdata_srv, 0);    // priority
    push_u16(rdata_srv, 0);    // weight
    push_u16(rdata_srv, port); // port
    push_name(rdata_srv, host);
    push_u16(pkt, static_cast<uint16_t>(rdata_srv.size()));
    pkt.insert(pkt.end(), rdata_srv.begin(), rdata_srv.end());

    // ── Additional: A host → ipv4 ────────────────────────────────────────────
    push_name(pkt, host);
    push_u16(pkt, TYPE_A);
    push_u16(pkt, CLASS_IN_FLUSH);
    push_u32(pkt, ttl);
    push_u16(pkt, 4); // RDLENGTH
    push_u32(pkt, ipv4);

    return pkt;
}

// Build a PTR query for _agentchat._tcp.local.
std::vector<uint8_t> build_query() {
    std::vector<uint8_t> pkt;
    push_u16(pkt, 0x0000); // ID
    push_u16(pkt, 0x0000); // Flags: standard query
    push_u16(pkt, 1);      // QDCOUNT
    push_u16(pkt, 0);      // ANCOUNT
    push_u16(pkt, 0);      // NSCOUNT
    push_u16(pkt, 0);      // ARCOUNT
    push_name(pkt, "_agentchat._tcp.local.");
    push_u16(pkt, TYPE_PTR);
    push_u16(pkt, CLASS_IN);
    return pkt;
}

// Read a DNS name from buf at *off, following one level of pointer compression.
std::string read_name(const uint8_t* buf, size_t len, size_t& off) {
    std::string result;
    bool jumped = false;
    size_t orig_off = off;
    int safety = 64;
    while (off < len && safety-- > 0) {
        uint8_t c = buf[off];
        if (c == 0) { ++off; break; }
        if ((c & 0xC0) == 0xC0) {
            // pointer
            if (off + 1 >= len) break;
            size_t ptr = ((c & 0x3F) << 8) | buf[off + 1];
            if (!jumped) orig_off = off + 2;
            off = ptr;
            jumped = true;
            continue;
        }
        ++off;
        if (off + c > len) break;
        if (!result.empty()) result += '.';
        result.append(reinterpret_cast<const char*>(buf + off), c);
        off += c;
    }
    if (jumped) off = orig_off;
    return result;
}

struct DnsRecord {
    std::string  name;
    uint16_t     type{0};
    uint16_t     rclass{0};
    uint32_t     ttl{0};
    size_t       rdata_off{0};
    uint16_t     rdlen{0};
};

struct DnsMsg {
    uint16_t flags{0};
    std::vector<DnsRecord> answers;
    std::vector<DnsRecord> additional;
};

bool parse(const uint8_t* buf, size_t len, DnsMsg& out) {
    if (len < 12) return false;
    size_t off = 2; // skip ID
    out.flags = static_cast<uint16_t>((buf[off] << 8) | buf[off+1]); off += 2;
    uint16_t qdcount = static_cast<uint16_t>((buf[off]<<8)|buf[off+1]); off+=2;
    uint16_t ancount = static_cast<uint16_t>((buf[off]<<8)|buf[off+1]); off+=2;
    uint16_t nscount = static_cast<uint16_t>((buf[off]<<8)|buf[off+1]); off+=2;
    uint16_t arcount = static_cast<uint16_t>((buf[off]<<8)|buf[off+1]); off+=2;

    // skip questions
    for (int i = 0; i < qdcount && off < len; ++i) {
        read_name(buf, len, off);
        off += 4; // qtype + qclass
    }

    auto read_rr = [&](DnsRecord& rr) -> bool {
        rr.name = read_name(buf, len, off);
        if (off + 10 > len) return false;
        rr.type   = static_cast<uint16_t>((buf[off]<<8)|buf[off+1]); off+=2;
        rr.rclass = static_cast<uint16_t>((buf[off]<<8)|buf[off+1]); off+=2;
        rr.ttl    = (static_cast<uint32_t>(buf[off])<<24)
                  | (static_cast<uint32_t>(buf[off+1])<<16)
                  | (static_cast<uint32_t>(buf[off+2])<<8)
                  |  static_cast<uint32_t>(buf[off+3]); off+=4;
        rr.rdlen  = static_cast<uint16_t>((buf[off]<<8)|buf[off+1]); off+=2;
        rr.rdata_off = off;
        off += rr.rdlen;
        return off <= len;
    };

    for (int i = 0; i < ancount && off < len; ++i) {
        DnsRecord rr;
        if (!read_rr(rr)) break;
        out.answers.push_back(rr);
    }
    for (int i = 0; i < nscount && off < len; ++i) {
        DnsRecord rr; read_rr(rr); // skip
    }
    for (int i = 0; i < arcount && off < len; ++i) {
        DnsRecord rr;
        if (!read_rr(rr)) break;
        out.additional.push_back(rr);
    }
    return true;
}

} // namespace dns

// ── Utility: get primary non-loopback IPv4 address ──────────────────────────

static uint32_t get_local_ipv4() {
    struct ifaddrs* ifa = nullptr;
    if (::getifaddrs(&ifa) != 0) return htonl(INADDR_LOOPBACK);
    uint32_t result = htonl(INADDR_LOOPBACK);
    for (auto* p = ifa; p; p = p->ifa_next) {
        if (!p->ifa_addr) continue;
        if (p->ifa_addr->sa_family != AF_INET) continue;
        if (p->ifa_flags & IFF_LOOPBACK) continue;
        if (!(p->ifa_flags & IFF_UP)) continue;
        result = reinterpret_cast<struct sockaddr_in*>(p->ifa_addr)->sin_addr.s_addr;
        break;
    }
    ::freeifaddrs(ifa);
    return result; // already in network byte order
}

// ── Shared socket setup ──────────────────────────────────────────────────────

static int open_mdns_socket() {
    int sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return -1;

    int yes = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(MDNS_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sock);
        return -1;
    }

    // Join multicast group
    struct ip_mreq mreq{};
    ::inet_pton(AF_INET, MDNS_ADDR, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = INADDR_ANY;
    ::setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    // Set multicast TTL and loopback (so same host can discover itself)
    uint8_t ttl = 255;
    ::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    uint8_t loop = 1;
    ::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    return sock;
}

static void send_mdns(int sock, const std::vector<uint8_t>& pkt) {
    struct sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(MDNS_PORT);
    ::inet_pton(AF_INET, MDNS_ADDR, &dst.sin_addr);
    ::sendto(sock, pkt.data(), pkt.size(), 0,
             reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst));
}

// ── Advertiser ───────────────────────────────────────────────────────────────

Advertiser::Advertiser(std::string instance_name, uint16_t port)
    : instance_name_(std::move(instance_name))
    , port_(port)
{}

Advertiser::~Advertiser() { stop(); }

void Advertiser::start() {
    if (running_.exchange(true)) return;
    sock_ = open_mdns_socket();
    if (sock_ < 0) {
        running_ = false;
        std::cerr << "[mdns] Failed to open mDNS socket\n";
        return;
    }
    thread_ = std::thread(&Advertiser::run, this);
    std::cout << "[mdns] Advertising '" << instance_name_
              << "' on port " << port_ << "\n";
}

void Advertiser::stop() {
    if (!running_.exchange(false)) return;
    // Send goodbye (TTL=0)
    if (sock_ >= 0) {
        uint32_t ipv4 = get_local_ipv4();
        auto bye = dns::build_announcement(instance_name_, port_,
                                           ntohl(ipv4), dns::TTL_GOODBYE);
        send_mdns(sock_, bye);
        ::close(sock_);
        sock_ = -1;
    }
    if (thread_.joinable()) thread_.join();
    std::cout << "[mdns] Advertiser stopped\n";
}

void Advertiser::run() {
    uint32_t ipv4 = get_local_ipv4();

    // Announce immediately on start
    auto pkt = dns::build_announcement(instance_name_, port_, ntohl(ipv4));
    send_mdns(sock_, pkt);

    // Also listen for PTR queries and respond
    // Set socket to non-blocking with timeout for select
    struct timeval tv{};
    tv.tv_sec  = 1;
    tv.tv_usec = 0;
    ::setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    auto last_announce = std::chrono::steady_clock::now();
    constexpr int ANNOUNCE_INTERVAL_S = 60;

    uint8_t buf[4096];
    while (running_) {
        // Re-announce periodically
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(
                now - last_announce).count() >= ANNOUNCE_INTERVAL_S) {
            send_mdns(sock_, pkt);
            last_announce = now;
        }

        ssize_t n = ::recv(sock_, buf, sizeof(buf), 0);
        if (n <= 0) continue; // timeout or error

        dns::DnsMsg msg;
        if (!dns::parse(buf, static_cast<size_t>(n), msg)) continue;

        // If this is a query (QR=0) containing our PTR type, respond
        if ((msg.flags & 0x8000) == 0) {
            // It's a question — respond with our announcement
            send_mdns(sock_, pkt);
        }
    }
}

// ── Browser ──────────────────────────────────────────────────────────────────

Browser::Browser(Callback on_found)
    : on_found_(std::move(on_found))
{}

Browser::~Browser() { stop(); }

void Browser::start() {
    if (running_.exchange(true)) return;
    sock_ = open_mdns_socket();
    if (sock_ < 0) {
        running_ = false;
        std::cerr << "[mdns] Browser: failed to open socket\n";
        return;
    }
    thread_ = std::thread(&Browser::run, this);
    std::cout << "[mdns] Browser started\n";
}

void Browser::stop() {
    if (!running_.exchange(false)) return;
    if (sock_ >= 0) { ::close(sock_); sock_ = -1; }
    if (thread_.joinable()) thread_.join();
    std::cout << "[mdns] Browser stopped\n";
}

std::vector<ServiceInfo> Browser::peers() const {
    std::lock_guard<std::mutex> lk(peers_mu_);
    return peers_;
}

void Browser::send_query() {
    auto pkt = dns::build_query();
    send_mdns(sock_, pkt);
}

void Browser::handle_packet(const uint8_t* data, size_t len) {
    dns::DnsMsg msg;
    if (!dns::parse(data, len, msg)) return;
    if ((msg.flags & 0x8000) == 0) return; // ignore queries

    // Look for PTR answers pointing to our service
    for (const auto& rr : msg.answers) {
        if (rr.type != dns::TYPE_PTR) continue;

        // Decode PTR rdata to get instance name
        size_t roff = rr.rdata_off;
        std::string inst = dns::read_name(data, len, roff);
        if (inst.empty()) continue;

        // Prefix before "._agentchat._tcp.local"
        std::string svc_suffix = "._agentchat._tcp.local";
        std::string name = inst;
        auto spos = inst.find(svc_suffix);
        if (spos != std::string::npos) name = inst.substr(0, spos);

        // Find matching SRV + A in additional records
        uint16_t port = 0;
        std::string host;
        uint32_t ipv4 = 0;

        for (const auto& ar : msg.additional) {
            if (ar.type == dns::TYPE_SRV && ar.name == inst) {
                size_t off = ar.rdata_off + 4; // skip priority+weight
                if (off + 2 > len) continue;
                port = static_cast<uint16_t>((data[off] << 8) | data[off+1]);
                off += 2;
                host = dns::read_name(data, len, off);
            }
            if (ar.type == dns::TYPE_A && !host.empty() && ar.name == host) {
                if (ar.rdata_off + 4 <= len) {
                    uint32_t raw =
                        (static_cast<uint32_t>(data[ar.rdata_off])   << 24)
                      | (static_cast<uint32_t>(data[ar.rdata_off+1]) << 16)
                      | (static_cast<uint32_t>(data[ar.rdata_off+2]) <<  8)
                      |  static_cast<uint32_t>(data[ar.rdata_off+3]);
                    ipv4 = raw;
                }
            }
        }

        if (port == 0 || ipv4 == 0) continue;

        // Format IP
        char ip_str[INET_ADDRSTRLEN];
        uint32_t ipv4_net = htonl(ipv4);
        ::inet_ntop(AF_INET, &ipv4_net, ip_str, sizeof(ip_str));

        ServiceInfo si;
        si.name    = name;
        si.host    = host;
        si.address = ip_str;
        si.port    = port;

        // Check if already known
        {
            std::lock_guard<std::mutex> lk(peers_mu_);
            bool found = false;
            for (auto& p : peers_) {
                if (p.address == si.address && p.port == si.port) {
                    found = true; break;
                }
            }
            if (!found) {
                peers_.push_back(si);
            } else {
                continue; // already known, skip callback
            }
        }

        if (on_found_) on_found_(si);
        std::cout << "[mdns] Discovered: " << si.name
                  << " @ " << si.address << ":" << si.port << "\n";
    }
}

void Browser::run() {
    struct timeval tv{};
    tv.tv_sec  = 1;
    tv.tv_usec = 0;
    ::setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Send initial query
    send_query();
    auto last_query = std::chrono::steady_clock::now();
    constexpr int QUERY_INTERVAL_S = 30;

    uint8_t buf[4096];
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(
                now - last_query).count() >= QUERY_INTERVAL_S) {
            send_query();
            last_query = now;
        }

        ssize_t n = ::recv(sock_, buf, sizeof(buf), 0);
        if (n <= 0) continue;
        handle_packet(buf, static_cast<size_t>(n));
    }
}

} // namespace agentchat::mdns