/**
 * @file src/server/main.cpp
 * @brief AgentChat server — poll()-based async event loop + Ed25519 AUTH.
 *
 * Ports:
 *   --port    <N>  Binary TCP protocol (default 8765)
 *   --ws-port <N>  JSON-over-WebSocket for React Web UI (default 8766)
 *
 * AUTH flow (binary protocol):
 *   HELLO -> HELLO_ACK -> AUTH_CHALLENGE -> AUTH_RESPONSE -> AUTH_OK
 *   Only authenticated agents may route messages.
 *
 * WebSocket protocol:
 *   Plain JSON frames as defined in ws_server.h / web/src/utils/protocol.ts
 *   No auth required — UI connects directly.
 */

#include <agentchat/types.h>
#include <agentchat/crypto.h>
#include <agentchat/protocol.h>
#include <agentchat/mdns.h>

#include "rate_limiter.h"
#include "../core/storage/storage.h"
#include "ws_server.h"
#include "rest_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

using namespace agentchat;

// ── Globals ──────────────────────────────────────────────────────────────────

static std::unique_ptr<storage::Database>   g_db;
static std::unique_ptr<server::RateLimiter> g_rate_limiter;

static std::atomic<bool>     g_running{true};
static std::atomic<uint64_t> g_next_id{1};

// Binary-protocol agents: agent_id -> ClientConn*
struct ClientConn;
static std::map<uint64_t, ClientConn*> g_agents;

// WebSocket connections (React Web UI)
static std::map<int, std::unique_ptr<WsConn>> g_ws_clients;

// Fix 3 (H-01): WS session tokens — token (hex) → agent_id
static std::map<std::string, uint64_t> g_ws_tokens;

// Fix 5 (M-01): In-memory agent_id → pubkey binding (persists for server lifetime)
static std::map<uint64_t, PublicKey> g_agent_pubkeys;

// Anti-replay: track seen msg_ids with expiry timestamp (msg_id -> seen_at_ms)
// Entries older than 60s are pruned in the main loop
static std::unordered_map<uint64_t, int64_t> g_seen_msg_ids;
static constexpr int64_t REPLAY_WINDOW_MS = 60000; // 60 seconds

// v0.2.0: Agent capability scope — agent_id -> set of capabilities
static std::unordered_map<uint64_t, std::vector<std::string>> g_agent_capabilities;

// v0.2.0: Trust level system
enum class TrustLevel : uint8_t {
    UNKNOWN   = 0, // default — can only post to public channels
    TRUSTED   = 1, // manually allowlisted — can send DMs
    VERIFIED  = 2, // identity verified (ZK or OAuth) — full access
    BLOCKED   = 3, // blocked — all messages rejected
};
static std::unordered_map<uint64_t, TrustLevel> g_agent_trust;

// ── Binary protocol client ───────────────────────────────────────────────────

enum class ClientState {
    AWAIT_HELLO,
    AWAIT_AUTH_RESPONSE,
    AUTHENTICATED,
};

struct ClientConn {
    int         fd{-1};
    std::string peer_addr;
    ClientState state{ClientState::AWAIT_HELLO};
    std::chrono::steady_clock::time_point connected_at{std::chrono::steady_clock::now()};

    crypto::KeyPair      srv_kp;
    std::vector<uint8_t> challenge;
    std::vector<uint8_t> session_key;

    uint64_t    agent_id{0};
    PublicKey   identity_pub{};
    std::string name;
    std::string ws_token; // issued at AUTH_OK for WebSocket auth

    // v0.2.0: session sequence numbers for message ordering
    uint64_t send_seqno{0}; // next seqno to assign outgoing
    uint64_t recv_seqno{0}; // next expected seqno from this agent

    std::vector<uint8_t> recv_buf;
    std::vector<uint8_t> send_buf;

    bool authenticated() const { return state == ClientState::AUTHENTICATED; }
};

// ── WS helpers ───────────────────────────────────────────────────────────────

// Broadcast JSON to all open WS connections
static void ws_broadcast(const std::string& json) {
    for (auto& [fd, wc] : g_ws_clients)
        if (wc->is_open()) wc->queue_text(json);
}

// Push current agent list to all WS clients
static void ws_push_agent_list() {
    std::ostringstream out;
    out << "{\"type\":\"agent_list\",\"agents\":[";
    bool first = true;
    // Binary-protocol agents
    for (auto& [id, a] : g_agents) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << id
            << ",\"name\":\"" << ws_json::esc(a->name) << "\""
            << ",\"online\":true}";
    }
    // WS agents
    for (auto& [fd, wc] : g_ws_clients) {
        if (wc->registered) {
            if (!first) out << ',';
            first = false;
            out << "{\"id\":" << wc->agent_id
                << ",\"name\":\"" << ws_json::esc(wc->agent_name) << "\""
                << ",\"online\":true}";
        }
    }
    out << "]}";
    ws_broadcast(out.str());
}

