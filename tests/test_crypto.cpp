#include <agentchat/crypto.h>
#include <agentchat/types.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond) do { if (!(cond)) throw std::runtime_error( \
    std::string("ASSERT: " #cond " L") + std::to_string(__LINE__)); } while(0)

#define RUN(name) do { \
    std::cout << "  " #name " ... " << std::flush; \
    try { test_##name(); std::cout << "PASS\n"; ++g_pass; } \
    catch (const std::exception& e) { std::cout << "FAIL: " << e.what() << "\n"; ++g_fail; } \
} while(0)

static void test_keygen_x25519() {
    auto kp = agentchat::crypto::generate_x25519_keypair();
    ASSERT(kp.has_value());
    bool nz = false;
    for (auto b : kp->pub) if (b) { nz = true; break; }
    ASSERT(nz);
}

static void test_keygen_ed25519() {
    auto kp = agentchat::crypto::generate_ed25519_keypair();
    ASSERT(kp.has_value());
    bool nz = false;
    for (auto b : kp->pub) if (b) { nz = true; break; }
    ASSERT(nz);
}

static void test_x25519_ecdh() {
    auto alice = agentchat::crypto::generate_x25519_keypair();
    auto bob   = agentchat::crypto::generate_x25519_keypair();
    ASSERT(alice && bob);
    auto sa = agentchat::crypto::x25519_exchange(alice->priv, bob->pub);
    auto sb = agentchat::crypto::x25519_exchange(bob->priv,   alice->pub);
    ASSERT(sa && sb);
    ASSERT(*sa == *sb);
}

static void test_hkdf() {
    std::vector<uint8_t> secret(32, 0xAB);
    auto k1 = agentchat::crypto::hkdf_derive(std::span<const uint8_t>{secret}, "ctx");
    auto k2 = agentchat::crypto::hkdf_derive(std::span<const uint8_t>{secret}, "ctx");
    ASSERT(!k1.empty() && k1 == k2);
    auto k3 = agentchat::crypto::hkdf_derive(std::span<const uint8_t>{secret}, "other");
    ASSERT(k1 != k3);
}

static void test_aes_gcm_roundtrip() {
    auto key = agentchat::crypto::random_bytes(32);
    std::string pt_str = "Hello, AgentChat!";
    std::vector<uint8_t> pt(pt_str.begin(), pt_str.end());
    auto ct  = agentchat::crypto::aes_gcm_encrypt(
        std::span<const uint8_t>{key}, std::span<const uint8_t>{pt});
    ASSERT(!ct.empty() && ct.size() > pt.size());
    auto dec = agentchat::crypto::aes_gcm_decrypt(
        std::span<const uint8_t>{key}, std::span<const uint8_t>{ct});
    ASSERT(dec == pt);
}

static void test_aes_gcm_tamper() {
    auto key = agentchat::crypto::random_bytes(32);
    std::vector<uint8_t> pt = {1,2,3,4,5};
    auto ct = agentchat::crypto::aes_gcm_encrypt(
        std::span<const uint8_t>{key}, std::span<const uint8_t>{pt});
    ct[agentchat::crypto::NONCE_SIZE + 1] ^= 0xFF;
    auto dec = agentchat::crypto::aes_gcm_decrypt(
        std::span<const uint8_t>{key}, std::span<const uint8_t>{ct});
    ASSERT(dec.empty());
}

static void test_aes_gcm_aad() {
    auto key = agentchat::crypto::random_bytes(32);
    std::vector<uint8_t> pt  = {'s','e','c'};
    std::vector<uint8_t> aad = {'h','d','r'};
    auto ct = agentchat::crypto::aes_gcm_encrypt(
        std::span<const uint8_t>{key},
        std::span<const uint8_t>{pt},
        std::span<const uint8_t>{aad});
    auto dec1 = agentchat::crypto::aes_gcm_decrypt(
        std::span<const uint8_t>{key},
        std::span<const uint8_t>{ct},
        std::span<const uint8_t>{aad});
    ASSERT(dec1 == pt);
    std::vector<uint8_t> bad = {'x'};
    auto dec2 = agentchat::crypto::aes_gcm_decrypt(
        std::span<const uint8_t>{key},
        std::span<const uint8_t>{ct},
        std::span<const uint8_t>{bad});
    ASSERT(dec2.empty());
}

static void test_ed25519() {
    auto kp = agentchat::crypto::generate_ed25519_keypair();
    ASSERT(kp.has_value());
    std::string msg = "sign me";
    std::vector<uint8_t> mv(msg.begin(), msg.end());
    auto sig = agentchat::crypto::ed25519_sign(kp->priv, std::span<const uint8_t>{mv});
    ASSERT(sig.has_value());
    ASSERT(agentchat::crypto::ed25519_verify(kp->pub, std::span<const uint8_t>{mv}, *sig));
    mv[0] ^= 1;
    ASSERT(!agentchat::crypto::ed25519_verify(kp->pub, std::span<const uint8_t>{mv}, *sig));
}

static void test_hex() {
    auto rnd = agentchat::crypto::random_bytes(16);
    auto hex = agentchat::crypto::to_hex(std::span<const uint8_t>{rnd});
    ASSERT(hex.size() == 32);
    ASSERT(agentchat::crypto::from_hex(hex) == rnd);
}

static void test_e2e_flow() {
    auto alice_id = agentchat::crypto::generate_ed25519_keypair();
    auto alice_ex = agentchat::crypto::generate_x25519_keypair();
    auto bob_ex   = agentchat::crypto::generate_x25519_keypair();
    ASSERT(alice_id && alice_ex && bob_ex);
    auto sa = agentchat::crypto::x25519_exchange(alice_ex->priv, bob_ex->pub);
    auto sb = agentchat::crypto::x25519_exchange(bob_ex->priv,   alice_ex->pub);
    ASSERT(sa && sb && *sa == *sb);
    auto key = agentchat::crypto::hkdf_derive(
        std::span<const uint8_t>{sa->data(), sa->size()},
        "AgentChat-v1-session");
    ASSERT(!key.empty());
    std::string plaintext = "Agent command: trade BTC";
    std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
    auto ct  = agentchat::crypto::aes_gcm_encrypt(
        std::span<const uint8_t>{key}, std::span<const uint8_t>{pt});
    ASSERT(!ct.empty());
    auto dec = agentchat::crypto::aes_gcm_decrypt(
        std::span<const uint8_t>{key}, std::span<const uint8_t>{ct});
    ASSERT(dec == pt);
    auto sig = agentchat::crypto::ed25519_sign(
        alice_id->priv, std::span<const uint8_t>{ct});
    ASSERT(sig.has_value());
    ASSERT(agentchat::crypto::ed25519_verify(
        alice_id->pub, std::span<const uint8_t>{ct}, *sig));
}

// ── Group ratchet (Megolm-style) tests ───────────────────────────────────────
#include "../src/core/crypto/group_ratchet.h"

static void test_group_ratchet_basic() {
    // Create outbound session, export before any messages, import as inbound
    auto out_opt = agentchat::crypto::group_session_create();
    ASSERT(out_opt.has_value());
    auto exported = agentchat::crypto::group_session_export(*out_opt);
    ASSERT(exported.size() == 68);
    auto in_opt = agentchat::crypto::group_session_import(exported);
    ASSERT(in_opt.has_value());

    std::string m1 = "Hello channel";
    std::string m2 = "Second message";
    std::vector<uint8_t> pt1(m1.begin(), m1.end());
    std::vector<uint8_t> pt2(m2.begin(), m2.end());

    auto e1 = agentchat::crypto::group_encrypt(*out_opt, pt1);
    auto e2 = agentchat::crypto::group_encrypt(*out_opt, pt2);
    ASSERT(e1.has_value() && e2.has_value());
    ASSERT(e1->sender_index == 0);
    ASSERT(e2->sender_index == 1);

    auto d1 = agentchat::crypto::group_decrypt(*in_opt, *e1);
    auto d2 = agentchat::crypto::group_decrypt(*in_opt, *e2);
    ASSERT(d1.has_value() && *d1 == pt1);
    ASSERT(d2.has_value() && *d2 == pt2);
}

static void test_group_ratchet_out_of_order() {
    auto out_opt = agentchat::crypto::group_session_create();
    ASSERT(out_opt.has_value());
    auto exported = agentchat::crypto::group_session_export(*out_opt);
    auto in_opt   = agentchat::crypto::group_session_import(exported);
    ASSERT(in_opt.has_value());

    std::vector<uint8_t> pt0 = {'A'};
    std::vector<uint8_t> pt1 = {'B'};
    std::vector<uint8_t> pt2 = {'C'};

    auto e0 = agentchat::crypto::group_encrypt(*out_opt, pt0);
    auto e1 = agentchat::crypto::group_encrypt(*out_opt, pt1);
    auto e2 = agentchat::crypto::group_encrypt(*out_opt, pt2);
    ASSERT(e0 && e1 && e2);

    // Deliver out of order: 2, 0, 1
    auto d2 = agentchat::crypto::group_decrypt(*in_opt, *e2);
    ASSERT(d2.has_value() && *d2 == pt2);
    auto d0 = agentchat::crypto::group_decrypt(*in_opt, *e0);
    ASSERT(d0.has_value() && *d0 == pt0);
    auto d1 = agentchat::crypto::group_decrypt(*in_opt, *e1);
    ASSERT(d1.has_value() && *d1 == pt1);
}

static void test_group_ratchet_tamper() {
    auto out_opt = agentchat::crypto::group_session_create();
    ASSERT(out_opt.has_value());
    auto exported = agentchat::crypto::group_session_export(*out_opt);
    auto in_opt   = agentchat::crypto::group_session_import(exported);
    ASSERT(in_opt.has_value());

    std::vector<uint8_t> pt = {'s', 'e', 'c', 'r', 'e', 't'};
    auto enc = agentchat::crypto::group_encrypt(*out_opt, pt);
    ASSERT(enc.has_value());
    enc->ciphertext[0] ^= 0xFF; // tamper
    auto dec = agentchat::crypto::group_decrypt(*in_opt, *enc);
    ASSERT(!dec.has_value()); // must fail auth
}

static void test_group_session_export_import_roundtrip() {
    auto out_opt = agentchat::crypto::group_session_create();
    ASSERT(out_opt.has_value());
    auto& out = *out_opt;

    // Advance ratchet 5 steps
    std::vector<uint8_t> dummy = {1, 2, 3};
    for (int i = 0; i < 5; ++i) {
        auto e = agentchat::crypto::group_encrypt(out, dummy);
        ASSERT(e.has_value());
    }
    ASSERT(out.index == 5);

    // Snapshot current state and export
    agentchat::crypto::OutboundGroupSession snap;
    snap.session_id = out.session_id;
    snap.chain_key  = out.chain_key;
    snap.index      = out.index;
    auto blob = agentchat::crypto::group_session_export(snap);
    ASSERT(blob.size() == 68);

    auto in_opt = agentchat::crypto::group_session_import(blob);
    ASSERT(in_opt.has_value());
    ASSERT(in_opt->known_index == 5);
    ASSERT(in_opt->session_id == out.session_id);

    // Encrypt message at index 5, decrypt with imported inbound
    std::vector<uint8_t> pt = {'h', 'i'};
    auto enc = agentchat::crypto::group_encrypt(out, pt);
    ASSERT(enc.has_value() && enc->sender_index == 5);
    auto dec = agentchat::crypto::group_decrypt(*in_opt, *enc);
    ASSERT(dec.has_value() && *dec == pt);
}

int main() {
    std::cout << "=== AgentChat Crypto Tests ===\n";
    RUN(keygen_x25519);
    RUN(keygen_ed25519);
    RUN(x25519_ecdh);
    RUN(hkdf);
    RUN(aes_gcm_roundtrip);
    RUN(aes_gcm_tamper);
    RUN(aes_gcm_aad);
    RUN(ed25519);
    RUN(hex);
    RUN(e2e_flow);
    RUN(group_ratchet_basic);
    RUN(group_ratchet_out_of_order);
    RUN(group_ratchet_tamper);
    RUN(group_session_export_import_roundtrip);
    std::cout << "\nResult: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail ? 1 : 0;
}
