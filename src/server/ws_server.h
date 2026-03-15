#pragma once
/**
 * @file src/server/ws_server.h
 * @brief Minimal WebSocket server layer (RFC 6455) for AgentChat.
 *
 * Provides JSON-over-WebSocket access so the React Web UI can connect
 * directly without needing the binary TCP protocol.
 *
 * JSON protocol (matches web/src/utils/protocol.ts):
 *
 * Client → Server:
 *   { "type": "register",        "agent_id": N, "name": ".." }
 *   { "type": "message",         "to": N, "text": "..", "reply_to"?: N }
 *   { "type": "list_agents" }
 *   { "type": "join_channel",    "channel": ".." }
 *   { "type": "leave_channel",   "channel": ".." }
 *   { "type": "channel_message", "channel": "..", "text": ".." }
 *   { "type": "ping" }
 *
 * Server → Client:
 *   { "type": "ack",          "id": N }
 *   { "type": "message",      "id": N, "from": N, "to": N, "text": ".." }
 *   { "type": "agent_list",   "agents": [{"id":N,"name":"..","online":true},...] }
 *   { "type": "channel_event","event":"join"|"leave", "channel":"..",
 *                               "agent_id":N, "members":[...] }
 *   { "type": "error",        "message": ".." }
 *   { "type": "pong" }
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/sha.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// ── tiny JSON helpers (no external deps) ────────────────────────────────────

namespace ws_json {

// Escape a string for JSON embedding
inline std::string esc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

// Minimal JSON object getter — returns raw value string for a key
// Only handles flat objects (no nested braces in values)
inline std::optional<std::string> get(const std::string& json, const std::string& key) {
    // search for "key":
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    pos += needle.size();
    // skip whitespace
    while (pos < json.size() && json[pos] == ' ') ++pos;
    if (pos >= json.size()) return std::nullopt;

    if (json[pos] == '"') {
        // string value
        ++pos;
        std::string val;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos;
                switch (json[pos]) {
                    case '"': val += '"'; break;
                    case '\\': val += '\\'; break;
                    case 'n':  val += '\n'; break;
                    case 'r':  val += '\r'; break;
                    case 't':  val += '\t'; break;
                    default:   val += json[pos]; break;
                }
            } else {
                val += json[pos];
            }
            ++pos;
        }
        return val;
    } else {
        // number / bool / null — read until delimiter
        size_t end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ' ')
            ++end;
        return json.substr(pos, end - pos);
    }
}

inline uint64_t get_u64(const std::string& json, const std::string& key, uint64_t def = 0) {
    auto v = get(json, key);
    if (!v) return def;
    try { return std::stoull(*v); } catch (...) { return def; }
}

} // namespace ws_json

// ── Base64 encode (for WebSocket handshake) ──────────────────────────────────

namespace ws_base64 {
static const char* B64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::string encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = (uint32_t)data[i] << 16;
        if (i + 1 < len) b |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) b |= (uint32_t)data[i + 2];
        out += B64[(b >> 18) & 0x3f];
        out += B64[(b >> 12) & 0x3f];
        out += (i + 1 < len) ? B64[(b >> 6) & 0x3f] : '=';
        out += (i + 2 < len) ? B64[(b >> 0) & 0x3f] : '=';
    }
    return out;
}
} // namespace ws_base64

// ── WebSocket frame codec ────────────────────────────────────────────────────

namespace ws_frame {

enum Opcode : uint8_t {
    CONTINUATION = 0x0,
    TEXT         = 0x1,
    BINARY       = 0x2,
    CLOSE        = 0x8,
    PING         = 0x9,
    PONG         = 0xA,
};

// Encode a server→client text frame (no masking — server never masks)
inline std::vector<uint8_t> encode_text(const std::string& payload) {
    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN + TEXT opcode
    size_t plen = payload.size();
    if (plen <= 125) {
        frame.push_back(static_cast<uint8_t>(plen));
    } else if (plen <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((plen >> 8) & 0xff));
        frame.push_back(static_cast<uint8_t>(plen & 0xff));
    } else {
        frame.push_back(127);
        for (int s = 56; s >= 0; s -= 8)
            frame.push_back(static_cast<uint8_t>((plen >> s) & 0xff));
    }
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

// Encode a CLOSE frame
inline std::vector<uint8_t> encode_close(uint16_t code = 1000) {
    std::vector<uint8_t> frame = {0x88, 0x02,
        static_cast<uint8_t>(code >> 8),
        static_cast<uint8_t>(code & 0xff)};
    return frame;
}

// Encode a PONG frame
inline std::vector<uint8_t> encode_pong(const std::vector<uint8_t>& data = {}) {
    std::vector<uint8_t> frame;
    frame.push_back(0x8A);
    frame.push_back(static_cast<uint8_t>(std::min(data.size(), size_t(125))));
    frame.insert(frame.end(), data.begin(),
                 data.begin() + static_cast<ptrdiff_t>(
                     std::min(data.size(), size_t(125))));
    return frame;
}

struct Frame {
    bool   fin{false};
    Opcode opcode{TEXT};
    std::vector<uint8_t> payload;
    bool   masked{false};
};

// Try to decode one frame from buf; returns bytes consumed (0 = need more data, -1 = error)
inline int decode(const std::vector<uint8_t>& buf, Frame& out) {
    if (buf.size() < 2) return 0;
    out.fin    = (buf[0] & 0x80) != 0;
    out.opcode = static_cast<Opcode>(buf[0] & 0x0f);
    out.masked = (buf[1] & 0x80) != 0;
    uint64_t plen = buf[1] & 0x7f;
    size_t hdr = 2;

    if (plen == 126) {
        if (buf.size() < 4) return 0;
        plen = ((uint64_t)buf[2] << 8) | buf[3];
        hdr = 4;
    } else if (plen == 127) {
        if (buf.size() < 10) return 0;
        plen = 0;
        for (int i = 0; i < 8; ++i) plen = (plen << 8) | buf[2 + i];
        hdr = 10;
    }

    if (plen > 1024 * 1024) return -1; // 1MB max frame — DoS prevention (C-01)

    size_t mask_off = hdr;
    if (out.masked) hdr += 4;
    if (buf.size() < hdr + plen) return 0;

    out.payload.assign(buf.begin() + static_cast<ptrdiff_t>(hdr),
                       buf.begin() + static_cast<ptrdiff_t>(hdr + plen));
    if (out.masked) {
        const uint8_t* mk = buf.data() + mask_off;
        for (size_t i = 0; i < out.payload.size(); ++i)
            out.payload[i] ^= mk[i & 3];
    }
    return static_cast<int>(hdr + plen);
}

} // namespace ws_frame

// ── WebSocket connection state ───────────────────────────────────────────────

struct WsConn {
    int         fd{-1};
    std::string peer_addr;

    enum class State { HTTP_UPGRADE, OPEN, CLOSING, CLOSED } state{State::HTTP_UPGRADE};

    std::vector<uint8_t> recv_buf;
    std::vector<uint8_t> send_buf;

    // Agent identity (set after 'register')
    uint64_t    agent_id{0};
    std::string agent_name;
    bool        registered{false};

    // channel memberships
    std::vector<std::string> channels;

    bool is_open() const { return state == State::OPEN; }

    void queue_text(const std::string& json) {
        auto frame = ws_frame::encode_text(json);
        send_buf.insert(send_buf.end(), frame.begin(), frame.end());
    }

    void queue_close() {
        auto frame = ws_frame::encode_close();
        send_buf.insert(send_buf.end(), frame.begin(), frame.end());
        state = State::CLOSING;
    }

    bool flush() {
        while (!send_buf.empty()) {
            ssize_t n = ::send(fd, send_buf.data(), send_buf.size(), MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
                return false;
            }
            send_buf.erase(send_buf.begin(), send_buf.begin() + n);
        }
        return true;
    }
};

// ── WebSocket HTTP upgrade handshake ────────────────────────────────────────

namespace ws_handshake {

inline std::optional<std::string> extract_key(const std::string& http) {
    // find Sec-WebSocket-Key header (case-insensitive search for the header name)
    std::string lower = http;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto pos = lower.find("sec-websocket-key:");
    if (pos == std::string::npos) return std::nullopt;
    pos += 18; // len("sec-websocket-key:")
    while (pos < http.size() && http[pos] == ' ') ++pos;
    size_t end = http.find('\r', pos);
    if (end == std::string::npos) end = http.find('\n', pos);
    if (end == std::string::npos) return std::nullopt;
    return http.substr(pos, end - pos);
}

inline std::string accept_key(const std::string& client_key) {
    static const char* MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = client_key + MAGIC;
    uint8_t sha1[20];
    SHA1(reinterpret_cast<const uint8_t*>(combined.data()), combined.size(), sha1);
    return ws_base64::encode(sha1, 20);
}

// Returns true if the buffer contains a complete HTTP request (ends with \r\n\r\n)
inline bool is_complete(const std::vector<uint8_t>& buf) {
    if (buf.size() < 4) return false;
    for (size_t i = 0; i + 3 < buf.size(); ++i) {
        if (buf[i]=='\r' && buf[i+1]=='\n' && buf[i+2]=='\r' && buf[i+3]=='\n')
            return true;
    }
    return false;
}

// Perform the HTTP→WebSocket upgrade.  Returns false if request is invalid.
inline bool upgrade(WsConn& c) {
    std::string req(c.recv_buf.begin(), c.recv_buf.end());
    c.recv_buf.clear();

    auto key = extract_key(req);
    if (!key) {
        // Not a valid WS upgrade — send 400
        const char* r400 = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        ::send(c.fd, r400, strlen(r400), MSG_NOSIGNAL);
        return false;
    }

    std::string accept = accept_key(*key);
    std::ostringstream resp;
    resp << "HTTP/1.1 101 Switching Protocols\r\n"
         << "Upgrade: websocket\r\n"
         << "Connection: Upgrade\r\n"
         << "Sec-WebSocket-Accept: " << accept << "\r\n"
         << "\r\n";
    std::string rs = resp.str();
    ::send(c.fd, rs.data(), rs.size(), MSG_NOSIGNAL);

    c.state = WsConn::State::OPEN;
    return true;
}

} // namespace ws_handshake

