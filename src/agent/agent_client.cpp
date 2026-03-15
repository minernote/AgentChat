#include "agent_client.h"

#include <agentchat/crypto.h>
#include <agentchat/protocol.h>
#include "../core/crypto/ratchet.h"
#include "../core/crypto/x3dh.h"

using namespace agentchat::crypto;

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <unordered_map>

namespace agentchat {

AgentClient::AgentClient(
    std::string     host,
    uint16_t        port,
    AgentId         agent_id,
    crypto::KeyPair identity_keypair,
    crypto::KeyPair exchange_keypair)
    : host_(std::move(host))
    , port_(port)
    , agent_id_(agent_id)
    , identity_keypair_(std::move(identity_keypair))
    , exchange_keypair_(std::move(exchange_keypair))
{}

AgentClient::~AgentClient() { disconnect(); }

bool AgentClient::connect() {
    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    std::string port_str = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &res) != 0) {
        if (on_error_cb_) on_error_cb_("DNS resolution failed: " + host_);
        return false;
    }
    sockfd_ = -1;
    for (auto* p = res; p; p = p->ai_next) {
        int fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) { sockfd_ = fd; break; }
        ::close(fd);
    }
    freeaddrinfo(res);
    if (sockfd_ < 0) {
        if (on_error_cb_) on_error_cb_("Connection refused: " + host_ + ":" + port_str);
        return false;
    }
    if (!perform_handshake()) { ::close(sockfd_); sockfd_ = -1; return false; }
    connected_ = true;
    running_   = true;
    recv_thread_ = std::thread(&AgentClient::recv_thread_fn, this);
    if (on_connect_cb_) on_connect_cb_();
    return true;
}

void AgentClient::disconnect() {
    running_   = false;
    connected_ = false;
    if (sockfd_ >= 0) { ::shutdown(sockfd_, SHUT_RDWR); ::close(sockfd_); sockfd_ = -1; }
    if (recv_thread_.joinable()) recv_thread_.join();
}

bool AgentClient::is_connected() const { return connected_; }

// Read one complete frame from sockfd_ (blocking, for handshake use only)
static bool recv_frame_blocking(int fd, protocol::PacketType& out_type,
                                 std::vector<uint8_t>& out_payload) {
    uint8_t hdr[protocol::FRAME_HEADER_SIZE];
    if (::recv(fd, hdr, sizeof(hdr), MSG_WAITALL) !=
        static_cast<ssize_t>(protocol::FRAME_HEADER_SIZE)) return false;
    size_t off = 0;
    uint32_t plen = 0;
    std::span<const uint8_t> hs{hdr, sizeof(hdr)};
    if (!protocol::unpack_u32(hs, off, plen)) return false;
    out_type = static_cast<protocol::PacketType>(hdr[off]);
    out_payload.resize(plen);
    if (plen == 0) return true;
    return ::recv(fd, out_payload.data(), plen, MSG_WAITALL) ==
           static_cast<ssize_t>(plen);
}

