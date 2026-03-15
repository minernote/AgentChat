#pragma once
/**
 * @file group_ratchet.h
 * @brief Megolm-style group channel encryption
 *
 * Each sender maintains an independent symmetric ratchet (chain key → message key).
 * The channel group session is identified by a session_id. Members receive the
 * sender's current chain key via an individual Double Ratchet–encrypted
 * GROUP_KEY_DIST message, then can decrypt all subsequent group messages from
 * that sender without per-message DH.
 *
 * Wire layout of a GroupMessage payload:
 *   [session_id: 32 bytes]
 *   [sender_index: u32]      — monotonic message counter for this sender
 *   [nonce: 12 bytes]
 *   [ciphertext blob: u32 len + bytes]  — AES-256-GCM over plaintext
 *   [mac: 16 bytes]          — GCM auth tag (appended by encrypt, verified on decrypt)
 *
 * Reference: https://gitlab.matrix.org/matrix-org/olm/-/blob/master/docs/megolm.md
 */

#include <agentchat/crypto.h>
#include <array>
#include <optional>
#include <vector>
#include <map>
#include <stdint.h>

namespace agentchat {
namespace crypto {

constexpr size_t GROUP_SESSION_ID_SIZE = 32;
constexpr size_t GROUP_CHAIN_KEY_SIZE  = 32;
constexpr size_t GROUP_MSG_KEY_SIZE    = 32;
constexpr uint32_t GROUP_MAX_SKIP      = 1000; ///< max out-of-order messages to buffer

using GroupSessionId = std::array<uint8_t, GROUP_SESSION_ID_SIZE>;
using GroupChainKey  = std::array<uint8_t, GROUP_CHAIN_KEY_SIZE>;
using GroupMsgKey    = std::array<uint8_t, GROUP_MSG_KEY_SIZE>;

/**
 * @brief Outbound group session — one per (channel, sender).
 *
 * The sender owns this and distributes the current chain_key to
 * new members via GROUP_KEY_DIST.
 */
struct OutboundGroupSession {
    GroupSessionId session_id{}; ///< Random 32-byte session identifier
    GroupChainKey  chain_key{};  ///< Current ratchet chain key
    uint32_t       index{0};     ///< Message counter (sender_index)
};

/**
 * @brief Inbound group session — one per (channel, sender_agent_id).
 *
 * Reconstructed from the chain_key + index received in GROUP_KEY_DIST.
 * Out-of-order messages can be handled by caching skipped message keys.
 */
struct InboundGroupSession {
    GroupSessionId session_id{};
    GroupChainKey  chain_key{};  ///< Chain key at known_index
    uint32_t       known_index{0}; ///< The index corresponding to chain_key
    /// Cache of pre-computed message keys for out-of-order delivery
    /// key = sender_index, value = message key
    std::map<uint32_t, GroupMsgKey> key_cache;
};

/**
 * @brief Encrypted group message (ready for wire serialisation).
 */
struct GroupMessage {
    GroupSessionId session_id{};
    uint32_t       sender_index{0};
    std::array<uint8_t, 12> nonce{};
    std::vector<uint8_t>    ciphertext; ///< includes 16-byte GCM tag
};

// ── Session lifecycle ─────────────────────────────────────────────────────────

/**
 * @brief Create a new outbound group session with a random session_id and
 *        chain_key.
 */
std::optional<OutboundGroupSession> group_session_create();

/**
 * @brief Serialise the current chain_key + index for distribution to a new
 *        member via individual E2EE (GROUP_KEY_DIST payload).
 *
 * Format: [session_id: 32][chain_key: 32][index: u32] = 68 bytes
 */
std::vector<uint8_t> group_session_export(const OutboundGroupSession& s);

/**
 * @brief Reconstruct an InboundGroupSession from a GROUP_KEY_DIST payload.
 */
std::optional<InboundGroupSession> group_session_import(
    const std::vector<uint8_t>& exported);

// ── KDF ───────────────────────────────────────────────────────────────────────

/**
 * @brief Advance the chain key one step, deriving a message key.
 *
 * chain_key_n+1 = HKDF(chain_key_n, info="AgentChat-group-chain", len=32)
 * message_key_n = HKDF(chain_key_n, info="AgentChat-group-msg",   len=32)
 */
bool group_kdf(const GroupChainKey& ck,
               GroupChainKey& out_ck,
               GroupMsgKey&   out_mk);

// ── Encrypt / Decrypt ─────────────────────────────────────────────────────────

/**
 * @brief Encrypt a plaintext for the group, advancing the outbound ratchet.
 *
 * @param session   Outbound session (chain_key and index are advanced in place)
 * @param plaintext Raw message bytes
 * @return GroupMessage ready for serialisation, or nullopt on crypto failure
 */
std::optional<GroupMessage> group_encrypt(
    OutboundGroupSession& session,
    const std::vector<uint8_t>& plaintext);

/**
 * @brief Decrypt a group message using the inbound session.
 *
 * Handles forward ratcheting: if sender_index > known_index, advances the
 * chain and caches skipped message keys (up to MAX_SKIP_MESSAGES).
 *
 * @param session  Inbound session for this sender
 * @param msg      Incoming GroupMessage
 * @return Decrypted plaintext, or nullopt on failure / auth error
 */
std::optional<std::vector<uint8_t>> group_decrypt(
    InboundGroupSession& session,
    const GroupMessage&  msg);

} // namespace crypto
} // namespace agentchat