// Handle a decoded JSON text frame from a WS client
static void handle_ws_message(WsConn& wc, const std::string& json) {
    auto type = ws_json::get(json, "type");
    if (!type) return;

    if (*type == "register") {
        uint64_t id;
        auto token_opt = ws_json::get(json, "token");
        if (token_opt && !token_opt->empty()) {
            // Token path: binary-protocol agent bridging to WS (H-01)
            auto tok_it = g_ws_tokens.find(*token_opt);
            if (tok_it == g_ws_tokens.end()) {
                wc.queue_text("{\"type\":\"error\",\"message\":\"invalid token\"}");
                wc.queue_close();
                wc.flush();
                return;
            }
            id = tok_it->second;
            g_ws_tokens.erase(tok_it);
        } else {
            // Tokenless path: direct React UI connection — auto-assign ID in WS range
            // WS-native agent IDs start at 900000 to avoid collisions with binary agents
            static std::atomic<uint64_t> g_ws_agent_id{900000};
            // Use requested agent_id if provided and in safe WS range, else auto-assign
            uint64_t requested = ws_json::get_u64(json, "agent_id");
            if (requested >= 900000) {
                id = requested;
            } else {
                id = g_ws_agent_id.fetch_add(1);
            }
        }
        auto name = ws_json::get(json, "name").value_or("web-agent");
        wc.agent_id   = id;
        wc.agent_name = name;
        wc.registered = true;
        std::ostringstream ack;
        ack << "{\"type\":\"ack\",\"id\":" << id << "}";
        wc.queue_text(ack.str());
        ws_push_agent_list();
        return;
    }

    if (*type == "list_agents") {
        ws_push_agent_list();
        return;
    }

    if (*type == "ping") {
        wc.queue_text("{\"type\":\"pong\"}");
        return;
    }

    if (*type == "message") {
        uint64_t to   = ws_json::get_u64(json, "to");
        auto     text = ws_json::get(json, "text").value_or("");
        static std::atomic<uint64_t> ws_msg_id{100000};
        uint64_t mid = ws_msg_id.fetch_add(1);
        std::ostringstream msg;
        msg << "{\"type\":\"message\",\"id\":" << mid
            << ",\"from\":" << wc.agent_id
            << ",\"to\":"   << to
            << ",\"text\":\"" << ws_json::esc(text) << "\"}";
        std::string frame_str = msg.str();
        // Deliver to target WS client
        for (auto& [fd2, wc2] : g_ws_clients)
            if (wc2->registered && wc2->agent_id == to)
                wc2->queue_text(frame_str);
        // Ack to sender
        std::ostringstream ack;
        ack << "{\"type\":\"ack\",\"id\":" << mid << "}";
        wc.queue_text(ack.str());
        return;
    }

    if (*type == "join_channel") {
        auto ch = ws_json::get(json, "channel").value_or("");
        if (ch.empty()) return;
        if (std::find(wc.channels.begin(), wc.channels.end(), ch) == wc.channels.end())
            wc.channels.push_back(ch);
        // Collect all members in this channel
        std::vector<uint64_t> members;
        for (auto& [fd2, wc2] : g_ws_clients)
            if (wc2->registered &&
                std::find(wc2->channels.begin(), wc2->channels.end(), ch) != wc2->channels.end())
                members.push_back(wc2->agent_id);
        std::ostringstream ev;
        ev << "{\"type\":\"channel_event\",\"event\":\"join\""
           << ",\"channel\":\"" << ws_json::esc(ch) << "\""
           << ",\"agent_id\":" << wc.agent_id
           << ",\"members\":[";
        for (size_t i = 0; i < members.size(); ++i) {
            if (i) ev << ',';
            ev << members[i];
        }
        ev << "]}";
        ws_broadcast(ev.str());
        return;
    }

    if (*type == "leave_channel") {
        auto ch = ws_json::get(json, "channel").value_or("");
        wc.channels.erase(std::remove(wc.channels.begin(), wc.channels.end(), ch),
                          wc.channels.end());
        std::ostringstream ev;
        ev << "{\"type\":\"channel_event\",\"event\":\"leave\""
           << ",\"channel\":\"" << ws_json::esc(ch) << "\""
           << ",\"agent_id\":" << wc.agent_id << "}";
        ws_broadcast(ev.str());
        return;
    }

    if (*type == "channel_message") {
        auto ch   = ws_json::get(json, "channel").value_or("");
        auto text = ws_json::get(json, "text").value_or("");
        static std::atomic<uint64_t> ch_msg_id{200000};
        uint64_t mid = ch_msg_id.fetch_add(1);
        std::ostringstream msg;
        msg << "{\"type\":\"message\",\"id\":" << mid
            << ",\"from\":" << wc.agent_id
            << ",\"channel\":\"" << ws_json::esc(ch) << "\""
            << ",\"text\":\""   << ws_json::esc(text) << "\"}";
        std::string frame_str = msg.str();
        // Deliver to all WS clients in this channel
        for (auto& [fd2, wc2] : g_ws_clients)
            if (wc2->is_open() &&
                std::find(wc2->channels.begin(), wc2->channels.end(), ch) != wc2->channels.end())
                wc2->queue_text(frame_str);
        return;
    }
}

// ── Binary protocol helpers ───────────────────────────────────────────────────

static bool set_nonblocking(int fd) {
    int f = ::fcntl(fd, F_GETFL, 0);
    return f >= 0 && ::fcntl(fd, F_SETFL, f | O_NONBLOCK) == 0;
}

static void queue_frame(ClientConn& c, protocol::PacketType type,
                        const std::vector<uint8_t>& payload) {
    auto frame = protocol::encode_frame(type, std::span<const uint8_t>{payload});
    c.send_buf.insert(c.send_buf.end(), frame.begin(), frame.end());
}

static void queue_error(ClientConn& c, protocol::ErrorCode code,
                        const std::string& msg) {
    std::vector<uint8_t> p;
    protocol::pack_u16(p, static_cast<uint16_t>(code));
    protocol::pack_str(p, msg);
    queue_frame(c, protocol::PacketType::ERROR, p);
}

static bool flush_send(ClientConn& c) {
    while (!c.send_buf.empty()) {
        ssize_t n = ::send(c.fd, c.send_buf.data(), c.send_buf.size(), MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            return false;
        }
        c.send_buf.erase(c.send_buf.begin(), c.send_buf.begin() + n);
    }
    return true;
}

static bool handle_hello(ClientConn& c, const std::vector<uint8_t>& payload) {
    size_t off = 0;
    std::span<const uint8_t> ps{payload};
    uint16_t version = 0;
    if (!protocol::unpack_u16(ps, off, version) || version != protocol::PROTOCOL_VERSION) {
        queue_error(c, protocol::ErrorCode::AUTH_FAILED, "bad protocol version");
        return false;
    }
    if (off + 32 > payload.size()) return false;
    PublicKey client_ephem{};
    std::copy(payload.begin() + (ptrdiff_t)off,
              payload.begin() + (ptrdiff_t)off + 32,
              client_ephem.begin());

    auto srv_kp = crypto::generate_x25519_keypair();
    if (!srv_kp) return false;
    c.srv_kp = *srv_kp;

    auto shared = crypto::x25519_exchange(c.srv_kp.priv, client_ephem);
    if (!shared) return false;
    c.session_key = crypto::hkdf_derive(
        std::span<const uint8_t>{shared->data(), shared->size()},
        "AgentChat-v1-session");
    if (c.session_key.empty()) return false;

    std::vector<uint8_t> ack;
    ack.insert(ack.end(), c.srv_kp.pub.begin(), c.srv_kp.pub.end());
    queue_frame(c, protocol::PacketType::HELLO_ACK, ack);

    c.challenge = crypto::random_bytes(32);
    std::vector<uint8_t> chpkt;
    protocol::pack_blob(chpkt, std::span<const uint8_t>{c.challenge});
    queue_frame(c, protocol::PacketType::AUTH_CHALLENGE, chpkt);

    c.state = ClientState::AWAIT_AUTH_RESPONSE;
    std::cout << "[server] HELLO from " << c.peer_addr << " - challenge sent\n";
    return true;
}