bool AgentClient::perform_handshake() {
    // 1. Send HELLO: version + X25519 ephemeral pubkey
    std::vector<uint8_t> hello;
    protocol::pack_u16(hello, protocol::PROTOCOL_VERSION);
    hello.insert(hello.end(), exchange_keypair_.pub.begin(), exchange_keypair_.pub.end());
    protocol::pack_str(hello, "AgentChat-cpp/0.1.0");
    if (!send_frame(protocol::PacketType::HELLO, hello)) return false;

    // 2. Receive HELLO_ACK: server X25519 pubkey (32 bytes, no blob prefix)
    protocol::PacketType ptype{};
    std::vector<uint8_t> payload;
    if (!recv_frame_blocking(sockfd_, ptype, payload)) return false;
    if (ptype != protocol::PacketType::HELLO_ACK) return false;
    if (payload.size() < 32) return false;
    PublicKey srv_pub{};
    std::copy(payload.begin(), payload.begin() + 32, srv_pub.begin());

    // Derive session key
    auto shared = crypto::x25519_exchange(exchange_keypair_.priv, srv_pub);
    if (!shared) return false;
    session_key_ = crypto::hkdf_derive(
        std::span<const uint8_t>{shared->data(), shared->size()},
        "AgentChat-v1-session");
    if (session_key_.empty()) return false;

    // 3. Receive AUTH_CHALLENGE: 32-byte challenge (blob-prefixed)
    if (!recv_frame_blocking(sockfd_, ptype, payload)) return false;
    if (ptype != protocol::PacketType::AUTH_CHALLENGE) return false;
    size_t boff = 0;
    std::vector<uint8_t> challenge;
    if (!protocol::unpack_blob(std::span<const uint8_t>{payload}, boff, challenge)) return false;
    if (challenge.size() != 32) return false;

    // 4. Send AUTH_RESPONSE: agent_id (u64) + identity_pubkey (32 bytes) + Ed25519 sig (64 bytes)
    auto sig = crypto::ed25519_sign(identity_keypair_.priv,
                                    std::span<const uint8_t>{challenge});
    if (!sig) return false;
    std::vector<uint8_t> auth_resp;
    protocol::pack_u64(auth_resp, static_cast<uint64_t>(agent_id_));
    auth_resp.insert(auth_resp.end(),
                     identity_keypair_.pub.begin(), identity_keypair_.pub.end());
    auth_resp.insert(auth_resp.end(), sig->begin(), sig->end());
    if (!send_frame(protocol::PacketType::AUTH_RESPONSE, auth_resp)) return false;

    // 5. Receive AUTH_OK
    if (!recv_frame_blocking(sockfd_, ptype, payload)) return false;
    if (ptype != protocol::PacketType::AUTH_OK) return false;

    std::cout << "[client] Handshake complete, agent_id=" << static_cast<uint64_t>(agent_id_) << "\n";
    return true;
}

bool AgentClient::send_frame(
    protocol::PacketType        type,
    const std::vector<uint8_t>& payload)
{
    auto frame = protocol::encode_frame(type, std::span<const uint8_t>{payload});
    return ::send(sockfd_, frame.data(), frame.size(), MSG_NOSIGNAL)
           == static_cast<ssize_t>(frame.size());
}

MessageId AgentClient::next_message_id() {
    return MessageId{msg_id_counter_.fetch_add(1)};
}

MessageId AgentClient::send_message_impl(AgentId to, ChannelId ch, MessageType type, const std::vector<uint8_t>& encrypted_payload, const std::vector<uint8_t>& nonce) {
    auto mid = next_message_id();
    std::vector<uint8_t> payload;
    protocol::pack_u64(payload, static_cast<uint64_t>(to));
    protocol::pack_u64(payload, static_cast<uint64_t>(ch));
    payload.push_back(static_cast<uint8_t>(type));
    protocol::pack_blob(payload, std::span<const uint8_t>{encrypted_payload});
    protocol::pack_blob(payload, std::span<const uint8_t>{nonce});

    // H-02 fix: append Ed25519 signature
    std::vector<uint8_t> signed_data;
    protocol::pack_u64(signed_data, static_cast<uint64_t>(agent_id_));
    protocol::pack_u64(signed_data, static_cast<uint64_t>(to));
    protocol::pack_u64(signed_data, static_cast<uint64_t>(ch));
    signed_data.push_back(static_cast<uint8_t>(type));
    signed_data.insert(signed_data.end(), encrypted_payload.begin(), encrypted_payload.end());

    auto sig_opt = crypto::ed25519_sign(identity_keypair_.priv, signed_data);
    if (sig_opt) {
        payload.insert(payload.end(), sig_opt->begin(), sig_opt->end());
    }

    return send_frame(protocol::PacketType::SEND_MESSAGE, payload) ? mid : MessageId{0};
}

MessageId AgentClient::send_message(AgentId to, const std::string& text) {
    if (!connected_) return MessageId{0};
    std::vector<uint8_t> pt(text.begin(), text.end());
    return send_message_impl(to, ChannelId{0}, MessageType::TEXT, pt, {});
}

MessageId AgentClient::send_binary(AgentId to, std::vector<uint8_t> data) {
    if (!connected_) return MessageId{0};
    auto ct = crypto::aes_gcm_encrypt(std::span<const uint8_t>{session_key_}, std::span<const uint8_t>{data});
    if (ct.empty()) return MessageId{0};
    return send_message_impl(to, ChannelId{0}, MessageType::BINARY, ct, {});
}

MessageId AgentClient::send_command(AgentId to, const std::string& cmd_json) {
    if (!connected_) return MessageId{0};
    std::span<const uint8_t> pt{reinterpret_cast<const uint8_t*>(cmd_json.data()), cmd_json.size()};
    auto ct = crypto::aes_gcm_encrypt(std::span<const uint8_t>{session_key_}, pt);
    if (ct.empty()) return MessageId{0};
    return send_message_impl(to, ChannelId{0}, MessageType::AGENT_COMMAND, ct, {});
}

