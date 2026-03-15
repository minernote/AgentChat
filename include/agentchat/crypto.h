#pragma once

/**
 * @file crypto.h
 * @brief AgentChat cryptography interface (OpenSSL 3.x)
 *
 * Algorithms:
 *   Key exchange  : X25519 ECDH
 *   Encryption    : AES-256-GCM (authenticated)
 *   Signing       : Ed25519
 *   Key derivation: HKDF-SHA256
 *
 * All functions return false / empty on failure and never throw.
 * Callers should treat a false return as a hard crypto failure.
 */

#include <agentchat/types.h>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace agentchat::crypto {

// ── Constants ─���──────────────────────────────────────────���────────────────────

inline constexpr size_t KEY_SIZE     = 32;   ///< X25519 / Ed25519 key bytes
inline constexpr size_t NONCE_SIZE   = 12;   ///< AES-GCM nonce bytes
inline constexpr size_t TAG_SIZE     = 16;   ///< AES-GCM auth tag bytes
inline constexpr size_t SIG_SIZE     = 64;   ///< Ed25519 signature bytes

// ── Key pair ───────────────────────────────────���─────────────────────────────

struct KeyPair {
    PrivateKey priv;
    PublicKey  pub;
};

/**
 * @brief Generate a new X25519 key pair for ECDH key exchange.
 */
std::optional<KeyPair> generate_x25519_keypair();

/**
 * @brief Generate a new Ed25519 key pair for signing.
 */
std::optional<KeyPair> generate_ed25519_keypair();

// ── X25519 ECDH ───────────────────────────────────────────────────────────────

/**
 * @brief Perform X25519 Diffie-Hellman.
 *
 * @param our_private   Our X25519 private key (32 bytes).
 * @param their_public  Peer's X25519 public key (32 bytes).
 * @return 32-byte shared secret, or nullopt on failure.
 */
std::optional<std::array<uint8_t, 32>> x25519_exchange(
    const PrivateKey& our_private,
    const PublicKey&  their_public);

// ── HKDF-SHA256 key derivation ────────────────────────���───────────────────────

/**
 * @brief Derive a symmetric key from a shared secret via HKDF-SHA256.
 *
 * @param shared_secret  Raw DH output (32 bytes).
 * @param info           Context string (e.g. \"AgentChat-v1-session\").
 * @param salt           Optional salt bytes; empty = zero-filled.
 * @param out_len        Desired output length (default 32 for AES-256).
 * @return Derived key bytes, or empty on failure.
 */
std::vector<uint8_t> hkdf_derive(
    std::span<const uint8_t> shared_secret,
    const std::string&       info,
    std::span<const uint8_t> salt    = {},
    size_t                   out_len = KEY_SIZE);

// ── AES-256-GCM ────���──────────────────────────────────────────���───────────────

/**
 * @brief Encrypt plaintext with AES-256-GCM.
 *
 * A random 12-byte nonce is generated internally and prepended to the output:
 *   output = [12-byte nonce][ciphertext][16-byte tag]
 *
 * @param key        32-byte symmetric key.
 * @param plaintext  Data to encrypt.
 * @param aad        Additional authenticated data (may be empty).
 * @return Nonce || ciphertext || tag, or empty on failure.
 */
std::vector<uint8_t> aes_gcm_encrypt(
    std::span<const uint8_t> key,
    std::span<const uint8_t> plaintext,
    std::span<const uint8_t> aad = {});

/**
 * @brief Decrypt AES-256-GCM ciphertext.
 *
 * Expects input in the format produced by aes_gcm_encrypt:
 *   input = [12-byte nonce][ciphertext][16-byte tag]
 *
 * @param key     32-byte symmetric key.
 * @param input   Nonce || ciphertext || tag.
 * @param aad     Additional authenticated data (must match encryption).
 * @return Decrypted plaintext, or empty on authentication failure.
 */
std::vector<uint8_t> aes_gcm_decrypt(
    std::span<const uint8_t> key,
    std::span<const uint8_t> input,
    std::span<const uint8_t> aad = {});

// ── Ed25519 signing ──────────────────────────────────────���────────────────────

/**
 * @brief Sign a message with an Ed25519 private key.
 *
 * @param private_key  64-byte Ed25519 private key (seed || public).
 * @param message      Data to sign.
 * @return 64-byte signature, or nullopt on failure.
 */
std::optional<Signature> ed25519_sign(
    const PrivateKey&        private_key,
    std::span<const uint8_t> message);

/**
 * @brief Verify an Ed25519 signature.
 *
 * @param public_key  32-byte Ed25519 public key.
 * @param message     Original data.
 * @param signature   64-byte signature to verify.
 * @return true if valid, false otherwise.
 */
bool ed25519_verify(
    const PublicKey&         public_key,
    std::span<const uint8_t> message,
    const Signature&         signature);

// ── Utilities ──────────────────────────────────────────────────���──────────────

/**
 * @brief Generate cryptographically secure random bytes.
 */
std::vector<uint8_t> random_bytes(size_t count);

/**
 * @brief Derive Ed25519 public key from a private key.
 * @return 32-byte public key, or nullopt on failure.
 */
std::optional<PublicKey> ed25519_pubkey_from_private(const PrivateKey& private_key);

/**
 * @brief Derive X25519 public key from a private key.
 * @return 32-byte public key, or nullopt on failure.
 */
std::optional<PublicKey> x25519_pubkey_from_private(const PrivateKey& private_key);

/**
 * @brief Compute SHA-256 hash of data.
 */
std::vector<uint8_t> sha256(std::span<const uint8_t> data);

/**
 * @brief Encode bytes to lowercase hex string.
 */
std::string to_hex(std::span<const uint8_t> data);

/**
 * @brief Decode hex string to bytes. Returns empty on invalid input.
 */
std::vector<uint8_t> from_hex(const std::string& hex);

} // namespace agentchat::crypto
