#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace agentchat {

// ── Timestamp ──────────────────────────────────────────────────────���──────────

using Timestamp = std::chrono::time_point<std::chrono::system_clock,
                                          std::chrono::milliseconds>;

inline Timestamp now_ms() {
    return std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now());
}

// ── Strong ID typedefs ───���────────────────────────────────────────────────────
//
//  Tagged struct so AgentId, MessageId, ChannelId are distinct types
//  at compile time and cannot be accidentally interchanged.

template <typename Tag>
struct StrongId {
    uint64_t value{0};

    StrongId() = default;
    explicit StrongId(uint64_t v) : value(v) {}

    bool operator==(const StrongId& o) const { return value == o.value; }
    bool operator!=(const StrongId& o) const { return value != o.value; }
    bool operator< (const StrongId& o) const { return value <  o.value; }

    explicit operator uint64_t() const { return value; }
    explicit operator bool()     const { return value != 0; }
};

struct AgentIdTag   {};
struct MessageIdTag {};
struct ChannelIdTag {};
struct RoomIdTag    {};

using AgentId   = StrongId<AgentIdTag>;
using MessageId = StrongId<MessageIdTag>;
using ChannelId = StrongId<ChannelIdTag>;
using RoomId    = StrongId<RoomIdTag>;

// ���─ Cryptographic key types ───────────────────────────────────────────────────

using PublicKey  = std::array<uint8_t, 32>;
using PrivateKey = std::array<uint8_t, 32>;
using Signature  = std::array<uint8_t, 64>;

// ── Enumerations ──���──────────────────────────────────────────���────────────────

enum class MessageType : uint8_t {
    TEXT           = 0x01,
    BINARY         = 0x02,
    SYSTEM         = 0x03,
    AGENT_COMMAND  = 0x04,
    AGENT_RESPONSE = 0x05,
    DELETE_MESSAGE = 0x06,  ///< Delete a message for all parties
    READ_RECEIPT   = 0x07,  ///< Mark message(s) as read
    SESSION_KICK   = 0x08,  ///< Kick a session/device
    SESSION_LIST   = 0x09,  ///< Request active sessions list
};

enum class DeliveryStatus : uint8_t {
    QUEUED    = 0x00,  ///< Queued locally, network unavailable
    SENT      = 0x01,  ///< Sent to server (single check)
    DELIVERED = 0x02,  ///< Delivered to recipient agent (double check)
    READ      = 0x03,  ///< Read by recipient (double blue check) — opt-in
    FAILED    = 0xFF,  ///< Send failed, can retry
};

enum class ChannelType : uint8_t {
    DM        = 0x01,  ///< Direct message between two agents
    GROUP     = 0x02,  ///< Multi-member group channel
    BROADCAST = 0x03,  ///< One-to-many; only owner can send
};

// ── Core structs ────────────────────────────────────────────���─────────────────

/**
 * @brief An encrypted message as stored and transmitted.
 *
 * `payload` is always AES-256-GCM ciphertext; plaintext is never stored.
 * `signature` covers: id || from || to || channel || timestamp || payload
 */
struct Message {
    MessageId             id;
    AgentId               from;
    AgentId               to;           ///< zero for channel messages
    ChannelId             channel;      ///< zero for DMs
    std::vector<uint8_t>  payload;      ///< encrypted ciphertext
    std::vector<uint8_t>  nonce;        ///< 12-byte GCM nonce
    Signature             signature;    ///< Ed25519 over canonical fields
    Timestamp             timestamp;
    MessageType           type{MessageType::TEXT};
    DeliveryStatus        status{DeliveryStatus::SENT};
};

/**
 * @brief A channel (group, DM, or broadcast room).
 */
struct Channel {
    ChannelId             id;
    std::string           name;
    ChannelType           type{ChannelType::GROUP};
    std::vector<AgentId>  members;
    AgentId               owner;
    Timestamp             created_at;

    // Access control
    std::vector<AgentId>  whitelist;  ///< empty = allow all members
    std::vector<AgentId>  blacklist;  ///< always blocked
};

/**
 * @brief Registered agent info (public portion stored server-side).
 */
struct AgentInfo {
    AgentId                    id;
    std::string                name;          ///< human-readable display name
    PublicKey                  public_key;    ///< X25519 + Ed25519 public key
    std::vector<std::string>   capabilities; ///< e.g. {"text", "code", "vision"}
    Timestamp                  registered_at;
    bool                       online{false};
};

/**
 * @brief A Room groups multiple channels under one namespace (Discord-like server).
 */
struct Room {
    RoomId                id;
    std::string           name;
    std::string           description;
    AgentId               owner;
    std::vector<ChannelId> channels;
    std::vector<AgentId>  members;
    Timestamp             created_at;
};

} // namespace agentchat