MessageId AgentClient::send_to_channel(ChannelId channel, const std::string& text) {
    if (!connected_) return MessageId{0};
    std::span<const uint8_t> pt{reinterpret_cast<const uint8_t*>(text.data()), text.size()};
    auto ct = crypto::aes_gcm_encrypt(std::span<const uint8_t>{session_key_}, pt);
    if (ct.empty()) return MessageId{0};
    return send_message_impl(AgentId{0}, channel, MessageType::TEXT, ct, {});
}

bool AgentClient::react_message(MessageId msg_id, const std::string& emoji) {
    if (!connected_) return false;
    std::vector<uint8_t> payload;
    protocol::pack_u64(payload, static_cast<uint64_t>(msg_id));
    protocol::pack_str(payload, emoji);
    return send_frame(protocol::PacketType::REACT_MESSAGE, payload);
}

bool AgentClient::register_agent(
    const std::string&              display_name,
    const std::vector<std::string>& capabilities)
{
    if (!connected_) return false;
    std::vector<uint8_t> payload;
    protocol::pack_str(payload, display_name);
    protocol::pack_u16(payload, static_cast<uint16_t>(capabilities.size()));
    for (auto& cap : capabilities) protocol::pack_str(payload, cap);
    // Include identity pubkey (32 bytes, raw)
    payload.insert(payload.end(),
                   identity_keypair_.pub.begin(), identity_keypair_.pub.end());
    // Include X25519 exchange pubkey (32 bytes, raw) — for E2E encryption
    payload.insert(payload.end(),
                   exchange_keypair_.pub.begin(), exchange_keypair_.pub.end());
    return send_frame(protocol::PacketType::REGISTER_AGENT, payload);
}

std::optional<std::vector<uint8_t>> AgentClient::send_and_wait(
    protocol::PacketType      req_type,
    protocol::PacketType      resp_type,
    const std::vector<uint8_t>& payload,
    std::chrono::milliseconds timeout)
{
    auto req = std::make_shared<PendingRequest>();
    auto future = req->promise.get_future();
    {
        std::lock_guard<std::mutex> lk(pending_mu_);
        pending_requests_[resp_type] = req;
    }
    if (!send_frame(req_type, payload)) {
        std::lock_guard<std::mutex> lk(pending_mu_);
        pending_requests_.erase(resp_type);
        return std::nullopt;
    }
    if (future.wait_for(timeout) == std::future_status::timeout) {
        std::lock_guard<std::mutex> lk(pending_mu_);
        pending_requests_.erase(resp_type);
        return std::nullopt;
    }
    return future.get();
}

std::vector<AgentInfo> AgentClient::list_agents() {
    if (!connected_) return {};
    auto resp = send_and_wait(
        protocol::PacketType::LIST_AGENTS,
        protocol::PacketType::AGENT_LIST,
        {});
    if (!resp) return {};
    std::vector<AgentInfo> agents;
    size_t off = 0;
    std::span<const uint8_t> ps{*resp};
    uint16_t count = 0;
    if (!protocol::unpack_u16(ps, off, count)) return {};
    for (uint16_t i = 0; i < count; ++i) {
        uint64_t id = 0;
        std::string name;
        if (!protocol::unpack_u64(ps, off, id)) break;
        protocol::unpack_str(ps, off, name);  // optional name field
        agents.push_back(AgentInfo{AgentId{id}, name, true});
    }
    return agents;
}

std::optional<Channel> AgentClient::create_channel(
    const std::string&          name,
    ChannelType                 type,
    const std::vector<AgentId>& members)
{
    if (!connected_) return std::nullopt;
    std::vector<uint8_t> payload;
    protocol::pack_str(payload, name);
    payload.push_back(static_cast<uint8_t>(type));
    protocol::pack_u16(payload, static_cast<uint16_t>(members.size()));
    for (auto& m : members) protocol::pack_u64(payload, static_cast<uint64_t>(m));
    auto resp = send_and_wait(
        protocol::PacketType::CREATE_CHANNEL,
        protocol::PacketType::CHANNEL_CREATED,
        payload);
    if (!resp || resp->size() < 8) return std::nullopt;
    size_t off = 0;
    std::span<const uint8_t> ps{*resp};
    uint64_t ch_id = 0;
    std::string ch_name;
    if (!protocol::unpack_u64(ps, off, ch_id)) return std::nullopt;
    protocol::unpack_str(ps, off, ch_name);  // server echoes name
    if (ch_name.empty()) ch_name = name;
    return Channel{ChannelId{ch_id}, ch_name, type, members};
}

