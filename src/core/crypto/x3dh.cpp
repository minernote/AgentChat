/**
 * @file x3dh.cpp
 * @brief X3DH Extended Triple Diffie-Hellman implementation
 */

#include "x3dh.h"
#include <agentchat/crypto.h>
#include <cstring>

namespace agentchat {
namespace crypto {

// ── Helpers ───────────────────────────────────────────────────────────────────

// Concatenate DH outputs and derive shared secret via HKDF
static std::optional<std::array<uint8_t,32>>
x3dh_kdf(const std::vector<std::vector<uint8_t>>& dh_outputs) {
    std::vector<uint8_t> ikm;
    for (auto& dh : dh_outputs)
        ikm.insert(ikm.end(), dh.begin(), dh.end());

    std::vector<uint8_t> salt(32, 0xFF);
    auto out = hkdf_derive(
        std::span<const uint8_t>{ikm.data(), ikm.size()},
        "AgentChat-X3DH",
        std::span<const uint8_t>{salt.data(), salt.size()},
        32);

    if (out.size() < 32) return std::nullopt;
    std::array<uint8_t,32> result{};
    std::copy(out.begin(), out.begin()+32, result.begin());
    return result;
}

// ── Sender (Alice) ────────────────────────────────────────────────────────────

std::optional<X3DHResult> x3dh_sender(
    const KeyPair& alice_identity,
    const PrekeyBundle& bob_bundle) {

    // Verify Bob's signed prekey
    if (!verify_prekey_bundle(bob_bundle)) return std::nullopt;

    // Generate ephemeral keypair
    auto ek_a = generate_x25519_keypair();
    if (!ek_a) return std::nullopt;

    // DH1 = DH(IK_A_priv, SPK_B_pub)  — note: IK is Ed25519, convert to X25519
    // For simplicity, we use the identity keypair's priv directly as X25519
    // (In production, use proper Ed25519->X25519 conversion)
    auto dh1 = x25519_exchange(alice_identity.priv, bob_bundle.signed_prekey);
    if (!dh1) return std::nullopt;

    // DH2 = DH(EK_A_priv, IK_B_pub)
    auto dh2 = x25519_exchange(ek_a->priv, bob_bundle.identity_key);
    if (!dh2) return std::nullopt;

    // DH3 = DH(EK_A_priv, SPK_B_pub)
    auto dh3 = x25519_exchange(ek_a->priv, bob_bundle.signed_prekey);
    if (!dh3) return std::nullopt;

    std::vector<std::vector<uint8_t>> dh_list = {
        std::vector<uint8_t>(dh1->begin(), dh1->end()),
        std::vector<uint8_t>(dh2->begin(), dh2->end()),
        std::vector<uint8_t>(dh3->begin(), dh3->end()),
    };

    bool used_opk = false;
    // DH4 = DH(EK_A_priv, OPK_B_pub) [optional]
    if (bob_bundle.one_time_prekey.has_value()) {
        auto dh4 = x25519_exchange(ek_a->priv, *bob_bundle.one_time_prekey);
        if (dh4) {
            dh_list.push_back(std::vector<uint8_t>(dh4->begin(), dh4->end()));
            used_opk = true;
        }
    }

    auto shared = x3dh_kdf(dh_list);
    if (!shared) return std::nullopt;

    X3DHResult result;
    result.shared_secret        = *shared;
    result.ephemeral_key        = ek_a->pub;
    result.used_one_time_prekey = used_opk;
    return result;
}

// ── Receiver (Bob) ────────────────────────────────────────────────────────────

std::optional<std::array<uint8_t,32>> x3dh_receiver(
    const KeyPair& bob_identity,
    const KeyPair& bob_signed_prekey,
    const std::optional<KeyPair>& bob_one_time_prekey,
    const PublicKey& alice_identity_pub,
    const PublicKey& alice_ephemeral_pub) {

    // DH1 = DH(SPK_B_priv, IK_A_pub)
    auto dh1 = x25519_exchange(bob_signed_prekey.priv, alice_identity_pub);
    if (!dh1) return std::nullopt;

    // DH2 = DH(IK_B_priv, EK_A_pub)
    auto dh2 = x25519_exchange(bob_identity.priv, alice_ephemeral_pub);
    if (!dh2) return std::nullopt;

    // DH3 = DH(SPK_B_priv, EK_A_pub)
    auto dh3 = x25519_exchange(bob_signed_prekey.priv, alice_ephemeral_pub);
    if (!dh3) return std::nullopt;

    std::vector<std::vector<uint8_t>> dh_list = {
        std::vector<uint8_t>(dh1->begin(), dh1->end()),
        std::vector<uint8_t>(dh2->begin(), dh2->end()),
        std::vector<uint8_t>(dh3->begin(), dh3->end()),
    };

    // DH4 = DH(OPK_B_priv, EK_A_pub) [optional]
    if (bob_one_time_prekey.has_value()) {
        auto dh4 = x25519_exchange(bob_one_time_prekey->priv, alice_ephemeral_pub);
        if (dh4)
            dh_list.push_back(std::vector<uint8_t>(dh4->begin(), dh4->end()));
    }

    return x3dh_kdf(dh_list);
}

// ── Verify prekey bundle ──────────────────────────────────────────────────────

bool verify_prekey_bundle(const PrekeyBundle& bundle) {
    // Verify Ed25519 signature: IK_B signed SPK_B_pub
    return ed25519_verify(
        bundle.identity_key,
        std::vector<uint8_t>(bundle.signed_prekey.begin(), bundle.signed_prekey.end()),
        bundle.spk_signature);
}

} // namespace crypto
} // namespace agentchat
