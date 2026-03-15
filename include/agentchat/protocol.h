#pragma once

/**
 * @file protocol.h
 * @brief AgentChat wire protocol definitions
 *
 * Frame layout:
 *   [4 bytes big-endian payload length][1 byte PacketType][payload bytes]
 *
 * All multi-byte integers are big-endian.
 * Strings are length-prefixed: [2 bytes length][UTF-8 bytes]
 * Binary blobs are length-prefixed: [4 bytes length][bytes]
 */

#include <agentchat/types.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace agentchat::protocol {

// ── Frame header constants ───────────────────────────────────────────���────────

inline constexpr size_t   FRAME_LENGTH_SIZE = 4;  ///< bytes for payload length
inline constexpr size_t   FRAME_TYPE_SIZE   = 1;  ///< bytes for packet type
inline constexpr size_t   FRAME_HEADER_SIZE = FRAME_LENGTH_SIZE + FRAME_TYPE_SIZE;
inline constexpr uint32_t MAX_PAYLOAD_SIZE  = 64 * 1024 * 1024; ///< 64 MiB hard limit

// ─��� Packet types ─────────────────────────────────────���───────────────────────

enum class PacketType : uint8_t {
    // Handshake
    HELLO           = 0x01,  ///< Client → Server: announce version + ephemeral pubkey
    HELLO_ACK       = 0x02,  ///< Server → Client: server ephemeral pubkey
    AUTH            = 0x03,  ///< Client → Server: agent_id + Ed25519 challenge signature
    AUTH_OK         = 0x04,  ///< Server → Client: session token
    AUTH_FAIL       = 0x05,  ///< Server → Client: error code + message

    // Agent registry
    REGISTER_AGENT  = 0x10,  ///< Client → Server: register / update agent info
    REGISTER_ACK    = 0x11,  ///< Server → Client: assigned AgentId
    LIST_AGENTS     = 0x12,  ///< Client → Server: query online / all agents
    AGENT_LIST      = 0x13,  ///< Server → Client: list of AgentInfo

    // Messaging
    SEND_MESSAGE    = 0x20,  ///< Client → Server: encrypted Message
    RECV_MESSAGE    = 0x21,  ///< Server → Client: encrypted Message
    ACK             = 0x22,  ///< Bidirectional: delivery acknowledgement
    READ_RECEIPT    = 0x23,  ///< Client → Server: mark message read

    // Channels
    CREATE_CHANNEL  = 0x30,  ///< Client → Server: create DM / group / broadcast
    CHANNEL_CREATED = 0x31,  ///< Server → Client: Channel info
    JOIN_CHANNEL    = 0x32,  ///< Client → Server
    LEAVE_CHANNEL   = 0x33,  ///< Client → Server
    CHANNEL_EVENT   = 0x34,  ///< Server → Client: member join/leave/update
    SEND_TO_CHANNEL = 0x35,  ///< Client → Server: send message to channel
    CHANNEL_MSG     = 0x36,  ///< Server → Client: channel message broadcast
    LIST_CHANNELS   = 0x37,  ///< Client → Server: list joined channels
    CHANNEL_LIST    = 0x38,  ///< Server → Client: list of channels

    // Reactions
    REACT_MESSAGE   = 0x40,  ///< Client → Server: add/toggle reaction
    REACTION_UPDATE = 0x41,  ///< Server → Client: reaction state update

    // Auth challenge (Ed25519 challenge-response)
    AUTH_CHALLENGE  = 0x06,  ///< Server → Client: 32-byte random challenge
    AUTH_RESPONSE   = 0x07,  ///< Client → Server: Ed25519 signature of challenge

    // Key exchange
    GET_EXCHANGE_KEY = 0x50,  ///< Client → Server: request another agent's X25519 pubkey
    EXCHANGE_KEY     = 0x51,  ///< Server → Client: respond with the pubkey
    UPLOAD_PREKEY    = 0x52,  ///< Client → Server: upload X25519 prekey bundle for offline use

    // Group channel E2EE (Megolm-style)
    GROUP_KEY_DIST    = 0x60,  ///< Server → Client: distribute group session key (encrypted per-member)
    GROUP_MSG         = 0x61,  ///< Client → Server / Server → Client: group-encrypted channel message
    GROUP_KEY_REQUEST = 0x62,  ///< Client → Server: request current group key for a channel

    // Keepalive
    PING            = 0xF0,
    PONG            = 0xF1,

    // Errors
    ERROR           = 0xFF,
};

// ── Handshake payloads ────���──────────────────────────────────────────���────────