bool AgentClient::join_channel(ChannelId channel) {
    if (!connected_) return false;
    std::vector<uint8_t> payload;
    protocol::pack_u64(payload, static_cast<uint64_t>(channel));
    return send_frame(protocol::PacketType::JOIN_CHANNEL, payload);
}

bool AgentClient::leave_channel(ChannelId channel) {
    if (!connected_) return false;
    std::vector<uint8_t> payload;
    protocol::pack_u64(payload, static_cast<uint64_t>(channel));
    return send_frame(protocol::PacketType::LEAVE_CHANNEL, payload);
}

void AgentClient::on_message(MessageCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mu_);
    on_message_cb_ = std::move(cb);
}
void AgentClient::on_connect(ConnectionCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mu_);
    on_connect_cb_ = std::move(cb);
}
void AgentClient::on_error(ErrorCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mu_);
    on_error_cb_ = std::move(cb);
}

void AgentClient::recv_thread_fn() {
    constexpr size_t CHUNK = 4096;
    while (running_) {
        uint8_t tmp[CHUNK];
        ssize_t n = ::recv(sockfd_, tmp, CHUNK, 0);
        if (n <= 0) { connected_ = false; break; }
        recv_buf_.insert(recv_buf_.end(), tmp, tmp + n);
        while (true) {
            protocol::PacketType ptype;
            std::vector<uint8_t> pload;
            size_t consumed = 0;
            if (!protocol::decode_frame(
                    std::span<const uint8_t>{recv_buf_}, ptype, pload, consumed))
                break;
            recv_buf_.erase(recv_buf_.begin(),
                            recv_buf_.begin() + static_cast<ptrdiff_t>(consumed));
            // ── Resolve pending sync requests first ───────────────────────────
            {
                std::lock_guard<std::mutex> lk(pending_mu_);
                auto it = pending_requests_.find(ptype);
                if (it != pending_requests_.end()) {
                    it->second->promise.set_value(pload);
                    pending_requests_.erase(it);
                    continue;
                }
            }
            if (ptype == protocol::PacketType::RECV_MESSAGE) {
                size_t off = 0;
                std::span<const uint8_t> ps{pload};
                uint64_t from_id = 0, ch_id = 0, mid = 0;
                if (!protocol::unpack_u64(ps, off, from_id)) continue;
                if (!protocol::unpack_u64(ps, off, ch_id))   continue;
                uint8_t mtype = pload[off++];
                std::vector<uint8_t> ct;
                if (!protocol::unpack_blob(ps, off, ct)) continue;
                protocol::unpack_u64(ps, off, mid);
                uint64_t seqno = 0;
                protocol::unpack_u64(ps, off, seqno); // v0.3 E2EE seqno binding
                (void)seqno; // available for out-of-order detection by caller
                auto plain = crypto::aes_gcm_decrypt(
                    std::span<const uint8_t>{session_key_},
                    std::span<const uint8_t>{ct});
                Message msg;
                msg.id        = MessageId{mid};
                msg.from      = AgentId{from_id};
                msg.channel   = ChannelId{ch_id};
                msg.type      = static_cast<MessageType>(mtype);
                // If decrypt fails (e.g. msg encrypted with sender's session key),
                // fall back to raw payload — until per-agent E2E key exchange is implemented
                if (!plain.empty()) {
                    msg.payload = plain;
                } else {
                    // plaintext path (no encryption)
                    msg.payload = ct;
                }
                // debug
                std::cerr << "[recv] from=" << from_id << " ct.size=" << ct.size()
                          << " text='" << std::string(ct.begin(), ct.end()) << "'\n";
                msg.timestamp = now_ms();
                std::lock_guard<std::mutex> lk(cb_mu_);
                if (on_message_cb_) on_message_cb_(msg);
            } else if (ptype == protocol::PacketType::PING) {
                std::vector<uint8_t> empty;
                send_frame(protocol::PacketType::PONG, empty);
            }
        }
    }
}

