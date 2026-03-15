/**
 * @file group_ratchet.cpp
 * @brief Megolm-style group channel encryption implementation
 */

#include "group_ratchet.h"
#include <agentchat/crypto.h>
#include <cstring>
#include <stdexcept>

namespace agentchat {
namespace crypto {

// ── KDF ───────────────────────────────────────────────────────────────────────

bool group_kdf(const GroupChainKey& ck,
               GroupChainKey& out_ck,
               GroupMsgKey&   out_mk) {
    // message key: HKDF(ikm=0x01, salt=ck, info="AgentChat-group-msg")
    std::vector<uint8_t> ikm_mk = {0x01};
    auto mk = hkdf_derive(
        std::span<const uint8_t>{ikm_mk.data(), ikm_mk.size()},
        "AgentChat-group-msg",
        std::span<const uint8_t>{ck.data(), ck.size()},
        32);

    // next chain key: HKDF(ikm=0x02, salt=ck, info="AgentChat-group-chain")
    std::vector<uint8_t> ikm_ck = {0x02};
    auto nck = hkdf_derive(
        std::span<const uint8_t>{ikm_ck.data(), ikm_ck.size()},
        "AgentChat-group-chain",
        std::span<const uint8_t>{ck.data(), ck.size()},
        32);

    if (mk.size() < 32 || nck.size() < 32) return false;
    std::copy(mk.begin(),  mk.begin()  + 32, out_mk.begin());
    std::copy(nck.begin(), nck.begin() + 32, out_ck.begin());
    return true;
}

// ── Session lifecycle ─────────────────────────────────────────────────────────

std::optional<OutboundGroupSession> group_session_create() {
    OutboundGroupSession s;

    // Random session_id
    auto id_bytes = random_bytes(GROUP_SESSION_ID_SIZE);
    if (id_bytes.size() < GROUP_SESSION_ID_SIZE) return std::nullopt;
    std::copy(id_bytes.begin(), id_bytes.end(), s.session_id.begin());

    // Random initial chain key
    auto ck_bytes = random_bytes(GROUP_CHAIN_KEY_SIZE);
    if (ck_bytes.size() < GROUP_CHAIN_KEY_SIZE) return std::nullopt;
    std::copy(ck_bytes.begin(), ck_bytes.end(), s.chain_key.begin());

    s.index = 0;
    return s;
}

std::vector<uint8_t> group_session_export(const OutboundGroupSession& s) {
    // [session_id: 32][chain_key: 32][index: u32 big-endian] = 68 bytes
    std::vector<uint8_t> out;
    out.reserve(68);
    out.insert(out.end(), s.session_id.begin(), s.session_id.end());
    out.insert(out.end(), s.chain_key.begin(),  s.chain_key.end());
    out.push_back(static_cast<uint8_t>((s.index >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((s.index >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((s.index >>  8) & 0xFF));
    out.push_back(static_cast<uint8_t>( s.index        & 0xFF));
    return out;
}

std::optional<InboundGroupSession> group_session_import(
    const std::vector<uint8_t>& exported) {
    if (exported.size() < 68) return std::nullopt;

    InboundGroupSession s;
    std::copy(exported.begin(),      exported.begin() + 32, s.session_id.begin());
    std::copy(exported.begin() + 32, exported.begin() + 64, s.chain_key.begin());
    s.known_index =
        (static_cast<uint32_t>(exported[64]) << 24) |
        (static_cast<uint32_t>(exported[65]) << 16) |
        (static_cast<uint32_t>(exported[66]) <<  8) |
         static_cast<uint32_t>(exported[67]);
    return s;
}

// ── Encrypt ───────────────────────────────────────────────────────────────────

std::optional<GroupMessage> group_encrypt(
    OutboundGroupSession& session,
    const std::vector<uint8_t>& plaintext) {

    // Derive message key for current index
    GroupChainKey next_ck{};
    GroupMsgKey   msg_key{};
    if (!group_kdf(session.chain_key, next_ck, msg_key)) return std::nullopt;

    // AES-256-GCM encrypt — API prepends nonce internally, returns nonce||ct||tag
    std::array<uint8_t, KEY_SIZE> key_arr{};
    std::copy(msg_key.begin(), msg_key.end(), key_arr.begin());

    auto ct = agentchat::crypto::aes_gcm_encrypt(
        std::span<const uint8_t>{key_arr.data(), key_arr.size()},
        std::span<const uint8_t>{plaintext.data(), plaintext.size()});
    if (ct.empty()) return std::nullopt;

    // aes_gcm_encrypt prepends a 12-byte nonce — extract it
    if (ct.size() < 12) return std::nullopt;
    std::array<uint8_t, 12> nonce{};
    std::copy(ct.begin(), ct.begin() + 12, nonce.begin());

    // Advance ratchet
    session.chain_key = next_ck;
    uint32_t used_index = session.index++;

    GroupMessage msg;
    msg.session_id   = session.session_id;
    msg.sender_index = used_index;
    msg.nonce        = nonce;
    msg.ciphertext   = std::move(ct); // full nonce||ct||tag blob
    return msg;
}

// ── Decrypt ───────────────────────────────────────────────────────────────────

std::optional<std::vector<uint8_t>> group_decrypt(
    InboundGroupSession& session,
    const GroupMessage&  msg) {

    if (msg.session_id != session.session_id) return std::nullopt;

    GroupMsgKey msg_key{};
    bool found_in_cache = false;

    // Check cache first (out-of-order delivery)
    auto it = session.key_cache.find(msg.sender_index);
    if (it != session.key_cache.end()) {
        msg_key = it->second;
        session.key_cache.erase(it);
        found_in_cache = true;
    }

    if (!found_in_cache) {
        if (msg.sender_index < session.known_index) {
            // Already past this index and not cached — cannot decrypt
            return std::nullopt;
        }

        // Advance chain forward, caching skipped keys
        uint32_t steps = msg.sender_index - session.known_index;
        if (steps > GROUP_MAX_SKIP) return std::nullopt;

        GroupChainKey ck = session.chain_key;
        for (uint32_t i = 0; i < steps; ++i) {
            GroupChainKey next_ck{};
            GroupMsgKey   skipped_mk{};
            if (!group_kdf(ck, next_ck, skipped_mk)) return std::nullopt;
            // Cache the skipped key
            session.key_cache[session.known_index + i] = skipped_mk;
            ck = next_ck;
        }

        // Derive message key at target index
        GroupChainKey next_ck{};
        if (!group_kdf(ck, next_ck, msg_key)) return std::nullopt;

        // Advance session state
        session.chain_key   = next_ck;
        session.known_index = msg.sender_index + 1;
    }

    // AES-256-GCM decrypt — ciphertext stored as nonce||ct||tag blob
    std::array<uint8_t, KEY_SIZE> key_arr{};
    std::copy(msg_key.begin(), msg_key.end(), key_arr.begin());

    auto plaintext = agentchat::crypto::aes_gcm_decrypt(
        std::span<const uint8_t>{key_arr.data(), key_arr.size()},
        std::span<const uint8_t>{msg.ciphertext.data(), msg.ciphertext.size()});
    if (plaintext.empty()) return std::nullopt;
    return plaintext;
}

} // namespace crypto
} // namespace agentchat