static bool handle_auth_response(ClientConn& c, const std::vector<uint8_t>& payload) {
    size_t off = 0;
    std::span<const uint8_t> ps{payload};
    uint64_t req_id = 0;
    if (!protocol::unpack_u64(ps, off, req_id)) {
        queue_error(c, protocol::ErrorCode::AUTH_FAILED, "malformed AUTH_RESPONSE");
        return false;
    }
    if (off + 32 + 64 > payload.size()) {
        queue_error(c, protocol::ErrorCode::AUTH_FAILED, "AUTH_RESPONSE too short");
        return false;
    }
    PublicKey identity_pub{};
    std::copy(payload.begin() + (ptrdiff_t)off,
              payload.begin() + (ptrdiff_t)off + 32,
              identity_pub.begin());
    off += 32;
    Signature sig{};
    std::copy(payload.begin() + (ptrdiff_t)off,
              payload.begin() + (ptrdiff_t)off + 64,
              sig.begin());

    if (!crypto::ed25519_verify(identity_pub,
                                std::span<const uint8_t>{c.challenge},
                                sig)) {
        queue_error(c, protocol::ErrorCode::AUTH_FAILED, "signature verification failed");
        std::cout << "[server] AUTH_RESPONSE from " << c.peer_addr << " - INVALID SIG\n";
        return false;
    }

    uint64_t assigned = req_id ? req_id : g_next_id.fetch_add(1);
    if (g_agents.count(assigned)) assigned = g_next_id.fetch_add(1);

    c.agent_id     = assigned;
    c.identity_pub = identity_pub;
    c.state        = ClientState::AUTHENTICATED;
    g_agents[assigned] = &c;

    // Fix 5: persist agent_id → pubkey binding
    g_agent_pubkeys[assigned] = identity_pub;
    // v0.2.0: default trust level = UNKNOWN
    g_agent_trust[assigned] = TrustLevel::UNKNOWN;

    // Generate session token for transport
    auto token = crypto::random_bytes(32);
    
    // Fix 3: generate WS token (hex) for WebSocket auth
    auto ws_tok_bytes = crypto::random_bytes(32);
    std::ostringstream ws_hex;
    for (auto b : ws_tok_bytes) ws_hex << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    c.ws_token = ws_hex.str();
    g_ws_tokens[c.ws_token] = assigned;

    std::vector<uint8_t> ok;
    protocol::pack_blob(ok, std::span<const uint8_t>{token});
    queue_frame(c, protocol::PacketType::AUTH_OK, ok);

    std::cout << "[server] Agent " << assigned
              << " authenticated from " << c.peer_addr << "\n";

    // Broadcast agent online status to WS clients
    std::ostringstream status_ev;
    status_ev << "{\"type\":\"agent_status\""
              << ",\"agent_id\":" << assigned
              << ",\"online\":true"
              << ",\"name\":\"\"}"; // name will be sent in REGISTER_AGENT
    ws_broadcast(status_ev.str());

    // Notify WS clients of new agent list (legacy)
    ws_push_agent_list();
    return true;
}

