#pragma once
/**
 * @file ratchet.h
 * @brief Double Ratchet Algorithm implementation (Signal Protocol)
 *
 * Provides forward secrecy and break-in recovery for agent-to-agent E2EE.
 *
 * Protocol:
 *   1. Initial key agreement via X3DH (using prekey bundle)
 *   2. Diffie-Hellman ratchet: new DH key pair on each message chain
 *   3. Symmetric ratchet: KDF chain for individual message keys
 *
 * Reference: https://signal.org/docs/specifications/doubleratchet/
 */

#include <agentchat/crypto.h>
#include <array>
#include <vector>
#include <optional>
#include <map>
#include <stdint.h>

namespace agentchat {
namespace crypto {

constexpr size_t RATCHET_KEY_SIZE    = 32;
constexpr size_t RATCHET_NONCE_SIZE  = 12;
constexpr size_t MAX_SKIP_MESSAGES   = 1000; // max out-of-order messages to buffer

using RatchetKey   = std::array<uint8_t, RATCHET_KEY_SIZE>;
using MessageKey   = std::array<uint8_t, RATCHET_KEY_SIZE>;
using ChainKey     = std::array<uint8_t, RATCHET_KEY_SIZE>;
using RootKey      = std::array<uint8_t, RATCHET_KEY_SIZE>;

/**
 * @brief Skipped message key cache entry for out-of-order delivery
 */
struct SkippedKey {
    RatchetKey dh_public;  // sender's DH public key at time of send
    uint32_t   msg_num;    // message number in chain
};

/**
 * @brief Double Ratchet session state (one per peer)
 *
 * Serialisable for persistent storage. Each side maintains one
 * RatchetState per peer they communicate with.
 */
struct RatchetState {
    // Root key (32 bytes) — updated on each DH ratchet step
    RootKey root_key{};

    // DH ratchet keys
    KeyPair  dh_self;       // our current DH key pair
    RatchetKey dh_remote{}; // remote party's current DH public key

    // Sending chain
    ChainKey send_chain_key{};
    uint32_t send_msg_num{0};     // messages sent in current chain
    uint32_t prev_send_count{0};  // messages in previous send chain

    // Receiving chain
    ChainKey recv_chain_key{};
    uint32_t recv_msg_num{0};

    // Skipped message keys (for out-of-order delivery)
    std::map<std::pair<RatchetKey, uint32_t>, MessageKey> skipped;

    bool initialized{false};
};

/**
 * @brief Message header included with every ratchet-encrypted message
 */
struct RatchetHeader {
    RatchetKey dh_public;  // sender's current DH public key
    uint32_t   prev_count; // number of messages in previous sending chain
    uint32_t   msg_num;    // message number in current sending chain
};

/**
 * @brief Encrypted message from the ratchet
 */
struct RatchetMessage {
    RatchetHeader header;
    std::vector<uint8_t> ciphertext; // AES-256-GCM encrypted payload
    std::array<uint8_t, 12> nonce;
};

// ── KDF helpers ───────────────────────────────────────────────────────────────

/**
 * @brief KDF_RK: root key + DH output -> new root key + chain key
 */
bool kdf_root(const RootKey& rk,
              const std::array<uint8_t,32>& dh_out,
              RootKey& out_rk,
              ChainKey& out_ck);

/**
 * @brief KDF_CK: chain key -> new chain key + message key
 */
bool kdf_chain(const ChainKey& ck,
               ChainKey& out_ck,
               MessageKey& out_mk);

// ── Session lifecycle ─────────────────────────────────────────────────────────

/**
 * @brief Initialise ratchet state as the sender (Alice).
 *
 * Called after X3DH key agreement. Alice sends first.
 *
 * @param shared_secret  32-byte shared secret from X3DH
 * @param bob_dh_public  Bob's signed prekey public
 * @param state          Output ratchet state
 */
bool ratchet_init_sender(const std::array<uint8_t,32>& shared_secret,
                          const RatchetKey& bob_dh_public,
                          RatchetState& state);

/**
 * @brief Initialise ratchet state as the receiver (Bob).
 *
 * @param shared_secret  32-byte shared secret from X3DH
 * @param bob_dh_keypair Bob's signed prekey pair (private needed for first step)
 * @param state          Output ratchet state
 */
bool ratchet_init_receiver(const std::array<uint8_t,32>& shared_secret,
                            const KeyPair& bob_dh_keypair,
                            RatchetState& state);

// ── Message encrypt / decrypt ─────────────────────────────────────────────────

/**
 * @brief Encrypt a plaintext message, advancing the ratchet.
 *
 * @param state      Ratchet state (modified in place)
 * @param plaintext  Raw message bytes
 * @return           Encrypted message with header, or nullopt on error
 */
std::optional<RatchetMessage> ratchet_encrypt(
    RatchetState& state,
    const std::vector<uint8_t>& plaintext);

/**
 * @brief Decrypt a ratchet message, advancing the ratchet.
 *
 * Handles out-of-order delivery by caching skipped message keys.
 *
 * @param state   Ratchet state (modified in place)
 * @param msg     Encrypted message
 * @return        Decrypted plaintext, or nullopt on error / authentication failure
 */
std::optional<std::vector<uint8_t>> ratchet_decrypt(
    RatchetState& state,
    const RatchetMessage& msg);

} // namespace crypto
} // namespace agentchat
