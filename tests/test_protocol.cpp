/**
 * @file tests/test_protocol.cpp
 * @brief Unit tests for AgentChat wire protocol framing and serialisation.
 */

#include <agentchat/protocol.h>
#include <agentchat/types.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond) do { if (!(cond)) throw std::runtime_error( \
    "ASSERT failed: " #cond " (line " + std::to_string(__LINE__) + ")"); } while(0)

#define RUN(name) do { \
    std::cout << "  " #name " ... " << std::flush; \
    try { test_##name(); std::cout << "PASS\n"; ++g_pass; } \
    catch (const std::exception& e) { std::cout << "FAIL: " << e.what() << "\n"; ++g_fail; } \
} while(0)

using namespace agentchat::protocol;

static void test_pack_unpack_u16() {
    std::vector<uint8_t> buf;
    pack_u16(buf, 0xBEEF);
    ASSERT(buf.size() == 2);
    ASSERT(buf[0] == 0xBE && buf[1] == 0xEF);
    size_t off = 0;
    uint16_t v = 0;
    ASSERT(unpack_u16(std::span<const uint8_t>{buf}, off, v));
    ASSERT(v == 0xBEEF && off == 2);
}

static void test_pack_unpack_u32() {
    std::vector<uint8_t> buf;
    pack_u32(buf, 0xDEADBEEF);
    ASSERT(buf.size() == 4);
    size_t off = 0;
    uint32_t v = 0;
    ASSERT(unpack_u32(std::span<const uint8_t>{buf}, off, v));
    ASSERT(v == 0xDEADBEEF);
}

static void test_pack_unpack_u64() {
    std::vector<uint8_t> buf;
    pack_u64(buf, 0xCAFEBABEDEAD1234ULL);
    ASSERT(buf.size() == 8);
    size_t off = 0;
    uint64_t v = 0;
    ASSERT(unpack_u64(std::span<const uint8_t>{buf}, off, v));
    ASSERT(v == 0xCAFEBABEDEAD1234ULL);
}

static void test_pack_unpack_str() {
    std::vector<uint8_t> buf;
    pack_str(buf, "AgentChat");
    size_t off = 0;
    std::string out;
    ASSERT(unpack_str(std::span<const uint8_t>{buf}, off, out));
    ASSERT(out == "AgentChat");
}

static void test_pack_unpack_blob() {
    std::vector<uint8_t> buf;
    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0xFF};
    pack_blob(buf, std::span<const uint8_t>{data});
    size_t off = 0;
    std::vector<uint8_t> out;
    ASSERT(unpack_blob(std::span<const uint8_t>{buf}, off, out));
    ASSERT(out == data);
}

static void test_frame_encode_decode() {
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto frame = encode_frame(PacketType::SEND_MESSAGE,
                              std::span<const uint8_t>{payload});
    ASSERT(frame.size() == FRAME_HEADER_SIZE + payload.size());

    PacketType ptype;
    std::vector<uint8_t> decoded;
    size_t consumed = 0;
    ASSERT(decode_frame(std::span<const uint8_t>{frame}, ptype, decoded, consumed));
    ASSERT(ptype == PacketType::SEND_MESSAGE);
    ASSERT(decoded == payload);
    ASSERT(consumed == frame.size());
}

static void test_frame_partial() {
    std::vector<uint8_t> payload(100, 0xAB);
    auto frame = encode_frame(PacketType::PING, std::span<const uint8_t>{payload});

    // Feed only half — should return false (incomplete)
    std::vector<uint8_t> partial(frame.begin(), frame.begin() + frame.size() / 2);
    PacketType ptype;
    std::vector<uint8_t> decoded;
    size_t consumed = 0;
    ASSERT(!decode_frame(std::span<const uint8_t>{partial}, ptype, decoded, consumed));
    ASSERT(consumed == 0);
}

static void test_frame_multiple() {
    // Two frames concatenated in one buffer (as in a TCP stream)
    std::vector<uint8_t> p1 = {0x01};
    std::vector<uint8_t> p2 = {0x02, 0x03};
    auto f1 = encode_frame(PacketType::PING, std::span<const uint8_t>{p1});
    auto f2 = encode_frame(PacketType::PONG, std::span<const uint8_t>{p2});

    std::vector<uint8_t> stream;
    stream.insert(stream.end(), f1.begin(), f1.end());
    stream.insert(stream.end(), f2.begin(), f2.end());

    PacketType t1, t2;
    std::vector<uint8_t> d1, d2;
    size_t c1 = 0, c2 = 0;

    ASSERT(decode_frame(std::span<const uint8_t>{stream}, t1, d1, c1));
    ASSERT(t1 == PacketType::PING && d1 == p1);

    auto remaining = std::span<const uint8_t>{stream}.subspan(c1);
    ASSERT(decode_frame(remaining, t2, d2, c2));
    ASSERT(t2 == PacketType::PONG && d2 == p2);
}

static void test_frame_empty_payload() {
    std::vector<uint8_t> empty;
    auto frame = encode_frame(PacketType::PONG, std::span<const uint8_t>{empty});
    ASSERT(frame.size() == FRAME_HEADER_SIZE);

    PacketType ptype;
    std::vector<uint8_t> decoded;
    size_t consumed = 0;
    ASSERT(decode_frame(std::span<const uint8_t>{frame}, ptype, decoded, consumed));
    ASSERT(ptype == PacketType::PONG);
    ASSERT(decoded.empty());
}

static void test_multi_field_serialisation() {
    // Simulate serialising a SendMessagePacket manually
    std::vector<uint8_t> buf;
    uint64_t to_agent  = 42;
    uint64_t to_ch     = 0;
    uint8_t  msg_type  = static_cast<uint8_t>(agentchat::MessageType::TEXT);
    std::vector<uint8_t> ct = {0xCA, 0xFE, 0xBA, 0xBE};
    uint64_t msg_id    = 99;

    pack_u64(buf, to_agent);
    pack_u64(buf, to_ch);
    buf.push_back(msg_type);
    pack_blob(buf, std::span<const uint8_t>{ct});
    pack_u64(buf, msg_id);

    // Deserialise
    size_t off = 0;
    std::span<const uint8_t> s{buf};
    uint64_t ra = 0, rc = 0, rid = 0;
    ASSERT(unpack_u64(s, off, ra));  ASSERT(ra == to_agent);
    ASSERT(unpack_u64(s, off, rc));  ASSERT(rc == to_ch);
    ASSERT(buf[off++] == msg_type);
    std::vector<uint8_t> rct;
    ASSERT(unpack_blob(s, off, rct)); ASSERT(rct == ct);
    ASSERT(unpack_u64(s, off, rid));  ASSERT(rid == msg_id);
}

int main() {
    std::cout << "=== AgentChat Protocol Tests ===\n";
    RUN(pack_unpack_u16);
    RUN(pack_unpack_u32);
    RUN(pack_unpack_u64);
    RUN(pack_unpack_str);
    RUN(pack_unpack_blob);
    RUN(frame_encode_decode);
    RUN(frame_partial);
    RUN(frame_multiple);
    RUN(frame_empty_payload);
    RUN(multi_field_serialisation);
    std::cout << "\nResult: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail ? 1 : 0;
}