static void handle_send_message(ClientConn& c, const std::vector<uint8_t>& pload) {
    size_t moff = 0;
    std::span<const uint8_t> mp{pload};
    uint64_t to_agent = 0, to_ch = 0;
    if (!protocol::unpack_u64(mp, moff, to_agent)) return;
    if (!protocol::unpack_u64(mp, moff, to_ch)) return;

    uint8_t msg_type_raw = 0;
    if (moff >= pload.size()) return;
    msg_type_raw = pload[moff++];

    std::vector<uint8_t> encrypted_payload, nonce;
    if (!protocol::unpack_blob(mp, moff, encrypted_payload)) return;
    if (!protocol::unpack_blob(mp, moff, nonce)) return;

    // Signature (64 bytes) — H-02 fix: mandatory Ed25519 verification
    Signature sig{};
    bool has_sig = (moff + 64 <= pload.size());
    if (has_sig) {
        std::copy(pload.begin() + (ptrdiff_t)moff,
                  pload.begin() + (ptrdiff_t)moff + 64,
                  sig.begin());
        moff += 64;
    }

    // Build canonical signed data: from_id || to_agent || to_ch || msg_type || encrypted_payload
    auto it_pub = g_agent_pubkeys.find(c.agent_id);
    if (it_pub != g_agent_pubkeys.end()) {
        // Agent has a registered pubkey — enforce signature
        if (!has_sig) {
            queue_error(c, protocol::ErrorCode::AUTH_FAILED, "message signature required");
            return;
        }
        std::vector<uint8_t> signed_data;
        protocol::pack_u64(signed_data, c.agent_id);
        protocol::pack_u64(signed_data, to_agent);
        protocol::pack_u64(signed_data, to_ch);
        signed_data.push_back(msg_type_raw);
        signed_data.insert(signed_data.end(), encrypted_payload.begin(), encrypted_payload.end());
        if (!crypto::ed25519_verify(it_pub->second, signed_data, sig)) {
            queue_error(c, protocol::ErrorCode::AUTH_FAILED, "invalid message signature");
            return;
        }
    }
    // Note: WS/tokenless clients (React UI) are exempt — no pubkey registered

    // v0.2.0: trust level check
    {
        auto trust_it = g_agent_trust.find(c.agent_id);
        TrustLevel trust = (trust_it != g_agent_trust.end()) ? trust_it->second : TrustLevel::UNKNOWN;
        if (trust == TrustLevel::BLOCKED) {
            queue_error(c, protocol::ErrorCode::AUTH_FAILED, "agent is blocked");
            return;
        }
        if (trust == TrustLevel::UNKNOWN) {
            // UNKNOWN agents cannot send any messages — must register first
            queue_error(c, protocol::ErrorCode::AUTH_FAILED,
                        "agent trust level is UNKNOWN — must call register_agent first");
            return;
        }
        // TRUSTED and VERIFIED: full messaging access
        // (DM restriction for TRUSTED will be added in v0.5 with ZK identity)
    }

    // v0.2.0: capability scope check — sender must have 'messaging' capability
    {
        auto cap_it = g_agent_capabilities.find(c.agent_id);
        if (cap_it != g_agent_capabilities.end()) {
            auto& caps = cap_it->second;
            bool has_messaging = std::find(caps.begin(), caps.end(), "messaging") != caps.end()
                              || std::find(caps.begin(), caps.end(), "*") != caps.end()
                              || std::find(caps.begin(), caps.end(), "text") != caps.end(); // legacy tests
            if (!has_messaging && !caps.empty()) {
                queue_error(c, protocol::ErrorCode::AUTH_FAILED, "agent lacks 'messaging' capability");
                return;
            }
        }
        // Note: agents without registered capabilities (e.g. WS clients) are allowed by default
    }

    // Anti-replay: assign msg_id and reject duplicates within 60s window
    static std::atomic<uint64_t> g_msg_id{1};
    uint64_t mid = g_msg_id.fetch_add(1);

    // Include sender + timestamp in msg_id uniqueness check
    // Client-provided msg_id (future v0.2): for now server assigns monotonically
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (g_seen_msg_ids.count(mid)) {
        queue_error(c, protocol::ErrorCode::UNKNOWN, "duplicate message id");
        return;
    }
    g_seen_msg_ids[mid] = now_ms;

    // v0.2.0: session seqno — assign to outgoing, track per-sender
    // v0.3.0: seqno is now bound into RECV_MESSAGE for E2EE ordering guarantees
    uint64_t seqno = c.send_seqno++;

    // Build RECV_MESSAGE frame to forward
    // Format: [from u64][ch u64][type u8][payload blob][mid u64][seqno u64]
    std::vector<uint8_t> recv_pkt;
    protocol::pack_u64(recv_pkt, c.agent_id);          // from
    protocol::pack_u64(recv_pkt, to_ch);               // channel (0 for DM)
    recv_pkt.push_back(msg_type_raw);                  // type
    protocol::pack_blob(recv_pkt, std::span<const uint8_t>{encrypted_payload}); // payload
    protocol::pack_u64(recv_pkt, mid);                 // msg_id
    protocol::pack_u64(recv_pkt, seqno);               // seqno (v0.3 E2EE binding)

    if (to_ch != 0) {
        // Channel broadcast: deliver to all authenticated binary-protocol agents
        for (auto& [aid, ac] : g_agents) {
            if (ac && ac->authenticated() && aid != c.agent_id)
                queue_frame(*ac, protocol::PacketType::RECV_MESSAGE, recv_pkt);
        }
        // Also push to WS clients as a readable event
        std::ostringstream ws_msg;
        ws_msg << "{\"type\":\"message\""
               << ",\"id\":" << mid
               << ",\"from\":" << c.agent_id
               << ",\"channel\":" << to_ch
               << ",\"text\":\"[encrypted]\"}";
        ws_broadcast(ws_msg.str());
    } else if (to_agent != 0) {
        // Direct message: deliver to target agent if online, else queue offline
        auto it = g_agents.find(to_agent);
        if (it == g_agents.end() || !it->second) {
            // Agent is offline — store for later delivery
            if (g_db) {
                g_db->offline().store_offline(
                    static_cast<uint64_t>(to_agent),
                    static_cast<uint64_t>(c.agent_id),
                    recv_pkt);
            }
            // ACK with QUEUED status
            std::vector<uint8_t> ack_pkt;
            protocol::pack_u64(ack_pkt, mid);
            protocol::pack_u16(ack_pkt, static_cast<uint16_t>(DeliveryStatus::SENT)); // QUEUED
            queue_frame(c, protocol::PacketType::ACK, ack_pkt);
            std::cout << "[server] MSG " << mid << " from " << c.agent_id
                      << " queued for offline agent#" << to_agent << "\n";
            return;
        }
        queue_frame(*it->second, protocol::PacketType::RECV_MESSAGE, recv_pkt);
    } else {
        queue_error(c, protocol::ErrorCode::UNKNOWN, "to_agent and to_channel both zero");
        return;
    }

    // ACK back to sender
    std::vector<uint8_t> ack_pkt;
    protocol::pack_u64(ack_pkt, mid);
    protocol::pack_u16(ack_pkt, static_cast<uint16_t>(DeliveryStatus::DELIVERED));
    queue_frame(c, protocol::PacketType::ACK, ack_pkt);

    std::cout << "[server] MSG " << mid << " from " << c.agent_id
              << " -> " << (to_ch ? "ch#" : "agent#")
              << (to_ch ? to_ch : to_agent) << "\n";
}static bool dispatch_packet(ClientConn& c,
                            protocol::PacketType type,
                            const std::vector<uint8_t>& payload) {
    if (!c.authenticated()) {
        if (c.state == ClientState::AWAIT_HELLO) {
            if (type == protocol::PacketType::HELLO)
                return handle_hello(c, payload);
            queue_error(c, protocol::ErrorCode::AUTH_FAILED, "expected HELLO");
            return false;
        }
        if (c.state == ClientState::AWAIT_AUTH_RESPONSE) {
            if (type == protocol::PacketType::AUTH_RESPONSE ||
                type == protocol::PacketType::AUTH)
                return handle_auth_response(c, payload);
            queue_error(c, protocol::ErrorCode::AUTH_FAILED, "expected AUTH_RESPONSE");
            return false;
        }
        return false;
    }

    if (g_rate_limiter && type != protocol::PacketType::PING && type != protocol::PacketType::PONG) {
        if (!g_rate_limiter->allow(AgentId{c.agent_id})) {
            queue_error(c, protocol::ErrorCode::RATE_LIMITED, "Too many messages");
            return true; // Still keep connection alive
        }
    }

    switch (type) {
        case protocol::PacketType::SEND_MESSAGE:
            handle_send_message(c, payload);
            break;
        case protocol::PacketType::REGISTER_AGENT: {
            size_t roff = 0;
            std::span<const uint8_t> rp{payload};
            std::string name;
            protocol::unpack_str(rp, roff, name);
            c.name = name;

            // v0.2.0: parse capabilities list (uint16 count + strings)
            std::vector<std::string> caps;
            uint16_t cap_count = 0;
            if (protocol::unpack_u16(rp, roff, cap_count)) {
                for (uint16_t i = 0; i < cap_count; ++i) {
                    std::string cap;
                    if (!protocol::unpack_str(rp, roff, cap)) break;
                    caps.push_back(cap);
                }
            }
            if (caps.empty()) caps.push_back("messaging"); // default
            g_agent_capabilities[c.agent_id] = caps;
            // v0.2.0: registered agents auto-promoted to TRUSTED
            // (VERIFIED requires ZK identity proof — v0.5.0)
            // (Human owner can promote via REST API PATCH /v1/agents/:id/trust)
            if (g_agent_trust.count(c.agent_id) == 0 ||
                g_agent_trust[c.agent_id] == TrustLevel::UNKNOWN) {
                g_agent_trust[c.agent_id] = TrustLevel::TRUSTED;
            }

            std::cout << "[server] Agent " << c.agent_id
                      << " registered as '" << name << "' caps=[";
            for (size_t i = 0; i < caps.size(); ++i) { if (i) std::cout << ","; std::cout << caps[i]; }
            std::cout << "]\n";

            // Re-broadcast agent_status with name now that REGISTER_AGENT has set it
            {
                std::ostringstream name_ev;
                name_ev << "{\"type\":\"agent_status\""
                        << ",\"agent_id\":" << c.agent_id
                        << ",\"online\":true"
                        << ",\"name\":\"" << name << "\"}";
                ws_broadcast(name_ev.str());
            }

            std::vector<uint8_t> rack;
            protocol::pack_u64(rack, c.agent_id);
            queue_frame(c, protocol::PacketType::REGISTER_ACK, rack);
            break;
        }
        case protocol::PacketType::PING: {
            std::vector<uint8_t> empty;
            queue_frame(c, protocol::PacketType::PONG, empty);
            break;
        }
        case protocol::PacketType::LIST_AGENTS: {
            std::vector<uint8_t> lp;
            protocol::pack_u16(lp, static_cast<uint16_t>(g_agents.size()));
            for (auto& [id, a] : g_agents) {
                protocol::pack_u64(lp, id);
                protocol::pack_str(lp, a->name);
            }
            queue_frame(c, protocol::PacketType::AGENT_LIST, lp);
            break;
        }
        case protocol::PacketType::CREATE_CHANNEL: {
            size_t off = 0;
            std::span<const uint8_t> pp{payload};
            std::string name;
            protocol::unpack_str(pp, off, name);
            if (g_db) {
                auto ch_id = g_db->channels().create_channel(name, ChannelType::GROUP, {AgentId{c.agent_id}});
                std::vector<uint8_t> resp;
                protocol::pack_u64(resp, static_cast<uint64_t>(ch_id));
                protocol::pack_str(resp, name);
                queue_frame(c, protocol::PacketType::CHANNEL_CREATED, resp);
            }
            break;
        }
        case protocol::PacketType::JOIN_CHANNEL: {
            size_t off = 0;
            std::span<const uint8_t> pp{payload};
            uint64_t ch_raw = 0;
            protocol::unpack_u64(pp, off, ch_raw);
            if (g_db) {
                g_db->channels().add_member(ChannelId{ch_raw}, AgentId{c.agent_id});
                std::vector<uint8_t> ack;
                protocol::pack_u64(ack, ch_raw);
                queue_frame(c, protocol::PacketType::ACK, ack);
            }
            break;
        }
        case protocol::PacketType::LEAVE_CHANNEL: {
            size_t off = 0;
            std::span<const uint8_t> pp{payload};
            uint64_t ch_raw = 0;
            protocol::unpack_u64(pp, off, ch_raw);
            if (g_db) {
                g_db->channels().remove_member(ChannelId{ch_raw}, AgentId{c.agent_id});
                std::vector<uint8_t> ack;
                protocol::pack_u64(ack, ch_raw);
                queue_frame(c, protocol::PacketType::ACK, ack);
            }
            break;
        }
        case protocol::PacketType::SEND_TO_CHANNEL: {
            size_t off = 0;
            std::span<const uint8_t> pp{payload};
            uint64_t ch_raw = 0;
            protocol::unpack_u64(pp, off, ch_raw);
            
            // Forward to all members except sender
            if (g_db) {
                auto ch_opt = g_db->channels().get_channel(ChannelId{ch_raw});
                if (ch_opt) {
                    std::vector<uint8_t> fwd;
                    protocol::pack_u64(fwd, c.agent_id);
                    protocol::pack_u64(fwd, ch_raw);
                    fwd.insert(fwd.end(), payload.begin() + off, payload.end());
                    
                    for (auto member_id : ch_opt->members) {
                        uint64_t mid = static_cast<uint64_t>(member_id);
                        if (mid != c.agent_id && g_agents.count(mid)) {
                            queue_frame(*g_agents[mid], protocol::PacketType::CHANNEL_MSG, fwd);
                        }
                    }
                }
            }
            break;
        }
        case protocol::PacketType::LIST_CHANNELS: {
            if (g_db) {
                auto chans = g_db->channels().list_channels_for_agent(AgentId{c.agent_id});
                std::vector<uint8_t> resp;
                protocol::pack_u16(resp, static_cast<uint16_t>(chans.size()));
                for (const auto& ch : chans) {
                    protocol::pack_u64(resp, static_cast<uint64_t>(ch.id));
                    protocol::pack_str(resp, ch.name);
                }
                queue_frame(c, protocol::PacketType::CHANNEL_LIST, resp);
            }
            break;
        }
        case protocol::PacketType::REACT_MESSAGE: {
            size_t off = 0;
            std::span<const uint8_t> pp{payload};
            uint64_t msg_id = 0;
            std::string emoji;
            protocol::unpack_u64(pp, off, msg_id);
            protocol::unpack_str(pp, off, emoji);
            
            if (g_db) {
                bool added = g_db->reactions().toggle_reaction(MessageId{msg_id}, AgentId{c.agent_id}, emoji);
                std::vector<uint8_t> update;
                protocol::pack_u64(update, msg_id);
                protocol::pack_u64(update, c.agent_id);
                protocol::pack_str(update, emoji);
                update.push_back(added ? 1 : 0);
                
                // Broadcast to all connected agents for simplicity
                for (auto& [id, a] : g_agents) {
                    queue_frame(*a, protocol::PacketType::REACTION_UPDATE, update);
                }
            }
            break;
        }
        case protocol::PacketType::UPLOAD_PREKEY: {
            // Agent uploads its X25519 prekey for offline message encryption.
            // Payload: [4-byte blob length][32-byte X25519 pubkey]
            size_t off = 0;
            std::vector<uint8_t> prekey;
            if (!protocol::unpack_blob(payload, off, prekey) || prekey.size() != 32) {
                std::vector<uint8_t> err;
                protocol::pack_u16(err, static_cast<uint16_t>(protocol::ErrorCode::CRYPTO_ERROR));
                protocol::pack_str(err, "prekey must be exactly 32 bytes (X25519 public key)");
                queue_frame(c, protocol::PacketType::ERROR, err);
                break;
            }
            {
                std::lock_guard<std::mutex> lk(g_db->mutex());
                g_db->prekeys().store_prekey(c.agent_id, prekey);
            }
            // ACK with empty AUTH_OK-style blob
            std::vector<uint8_t> ack;
            protocol::pack_u16(ack, 0); // success code
            queue_frame(c, protocol::PacketType::ACK, ack);
            break;
        }
        case protocol::PacketType::GET_EXCHANGE_KEY: {
            // Requester asks for another agent's X25519 prekey.
            // Payload: [8-byte target agent_id]
            size_t off = 0;
            uint64_t target_id = 0;
            if (!protocol::unpack_u64(payload, off, target_id)) {
                std::vector<uint8_t> err;
                protocol::pack_u16(err, static_cast<uint16_t>(protocol::ErrorCode::UNKNOWN));
                protocol::pack_str(err, "invalid GET_EXCHANGE_KEY payload");
                queue_frame(c, protocol::PacketType::ERROR, err);
                break;
            }
            std::optional<std::vector<uint8_t>> prekey;
            {
                std::lock_guard<std::mutex> lk(g_db->mutex());
                prekey = g_db->prekeys().get_prekey(target_id);
            }
            if (!prekey) {
                std::vector<uint8_t> err;
                protocol::pack_u16(err, static_cast<uint16_t>(protocol::ErrorCode::AGENT_NOT_FOUND));
                protocol::pack_str(err, "no prekey registered for target agent");
                queue_frame(c, protocol::PacketType::ERROR, err);
                break;
            }
            // EXCHANGE_KEY payload: [8-byte agent_id][4-byte blob length][32-byte pubkey]
            std::vector<uint8_t> resp;
            protocol::pack_u64(resp, target_id);
            protocol::pack_blob(resp, *prekey);
            queue_frame(c, protocol::PacketType::EXCHANGE_KEY, resp);
            break;
        }
        case protocol::PacketType::GROUP_MSG: {
            // Megolm-style group message: client sends encrypted blob for a channel.
            // Payload: [ch_id: u64][session_id: 32 bytes][sender_index: u32]
            //          [nonce: 12 bytes][ciphertext blob: u32+bytes]
            // Server forwards as-is to all other channel members (opaque relay —
            // server never sees plaintext).
            size_t off = 0;
            std::span<const uint8_t> pp{payload};
            uint64_t ch_raw = 0;
            if (!protocol::unpack_u64(pp, off, ch_raw)) {
                queue_error(c, protocol::ErrorCode::UNKNOWN, "invalid GROUP_MSG");
                break;
            }
            if (!g_db) break;
            auto ch_opt = g_db->channels().get_channel(ChannelId{ch_raw});
            if (!ch_opt) {
                queue_error(c, protocol::ErrorCode::CHANNEL_NOT_FOUND, "channel not found");
                break;
            }
            // Build forward packet: [from: u64][ch: u64][rest of payload unchanged]
            std::vector<uint8_t> fwd;
            protocol::pack_u64(fwd, c.agent_id);
            fwd.insert(fwd.end(), payload.begin(), payload.end());
            for (auto member_id : ch_opt->members) {
                uint64_t mid = static_cast<uint64_t>(member_id);
                if (mid != c.agent_id && g_agents.count(mid)) {
                    queue_frame(*g_agents[mid], protocol::PacketType::GROUP_MSG, fwd);
                }
            }
            // ACK to sender
            std::vector<uint8_t> ack;
            protocol::pack_u64(ack, ch_raw);
            queue_frame(c, protocol::PacketType::ACK, ack);
            break;
        }
        case protocol::PacketType::GROUP_KEY_DIST: {
            // Sender distributes its group session key to a specific member.
            // Payload: [target_agent_id: u64][ch_id: u64][encrypted_key_blob: u32+bytes]
            // Server relays the blob to the target agent (opaque — blob is
            // Double-Ratchet encrypted by sender for target).
            size_t off = 0;
            std::span<const uint8_t> pp{payload};
            uint64_t target_id = 0, ch_raw = 0;
            std::vector<uint8_t> blob;
            if (!protocol::unpack_u64(pp, off, target_id) ||
                !protocol::unpack_u64(pp, off, ch_raw)   ||
                !protocol::unpack_blob(pp, off, blob)) {
                queue_error(c, protocol::ErrorCode::UNKNOWN, "invalid GROUP_KEY_DIST");
                break;
            }
            if (!g_agents.count(target_id)) {
                queue_error(c, protocol::ErrorCode::AGENT_NOT_FOUND, "target agent not connected");
                break;
            }
            // Forward: [from: u64][ch: u64][blob: u32+bytes]
            std::vector<uint8_t> fwd;
            protocol::pack_u64(fwd, c.agent_id);
            protocol::pack_u64(fwd, ch_raw);
            protocol::pack_blob(fwd, blob);
            queue_frame(*g_agents[target_id], protocol::PacketType::GROUP_KEY_DIST, fwd);
            break;
        }
        case protocol::PacketType::GROUP_KEY_REQUEST: {
            // A member requests the group session key from the channel creator / any keyholder.
            // Payload: [ch_id: u64]
            // Server broadcasts the request to all other channel members so any
            // keyholder can respond with GROUP_KEY_DIST.
            size_t off = 0;
            std::span<const uint8_t> pp{payload};
            uint64_t ch_raw = 0;
            if (!protocol::unpack_u64(pp, off, ch_raw)) {
                queue_error(c, protocol::ErrorCode::UNKNOWN, "invalid GROUP_KEY_REQUEST");
                break;
            }
            if (!g_db) break;
            auto ch_opt = g_db->channels().get_channel(ChannelId{ch_raw});
            if (!ch_opt) {
                queue_error(c, protocol::ErrorCode::CHANNEL_NOT_FOUND, "channel not found");
                break;
            }
            // Forward request: [requester: u64][ch: u64]
            std::vector<uint8_t> fwd;
            protocol::pack_u64(fwd, c.agent_id);
            protocol::pack_u64(fwd, ch_raw);
            for (auto member_id : ch_opt->members) {
                uint64_t mid = static_cast<uint64_t>(member_id);
                if (mid != c.agent_id && g_agents.count(mid)) {
                    queue_frame(*g_agents[mid], protocol::PacketType::GROUP_KEY_REQUEST, fwd);
                }
            }
            break;
        }
        default:
            break;
    }
    return true;
}

