/**
 * @file ratchet.cpp
 * @brief Double Ratchet Algorithm implementation
 */

#include "ratchet.h"
#include <agentchat/crypto.h>
#include <cstring>
#include <stdexcept>

namespace agentchat {
namespace crypto {

// ── KDF helpers ───────────────────────────────────────────────────────────────

bool kdf_root(const RootKey& rk,
              const std::array<uint8_t,32>& dh_out,
              RootKey& out_rk,
              ChainKey& out_ck) {
    // HKDF-SHA256(salt=rk, ikm=dh_out, info="AgentChat-ratchet-root", len=64)
    auto derived = hkdf_derive(
        std::span<const uint8_t>{dh_out.data(), dh_out.size()},  // shared_secret
        "AgentChat-ratchet-root",                                 // info
        std::span<const uint8_t>{rk.data(), rk.size()},           // salt
        64);                                                      // out_len
    if (derived.size() < 64) return false;
    std::copy(derived.begin(),      derived.begin()+32, out_rk.begin());
    std::copy(derived.begin()+32,   derived.begin()+64, out_ck.begin());
    return true;
}

bool kdf_chain(const ChainKey& ck,
               ChainKey& out_ck,
               MessageKey& out_mk) {
    std::vector<uint8_t> ikm_mk  = {0x01};
    std::vector<uint8_t> ikm_ck  = {0x02};

    auto mk = hkdf_derive(
        std::span<const uint8_t>{ikm_mk.data(), ikm_mk.size()},
        "AgentChat-ratchet-msg",
        std::span<const uint8_t>{ck.data(), ck.size()},
        32);
    auto nck = hkdf_derive(
        std::span<const uint8_t>{ikm_ck.data(), ikm_ck.size()},
        "AgentChat-ratchet-chain",
        std::span<const uint8_t>{ck.data(), ck.size()},
        32);

    if (mk.size() < 32 || nck.size() < 32) return false;
    std::copy(mk.begin(),  mk.begin()+32,  out_mk.begin());
    std::copy(nck.begin(), nck.begin()+32, out_ck.begin());
    return true;
}

// ── DH ratchet step ───────────────────────────────────────────────────────────

static bool dh_ratchet_step(RatchetState& state, const RatchetKey& remote_dh) {
    // Compute DH(our_dh_priv, remote_dh_pub)
    auto dh_out = x25519_exchange(state.dh_self.priv, remote_dh);
    if (!dh_out) return false;

    // Advance root key, derive new recv chain key
    if (!kdf_root(state.root_key, *dh_out, state.root_key, state.recv_chain_key))
        return false;

    // Generate new DH key pair for next step
    auto new_kp = generate_x25519_keypair();
    if (!new_kp) return false;
    state.dh_self = *new_kp;

    // Compute DH(new_priv, remote_dh_pub) for send chain
    auto dh_out2 = x25519_exchange(state.dh_self.priv, remote_dh);
    if (!dh_out2) return false;

    // Advance root key, derive new send chain key
    if (!kdf_root(state.root_key, *dh_out2, state.root_key, state.send_chain_key))
        return false;

    state.dh_remote    = remote_dh;
    state.recv_msg_num = 0;
    state.send_msg_num = 0;
    return true;
}

// ── Session init ──────────────────────────────────────────────────────────────

bool ratchet_init_sender(const std::array<uint8_t,32>& shared_secret,
                          const RatchetKey& bob_dh_public,
                          RatchetState& state) {
    // Root key = shared_secret
    std::copy(shared_secret.begin(), shared_secret.end(), state.root_key.begin());
    state.dh_remote = bob_dh_public;

    // Generate our DH key pair
    auto kp = generate_x25519_keypair();
    if (!kp) return false;
    state.dh_self = *kp;

    // Initial DH ratchet step (sender side)
    auto dh_out = x25519_exchange(state.dh_self.priv, bob_dh_public);
    if (!dh_out) return false;

    if (!kdf_root(state.root_key, *dh_out, state.root_key, state.send_chain_key))
        return false;

    state.send_msg_num  = 0;
    state.recv_msg_num  = 0;
    state.prev_send_count = 0;
    state.initialized   = true;
    return true;
}

bool ratchet_init_receiver(const std::array<uint8_t,32>& shared_secret,
                            const KeyPair& bob_dh_keypair,
                            RatchetState& state) {
    std::copy(shared_secret.begin(), shared_secret.end(), state.root_key.begin());
    state.dh_self       = bob_dh_keypair;
    // Copy public key to dh_remote placeholder (will be set on first recv)
    std::copy(bob_dh_keypair.pub.begin(), bob_dh_keypair.pub.end(), state.dh_remote.begin());
    state.send_msg_num  = 0;
    state.recv_msg_num  = 0;
    state.prev_send_count = 0;
    state.initialized   = true;
    return true;
}

// ── Encrypt ───────────────────────────────────────────────────────────────────

std::optional<RatchetMessage> ratchet_encrypt(
    RatchetState& state,
    const std::vector<uint8_t>& plaintext) {

    if (!state.initialized) return std::nullopt;

    // Advance sending chain
    ChainKey  new_ck{};
    MessageKey mk{};
    if (!kdf_chain(state.send_chain_key, new_ck, mk)) return std::nullopt;
    state.send_chain_key = new_ck;

    // Build header
    RatchetHeader hdr;
    std::copy(state.dh_self.pub.begin(), state.dh_self.pub.end(), hdr.dh_public.begin());
    hdr.prev_count = state.prev_send_count;
    hdr.msg_num    = state.send_msg_num++;

    // Encrypt with AES-256-GCM using message key
    auto ct = aes_gcm_encrypt(
        std::span<const uint8_t>{mk.data(), mk.size()},
        std::span<const uint8_t>{plaintext.data(), plaintext.size()});
    if (ct.empty()) return std::nullopt;

    RatchetMessage msg;
    msg.header = hdr;
    // aes_gcm_encrypt prepends 12-byte nonce
    if (ct.size() < 12) return std::nullopt;
    std::copy(ct.begin(), ct.begin()+12, msg.nonce.begin());
    msg.ciphertext = std::vector<uint8_t>(ct.begin()+12, ct.end());
    return msg;
}

// ── Skip messages ─────────────────────────────────────────────────────────────

static bool skip_message_keys(RatchetState& state, uint32_t until) {
    if (until - state.recv_msg_num > MAX_SKIP_MESSAGES) return false;
    while (state.recv_msg_num < until) {
        ChainKey new_ck{};
        MessageKey mk{};
        if (!kdf_chain(state.recv_chain_key, new_ck, mk)) return false;
        state.recv_chain_key = new_ck;
        auto key = std::make_pair(state.dh_remote, state.recv_msg_num);
        state.skipped[key] = mk;
        state.recv_msg_num++;
    }
    return true;
}

// ── Decrypt ───────────────────────────────────────────────────────────────────

std::optional<std::vector<uint8_t>> ratchet_decrypt(
    RatchetState& state,
    const RatchetMessage& msg) {

    if (!state.initialized) return std::nullopt;

    // Build full ciphertext (nonce + ciphertext) for aes_gcm_decrypt
    std::vector<uint8_t> full_ct;
    full_ct.insert(full_ct.end(), msg.nonce.begin(), msg.nonce.end());
    full_ct.insert(full_ct.end(), msg.ciphertext.begin(), msg.ciphertext.end());

    // Check skipped message key cache first
    auto skipped_key = std::make_pair(msg.header.dh_public, msg.header.msg_num);
    auto sk_it = state.skipped.find(skipped_key);
    if (sk_it != state.skipped.end()) {
        auto mk = sk_it->second;
        state.skipped.erase(sk_it);
        return aes_gcm_decrypt(
            std::span<const uint8_t>{mk.data(), mk.size()},
            std::span<const uint8_t>{full_ct.data(), full_ct.size()});
    }

    // DH ratchet step if new DH key
    if (msg.header.dh_public != state.dh_remote) {
        // Skip messages in current chain
        if (!skip_message_keys(state, msg.header.prev_count)) return std::nullopt;
        state.prev_send_count = state.send_msg_num;
        state.send_msg_num    = 0;
        state.recv_msg_num    = 0;
        if (!dh_ratchet_step(state, msg.header.dh_public)) return std::nullopt;
    }

    // Skip to msg_num
    if (!skip_message_keys(state, msg.header.msg_num)) return std::nullopt;

    // Derive message key
    ChainKey new_ck{};
    MessageKey mk{};
    if (!kdf_chain(state.recv_chain_key, new_ck, mk)) return std::nullopt;
    state.recv_chain_key = new_ck;
    state.recv_msg_num++;

    return aes_gcm_decrypt(
        std::span<const uint8_t>{mk.data(), mk.size()},
        std::span<const uint8_t>{full_ct.data(), full_ct.size()});
}

} // namespace crypto
} // namespace agentchat
