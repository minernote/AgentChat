#include <agentchat/protocol.h>

#include <arpa/inet.h>  // htonl / ntohl on POSIX
#include <cstring>
#include <stdexcept>

namespace agentchat::protocol {

// ── pack helpers ─────���────────────────────────────────────────────────────────

void pack_u16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v >> 8));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

void pack_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
    buf.push_back(static_cast<uint8_t>( v        & 0xFF));
}

void pack_u64(std::vector<uint8_t>& buf, uint64_t v) {
    for (int s = 56; s >= 0; s -= 8)
        buf.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
}

void pack_str(std::vector<uint8_t>& buf, const std::string& s) {
    auto len = static_cast<uint16_t>(s.size());
    pack_u16(buf, len);
    buf.insert(buf.end(),
               reinterpret_cast<const uint8_t*>(s.data()),
               reinterpret_cast<const uint8_t*>(s.data()) + s.size());
}

void pack_blob(std::vector<uint8_t>& buf, std::span<const uint8_t> data) {
    pack_u32(buf, static_cast<uint32_t>(data.size()));
    buf.insert(buf.end(), data.begin(), data.end());
}

// ── unpack helpers ───────────────────���────────────────────────────────────────

bool unpack_u16(std::span<const uint8_t> buf, size_t& off, uint16_t& out) {
    if (off + 2 > buf.size()) return false;
    out = static_cast<uint16_t>((buf[off] << 8) | buf[off + 1]);
    off += 2;
    return true;
}

bool unpack_u32(std::span<const uint8_t> buf, size_t& off, uint32_t& out) {
    if (off + 4 > buf.size()) return false;
    out = (static_cast<uint32_t>(buf[off    ]) << 24)
        | (static_cast<uint32_t>(buf[off + 1]) << 16)
        | (static_cast<uint32_t>(buf[off + 2]) <<  8)
        |  static_cast<uint32_t>(buf[off + 3]);
    off += 4;
    return true;
}

bool unpack_u64(std::span<const uint8_t> buf, size_t& off, uint64_t& out) {
    if (off + 8 > buf.size()) return false;
    out = 0;
    for (int i = 0; i < 8; ++i)
        out = (out << 8) | buf[off + i];
    off += 8;
    return true;
}

bool unpack_str(std::span<const uint8_t> buf, size_t& off, std::string& out) {
    uint16_t len = 0;
    if (!unpack_u16(buf, off, len)) return false;
    if (off + len > buf.size()) return false;
    out.assign(reinterpret_cast<const char*>(buf.data() + off), len);
    off += len;
    return true;
}

bool unpack_blob(std::span<const uint8_t> buf, size_t& off, std::vector<uint8_t>& out) {
    uint32_t len = 0;
    if (!unpack_u32(buf, off, len)) return false;
    if (off + len > buf.size()) return false;
    out.assign(buf.begin() + off, buf.begin() + off + len);
    off += len;
    return true;
}

// ── Frame encode ────────────────���─────────────────────────────────────────────

std::vector<uint8_t> encode_frame(
    PacketType               type,
    std::span<const uint8_t> payload)
{
    auto payload_len = static_cast<uint32_t>(payload.size());
    std::vector<uint8_t> frame;
    frame.reserve(FRAME_HEADER_SIZE + payload.size());
    // 4-byte big-endian length
    pack_u32(frame, payload_len);
    // 1-byte type
    frame.push_back(static_cast<uint8_t>(type));
    // payload
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

// ── Frame decode ───────────────���──────────────────────────────────────────────

bool decode_frame(
    std::span<const uint8_t> buf,
    PacketType&              out_type,
    std::vector<uint8_t>&    out_payload,
    size_t&                  consumed)
{
    consumed = 0;
    if (buf.size() < FRAME_HEADER_SIZE) return false;

    size_t off = 0;
    uint32_t payload_len = 0;
    if (!unpack_u32(buf, off, payload_len)) return false;

    if (payload_len > MAX_PAYLOAD_SIZE) return false;  // reject oversized frames

    out_type = static_cast<PacketType>(buf[off]);
    off += FRAME_TYPE_SIZE;

    if (off + payload_len > buf.size()) return false;  // incomplete frame

    out_payload.assign(buf.begin() + off, buf.begin() + off + payload_len);
    consumed = off + payload_len;
    return true;
}

} // namespace agentchat::protocol