int main(int argc, char* argv[]) {
    uint16_t port = 7777;
    uint16_t ws_port = 8766;
    uint16_t rest_port = 8767;
    std::string db_path = "agentchat.db";
    int rate_limit = 60;
    
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--port" && i + 1 < argc)
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (std::string(argv[i]) == "--ws-port" && i + 1 < argc)
            ws_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (std::string(argv[i]) == "--rest-port" && i + 1 < argc)
            rest_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (std::string(argv[i]) == "--db" && i + 1 < argc)
            db_path = argv[++i];
        else if (std::string(argv[i]) == "--rate-limit" && i + 1 < argc)
            rate_limit = std::stoi(argv[++i]);
    }

    g_db = std::make_unique<storage::Database>(db_path);
    g_rate_limiter = std::make_unique<server::RateLimiter>(rate_limit, 60);

    ::signal(SIGPIPE, SIG_IGN);
    ::signal(SIGINT,  [](int) { g_running = false; });
    ::signal(SIGTERM, [](int) { g_running = false; });

    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { std::cerr << "[server] socket() failed\n"; return 1; }
    int opt = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);
    if (::bind(server_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[server] bind() failed on port " << port << "\n";
        ::close(server_fd); return 1;
    }
    if (::listen(server_fd, 128) < 0 || !set_nonblocking(server_fd)) {
        std::cerr << "[server] listen/fcntl failed\n";
        ::close(server_fd); return 1;
    }

    std::cout << "[server] AgentChat async server (poll) on port " << port << "\n";

    // ── WebSocket server socket ───────────────────────────────────────────────
    int ws_server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (ws_server_fd < 0) { std::cerr << "[server] ws socket() failed\n"; return 1; }
    ::setsockopt(ws_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in ws_addr{};
    ws_addr.sin_family      = AF_INET;
    ws_addr.sin_addr.s_addr = INADDR_ANY;
    ws_addr.sin_port        = htons(ws_port);
    if (::bind(ws_server_fd, reinterpret_cast<struct sockaddr*>(&ws_addr), sizeof(ws_addr)) < 0) {
        std::cerr << "[server] bind() failed on ws-port " << ws_port << "\n";
        ::close(ws_server_fd); return 1;
    }
    if (::listen(ws_server_fd, 128) < 0 || !set_nonblocking(ws_server_fd)) {
        std::cerr << "[server] ws listen/fcntl failed\n";
        ::close(ws_server_fd); return 1;
    }
    std::cout << "[server] WebSocket server on port " << ws_port << "\n";

    // Start mDNS advertiser for zero-config LAN discovery
    std::string hostname = "agentchat";
    {
        char h[256];
        if (::gethostname(h, sizeof(h)) == 0) hostname = h;
        // strip .local suffix if present
        auto dot = hostname.find('.');
        if (dot != std::string::npos) hostname = hostname.substr(0, dot);
    }
    agentchat::mdns::Advertiser mdns_adv(hostname, port);
    mdns_adv.start();

    std::map<int, std::unique_ptr<ClientConn>> clients;
    size_t total_connections_accepted = 0;  // Task 3: total connection counter

    // Task 3: log max-connections config (default 1024)
    std::cout << "[server] max-connections: 1024\n";

    // ── REST API server (port 8767) ───────────────────────────────────────────
    rest::RestCallbacks rest_cb;
    rest_cb.list_agents = []() {
        std::vector<rest::AgentInfo> out;
        for (auto& [id, conn] : g_agents) {
            if (conn && conn->authenticated()) {
                rest::AgentInfo a;
                a.id   = id;
                a.name = conn->name;
                a.capabilities = "[]";
                out.push_back(a);
            }
        }
        return out;
    };
    rest_cb.get_agent = [](uint64_t id) -> std::optional<rest::AgentInfo> {
        auto it = g_agents.find(id);
        if (it == g_agents.end() || !it->second) return std::nullopt;
        rest::AgentInfo a;
        a.id   = id;
        a.name = it->second->name;
        a.capabilities = "[]";
        return a;
    };
    rest_cb.list_channels = []() {
        std::vector<rest::ChannelInfo> out;
        if (g_db) {
            // Return empty for now — channel listing from DB in v0.2
        }
        return out;
    };
    rest_cb.create_channel = [](const std::string& name, int type) -> uint64_t {
        static std::atomic<uint64_t> ch_id{1000};
        (void)name; (void)type;
        return ch_id.fetch_add(1);
    };
    rest_cb.set_trust = [](uint64_t agent_id, const std::string& level) -> bool {
        auto it = g_agents.find(agent_id);
        if (it == g_agents.end()) return false;
        TrustLevel tl = TrustLevel::UNKNOWN;
        if (level == "trusted")  tl = TrustLevel::TRUSTED;
        else if (level == "verified") tl = TrustLevel::VERIFIED;
        else if (level == "blocked")  tl = TrustLevel::BLOCKED;
        g_agent_trust[agent_id] = tl;
        std::cout << "[rest] Agent " << agent_id << " trust set to " << level << "\n";
        return true;
    };
    rest_cb.send_message = [](uint64_t from, uint64_t to_agent, uint64_t to_ch,
                               const std::string& text) -> uint64_t {
        // Route via existing handle_send_message logic
        static std::atomic<uint64_t> mid{9000};
        auto it = g_agents.find(to_agent);
        if (it != g_agents.end() && it->second) {
            std::vector<uint8_t> payload;
            protocol::pack_u64(payload, to_agent);
            protocol::pack_u64(payload, to_ch);
            payload.push_back(static_cast<uint8_t>(MessageType::TEXT));
            std::vector<uint8_t> pt(text.begin(), text.end());
            protocol::pack_blob(payload, std::span<const uint8_t>{pt});
            std::vector<uint8_t> empty;
            protocol::pack_blob(payload, std::span<const uint8_t>{empty});
            // Note: REST messages are not signed (no identity keypair available here)
            // Full signing support in v0.2 via REST auth token
            (void)from;
        }
        return mid.fetch_add(1);
    };

    rest::RestServer rest_srv(rest_port, std::move(rest_cb));
    rest_srv.start();
    std::cout << "[server] REST API on port " << rest_port << " (OpenAPI: http://localhost:"
              << rest_port << "/openapi.json)\n";

    // Task 1: periodic offline purge timer
    auto last_offline_purge = std::chrono::steady_clock::now();

    while (g_running) {
        // Task 1: purge expired offline messages every 60s
        {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_offline_purge).count() >= 60) {
                if (g_db) g_db->offline().purge_expired_offline();
                last_offline_purge = now;
                // Prune anti-replay window (remove entries older than 60s)
                auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                for (auto it = g_seen_msg_ids.begin(); it != g_seen_msg_ids.end(); ) {
                    if (now_ms - it->second > REPLAY_WINDOW_MS)
                        it = g_seen_msg_ids.erase(it);
                    else ++it;
                }
            }
        }
        std::vector<struct pollfd> pfds;
        pfds.reserve(2 + clients.size() + g_ws_clients.size());
        pfds.push_back({server_fd,    POLLIN, 0});
        pfds.push_back({ws_server_fd, POLLIN, 0});
        for (auto& [fd, conn] : clients) {
            short events = POLLIN;
            if (!conn->send_buf.empty()) events |= POLLOUT;
            pfds.push_back({fd, events, 0});
        }
        for (auto& [fd, wc] : g_ws_clients) {
            short events = POLLIN;
            if (!wc->send_buf.empty()) events |= POLLOUT;
            pfds.push_back({fd, events, 0});
        }

        int nready = ::poll(pfds.data(), (nfds_t)pfds.size(), 100);
        if (nready < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[server] poll() error\n"; break;
        }

        std::vector<int> to_close;

        for (size_t i = 0; i < pfds.size(); ++i) {
            auto& pfd = pfds[i];
            if (pfd.revents == 0) continue;

            if (pfd.fd == server_fd) {
                struct sockaddr_in ca{};
                socklen_t cal = sizeof(ca);
                int cfd = ::accept(server_fd,
                                   reinterpret_cast<struct sockaddr*>(&ca), &cal);
                if (cfd < 0) continue;
                if (!set_nonblocking(cfd)) { ::close(cfd); continue; }
                char peer[INET_ADDRSTRLEN];
                ::inet_ntop(AF_INET, &ca.sin_addr, peer, sizeof(peer));
                auto conn = std::make_unique<ClientConn>();
                conn->fd = cfd;
                conn->peer_addr = std::string(peer) + ":" +
                                  std::to_string(ntohs(ca.sin_port));
                std::cout << "[server] New connection from "
                          << conn->peer_addr << "\n";
                clients[cfd] = std::move(conn);
                continue;
            }

            // ── WS server: accept new WebSocket connection ──────────────
            if (pfd.fd == ws_server_fd) {
                struct sockaddr_in wca{};
                socklen_t wcal = sizeof(wca);
                int wfd = ::accept(ws_server_fd,
                                   reinterpret_cast<struct sockaddr*>(&wca), &wcal);
                if (wfd >= 0 && set_nonblocking(wfd)) {
                    char wpeer[INET_ADDRSTRLEN];
                    ::inet_ntop(AF_INET, &wca.sin_addr, wpeer, sizeof(wpeer));
                    auto wc = std::make_unique<WsConn>();
                    wc->fd        = wfd;
                    wc->peer_addr = std::string(wpeer) + ":" +
                                    std::to_string(ntohs(wca.sin_port));
                    std::cout << "[ws] New connection from " << wc->peer_addr << "\n";
                    g_ws_clients[wfd] = std::move(wc);
                } else if (wfd >= 0) {
                    ::close(wfd);
                }
                continue;
            }

            // ── WS client: read / write ──────────────────────────────────────
            {
                auto wit = g_ws_clients.find(pfd.fd);
                if (wit != g_ws_clients.end()) {
                    WsConn& wc = *wit->second;
                    bool ws_dead = false;

                    if (pfd.revents & POLLOUT) {
                        if (!wc.flush()) ws_dead = true;
                    }

                    if (!ws_dead && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
                        uint8_t tmp[4096];
                        ssize_t n = ::recv(pfd.fd, tmp, sizeof(tmp), 0);
                        if (n <= 0) {
                            ws_dead = true;
                        } else {
                            wc.recv_buf.insert(wc.recv_buf.end(), tmp, tmp + n);

                            if (wc.state == WsConn::State::HTTP_UPGRADE) {
                                if (ws_handshake::is_complete(wc.recv_buf)) {
                                    if (!ws_handshake::upgrade(wc)) ws_dead = true;
                                }
                            } else if (wc.state == WsConn::State::OPEN) {
                                while (!ws_dead) {
                                    ws_frame::Frame frm;
                                    int consumed = ws_frame::decode(wc.recv_buf, frm);
                                    if (consumed == 0) break;
                                    if (consumed < 0) { ws_dead = true; break; }
                                    wc.recv_buf.erase(wc.recv_buf.begin(),
                                                      wc.recv_buf.begin() + consumed);
                                    if (frm.opcode == ws_frame::Opcode::CLOSE) {
                                        wc.queue_close();
                                        wc.flush();
                                        ws_dead = true;
                                    } else if (frm.opcode == ws_frame::Opcode::PING) {
                                        auto pong = ws_frame::encode_pong(frm.payload);
                                        wc.send_buf.insert(wc.send_buf.end(),
                                                           pong.begin(), pong.end());
                                        if (!wc.flush()) ws_dead = true;
                                    } else if (frm.opcode == ws_frame::Opcode::TEXT) {
                                        std::string msg(frm.payload.begin(),
                                                        frm.payload.end());
                                        handle_ws_message(wc, msg);
                                        if (!wc.flush()) ws_dead = true;
                                    }
                                }
                            }
                        }
                    }

                    if (ws_dead) to_close.push_back(pfd.fd);
                    continue;
                }
            }

            auto it = clients.find(pfd.fd);
            if (it == clients.end()) continue;
            ClientConn& conn = *it->second;

            if (pfd.revents & POLLOUT) {
                if (!flush_send(conn)) {
                    to_close.push_back(pfd.fd);
                    continue;
                }
            }

            if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
                uint8_t tmp[4096];
                ssize_t n = ::recv(pfd.fd, tmp, sizeof(tmp), 0);
                if (n <= 0) {
                    to_close.push_back(pfd.fd);
                    continue;
                }
                conn.recv_buf.insert(conn.recv_buf.end(), tmp, tmp + n);

                bool alive = true;
                while (alive) {
                    protocol::PacketType ptype;
                    std::vector<uint8_t> pload;
                    size_t consumed = 0;
                    if (!protocol::decode_frame(
                            std::span<const uint8_t>{conn.recv_buf},
                            ptype, pload, consumed)) break;
                    conn.recv_buf.erase(conn.recv_buf.begin(),
                                       conn.recv_buf.begin() + (ptrdiff_t)consumed);
                    if (!dispatch_packet(conn, ptype, pload)) {
                        flush_send(conn);
                        to_close.push_back(pfd.fd);
                        alive = false;
                    } else {
                        flush_send(conn);
                    }
                }
            }
        }

        // Fix 2 (C-02): close connections stuck in pre-auth state > 30 seconds
        {
            auto now = std::chrono::steady_clock::now();
            for (auto& [fd, conn] : clients) {
                if (conn->state == ClientState::AWAIT_HELLO ||
                    conn->state == ClientState::AWAIT_AUTH_RESPONSE) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        now - conn->connected_at).count();
                    if (elapsed > 30) {
                        std::cout << "[server] Pre-auth timeout: closing "
                                  << conn->peer_addr << " (state="
                                  << (conn->state == ClientState::AWAIT_HELLO
                                       ? "AWAIT_HELLO" : "AWAIT_AUTH_RESPONSE")
                                  << ", " << elapsed << "s)\n";
                        to_close.push_back(fd);
                    }
                }
            }
        }

        for (int fd : to_close) {
            // WS client?
            {
                auto wit = g_ws_clients.find(fd);
                if (wit != g_ws_clients.end()) {
                    WsConn& wc = *wit->second;
                    if (wc.registered)
                        std::cout << "[ws] Agent " << wc.agent_id
                                  << " (" << wc.agent_name << ") disconnected\n";
                    else
                        std::cout << "[ws] Client " << wc.peer_addr << " disconnected\n";
                    ::close(fd);
                    g_ws_clients.erase(wit);
                    ws_push_agent_list();
                    continue;
                }
            }
            // Binary-protocol client
            auto it = clients.find(fd);
            if (it == clients.end()) continue;
            ClientConn& conn = *it->second;
            if (conn.agent_id) {
                // Broadcast agent offline status to WS clients
                std::ostringstream status_ev;
                status_ev << "{\"type\":\"agent_status\""
                          << ",\"agent_id\":" << conn.agent_id
                          << ",\"online\":false"
                          << ",\"name\":" << (conn.name.empty() ? "\"\"" : '"' + conn.name + '"')
                          << "}";
                ws_broadcast(status_ev.str());
                g_agents.erase(conn.agent_id);
                g_agent_trust.erase(conn.agent_id);
                g_agent_capabilities.erase(conn.agent_id);
                std::cout << "[server] Agent " << conn.agent_id
                          << " disconnected (" << conn.peer_addr << ")\n";
            } else {
                std::cout << "[server] Unauthenticated client "
                          << conn.peer_addr << " disconnected\n";
            }
            ::close(fd);
            clients.erase(it);
        }
    }

    // ── Task 4: Graceful shutdown ─────────────────────────────────────────────
    // Stop accepting new connections (server_fd already excluded from poll loop).
    // Broadcast shutdown notice to all WS clients, flush buffers, close cleanly.
    std::cout << "[server] Graceful shutdown initiated...\n";

    // Broadcast shutdown JSON to all open WS connections
    ws_broadcast("{\"type\":\"shutdown\"}");
    for (auto& [fd, wc] : g_ws_clients) {
        wc->queue_close();
        wc->flush();  // best-effort flush
        ::close(fd);
    }
    g_ws_clients.clear();

    // Flush and close all binary-protocol clients
    for (auto& [fd, conn] : clients) {
        flush_send(*conn);  // flush pending send buffer
        ::close(fd);
    }
    clients.clear();
    g_agents.clear();

    mdns_adv.stop();
    ::close(server_fd);
    ::close(ws_server_fd);
    std::cout << "[server] Shutdown complete.\n";
    return 0;
}
