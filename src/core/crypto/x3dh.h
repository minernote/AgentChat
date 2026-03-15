#pragma once
/**
 * @file x3dh.h
 * @brief Extended Triple Diffie-Hellman (X3DH) key agreement
 *
 * Used to establish a shared secret between two agents before
 * initialising a Double Ratchet session.
 *
 * Alice (sender) uses:
 *   - Her identity keypair (IK_A)
 *   - An ephemeral keypair (EK_A) generated per session
 *   - Bob's identity key (IK_B)
 *   - Bob's signed prekey (SPK_B)
 *   - Bob's one-time prekey (OPK_B, optional)
 *
 * Shared secret = KDF(DH1 || DH2 || DH3 || DH4)
 *   DH1 = DH(IK_A, SPK_B)
 *   DH2 = DH(EK_A, IK_B)
 *   DH3 = DH(EK_A, SPK_B)
 *   DH4 = DH(EK_A, OPK_B)  [if OPK present]
 *
 * Reference: https://signal.org/docs/specifications/x3dh/
 */

#include <agentchat/crypto.h>
#include <array>
#include <vector>
#include <optional>

namespace agentchat {
namespace crypto {

/**
 * @brief Bob's prekey bundle — published to server, fetched by Alice
 */
struct PrekeyBundle {
    PublicKey identity_key;      // IK_B (Ed25519 → converted to X25519)
    PublicKey signed_prekey;     // SPK_B (X25519)
    Signature spk_signature;     // Ed25519 signature over SPK_B by IK_B
    std::optional<PublicKey> one_time_prekey; // OPK_B (X25519, optional)
};

/**
 * @brief Result of X3DH key agreement (Alice's side)
 */
struct X3DHResult {
    std::array<uint8_t, 32> shared_secret;  // 32-byte shared secret
    PublicKey ephemeral_key;                  // EK_A public — sent to Bob in initial message
    bool used_one_time_prekey{false};         // whether OPK was consumed
};

/**
 * @brief Perform X3DH as the initiator (Alice).
 *
 * @param alice_identity   Alice's identity keypair (IK_A)
 * @param bob_bundle       Bob's prekey bundle from server
 * @return X3DHResult or nullopt on error
 */
std::optional<X3DHResult> x3dh_sender(
    const KeyPair& alice_identity,
    const PrekeyBundle& bob_bundle);

/**
 * @brief Perform X3DH as the responder (Bob).
 *
 * @param bob_identity       Bob's identity keypair (IK_B)
 * @param bob_signed_prekey  Bob's signed prekey pair (SPK_B)
 * @param bob_one_time_prekey Bob's one-time prekey pair (optional)
 * @param alice_identity_pub Alice's identity public key (IK_A)
 * @param alice_ephemeral_pub Alice's ephemeral public key (EK_A)
 * @return 32-byte shared secret or nullopt on error
 */
std::optional<std::array<uint8_t,32>> x3dh_receiver(
    const KeyPair& bob_identity,
    const KeyPair& bob_signed_prekey,
    const std::optional<KeyPair>& bob_one_time_prekey,
    const PublicKey& alice_identity_pub,
    const PublicKey& alice_ephemeral_pub);

/**
 * @brief Verify a signed prekey bundle.
 * @return true if signature is valid
 */
bool verify_prekey_bundle(const PrekeyBundle& bundle);

} // namespace crypto
} // namespace agentchat