std::unique_ptr<AgentClient> make_agent_client(
    const std::string& host,
    uint16_t           port,
    AgentId            agent_id,
    crypto::KeyPair*   out_identity,
    crypto::KeyPair*   out_exchange)
{
    auto id_kp = crypto::generate_ed25519_keypair();
    auto ex_kp = crypto::generate_x25519_keypair();
    if (!id_kp || !ex_kp) return nullptr;
    if (out_identity) *out_identity = *id_kp;
    if (out_exchange)  *out_exchange  = *ex_kp;
    return std::make_unique<AgentClient>(
        host, port, agent_id, std::move(*id_kp), std::move(*ex_kp));
}

// ── E2EE: Double Ratchet + X3DH ─────────────────────────────────────────────

bool AgentClient::init_e2ee_session(AgentId peer, const crypto::PrekeyBundle& bundle) {
    auto result = crypto::x3dh_sender(identity_keypair_, bundle);
    if (!result) return false;

    crypto::RatchetState state;
    if (!crypto::ratchet_init_sender(result->shared_secret, bundle.signed_prekey, state))
        return false;

    std::lock_guard<std::mutex> lk(ratchet_mu_);
    ratchet_sessions_[static_cast<uint64_t>(peer)] = std::move(state);
    e2ee_enabled_ = true;
    std::cout << "[e2ee] Session initialised with agent " << static_cast<uint64_t>(peer) << "\n";
    return true;
}

std::vector<uint8_t> AgentClient::e2ee_encrypt(AgentId peer, const std::vector<uint8_t>& plaintext) {
    std::lock_guard<std::mutex> lk(ratchet_mu_);
    auto it = ratchet_sessions_.find(static_cast<uint64_t>(peer));
    if (it == ratchet_sessions_.end()) {
        // No ratchet session yet — fall back to AES session key
        return aes_gcm_encrypt(
            std::span<const uint8_t>{session_key_},
            std::span<const uint8_t>{plaintext});
    }
    auto msg = crypto::ratchet_encrypt(it->second, plaintext);
    if (!msg) {
        // Ratchet failed — fall back
        return aes_gcm_encrypt(
            std::span<const uint8_t>{session_key_},
            std::span<const uint8_t>{plaintext});
    }
    // Serialise RatchetMessage: [32 dh_pub][4 prev][4 num][12 nonce][ciphertext]
    std::vector<uint8_t> out;
    out.insert(out.end(), msg->header.dh_public.begin(), msg->header.dh_public.end());
    uint32_t prev = msg->header.prev_count;
    uint32_t num  = msg->header.msg_num;
    out.push_back((prev >> 24) & 0xFF); out.push_back((prev >> 16) & 0xFF);
    out.push_back((prev >>  8) & 0xFF); out.push_back(prev & 0xFF);
    out.push_back((num  >> 24) & 0xFF); out.push_back((num  >> 16) & 0xFF);
    out.push_back((num  >>  8) & 0xFF); out.push_back(num  & 0xFF);
    out.insert(out.end(), msg->nonce.begin(), msg->nonce.end());
    out.insert(out.end(), msg->ciphertext.begin(), msg->ciphertext.end());
    return out;
}

std::optional<std::vector<uint8_t>> AgentClient::e2ee_decrypt(AgentId peer, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lk(ratchet_mu_);
    auto it = ratchet_sessions_.find(static_cast<uint64_t>(peer));
    if (it == ratchet_sessions_.end() || data.size() < 52) {
        // No session or too short — try AES fallback
        return aes_gcm_decrypt(
            std::span<const uint8_t>{session_key_},
            std::span<const uint8_t>{data});
    }
    // Deserialise RatchetMessage
    crypto::RatchetMessage msg;
    std::copy(data.begin(), data.begin()+32, msg.header.dh_public.begin());
    msg.header.prev_count = ((uint32_t)data[32]<<24)|((uint32_t)data[33]<<16)|
                            ((uint32_t)data[34]<<8)|(uint32_t)data[35];
    msg.header.msg_num    = ((uint32_t)data[36]<<24)|((uint32_t)data[37]<<16)|
                            ((uint32_t)data[38]<<8)|(uint32_t)data[39];
    std::copy(data.begin()+40, data.begin()+52, msg.nonce.begin());
    msg.ciphertext = std::vector<uint8_t>(data.begin()+52, data.end());
    return crypto::ratchet_decrypt(it->second, msg);
}

} // namespace agentchat