inline constexpr uint16_t PROTOCOL_VERSION = 1;

struct HelloPacket {
    uint16_t  version;         ///< PROTOCOL_VERSION
    PublicKey ephemeral_pubkey; ///< X25519 ephemeral key for this session
    std::string client_info;   ///< optional "AgentChat-cpp/0.1"
};

struct HelloAckPacket {
    PublicKey ephemeral_pubkey; ///< Server's X25519 ephemeral key
    std::vector<uint8_t> challenge; ///< 32-byte random challenge for AUTH
};

struct AuthPacket {
    AgentId   agent_id;
    Signature challenge_sig; ///< Ed25519 sign(challenge) with agent's identity key
};

struct AuthOkPacket {
    std::vector<uint8_t> session_token; ///< opaque 32-byte token for reconnect
};

// ── Message payloads ────────────────────────────���─────────────────────────────

struct SendMessagePacket {
    AgentId   to_agent;   ///< zero if channel message
    ChannelId to_channel; ///< zero if DM
    MessageType type;
    std::vector<uint8_t> encrypted_payload; ///< AES-256-GCM ciphertext
    std::vector<uint8_t> nonce;             ///< 12-byte GCM nonce
    Signature   signature;                  ///< Ed25519 over canonical fields
};

struct RecvMessagePacket {
    Message msg;
};

struct AckPacket {
    MessageId message_id;
    DeliveryStatus status;
};

// ── Channel payloads ──────────────────────────────────────────────────────────

struct CreateChannelPacket {
    std::string            name;
    ChannelType            type;
    std::vector<AgentId>   initial_members;
};

// ── Error payload ──────────────────────────────────────���──────────────────────

enum class ErrorCode : uint16_t {
    UNKNOWN          = 0x0000,
    AUTH_FAILED      = 0x0001,
    NOT_AUTHORIZED   = 0x0002,
    AGENT_NOT_FOUND  = 0x0003,
    CHANNEL_NOT_FOUND= 0x0004,
    BLACKLISTED      = 0x0005,
    RATE_LIMITED     = 0x0006,
    PAYLOAD_TOO_LARGE= 0x0007,
    CRYPTO_ERROR     = 0x0008,
};

struct ErrorPacket {
    ErrorCode   code;
    std::string message;
};

// ── Frame encode / decode ───────────────────────────────────────────���─────────

/**
 * @brief Encode a raw payload into a complete wire frame.
 *
 *  frame = [4-byte BE length][1-byte type][payload]
 *
 * @param type     PacketType byte.
 * @param payload  Already-serialised payload bytes.
 * @return Complete frame bytes ready to write to a socket.
 */
std::vector<uint8_t> encode_frame(
    PacketType               type,
    std::span<const uint8_t> payload);

/**
 * @brief Try to decode one frame from a stream buffer.
 *
 * @param buf     Incoming byte buffer (may contain partial frame).
 * @param out_type   Populated with PacketType if a complete frame is present.
 * @param out_payload Populated with payload bytes.
 * @param consumed   Set to number of bytes consumed from buf.
 * @return true if a complete frame was decoded.
 */
bool decode_frame(
    std::span<const uint8_t> buf,
    PacketType&              out_type,
    std::vector<uint8_t>&    out_payload,
    size_t&                  consumed);

// ── Serialisation helpers ──────────────────────���──────────────────────────────

/// Append a big-endian uint16 to a buffer.
void pack_u16(std::vector<uint8_t>& buf, uint16_t v);
/// Append a big-endian uint32 to a buffer.
void pack_u32(std::vector<uint8_t>& buf, uint32_t v);
/// Append a big-endian uint64 to a buffer.
void pack_u64(std::vector<uint8_t>& buf, uint64_t v);
/// Append a length-prefixed string (2-byte length).
void pack_str(std::vector<uint8_t>& buf, const std::string& s);
/// Append a length-prefixed blob (4-byte length).
void pack_blob(std::vector<uint8_t>& buf, std::span<const uint8_t> data);

/// Read big-endian uint16 from offset; advance offset.
bool unpack_u16(std::span<const uint8_t> buf, size_t& off, uint16_t& out);
bool unpack_u32(std::span<const uint8_t> buf, size_t& off, uint32_t& out);
bool unpack_u64(std::span<const uint8_t> buf, size_t& off, uint64_t& out);
bool unpack_str(std::span<const uint8_t> buf, size_t& off, std::string& out);
bool unpack_blob(std::span<const uint8_t> buf, size_t& off, std::vector<uint8_t>& out);

} // namespace agentchat::protocol
