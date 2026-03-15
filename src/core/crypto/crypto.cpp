#include <agentchat/crypto.h>

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <cassert>
#include <cstring>
#include <memory>

namespace agentchat::crypto {

// ── Internal RAII wrappers ────────────────────���───────────────────────────────

namespace {

struct EvpPkeyDeleter     { void operator()(EVP_PKEY*       p) const { EVP_PKEY_free(p);        } };
struct EvpPkeyCtxDeleter  { void operator()(EVP_PKEY_CTX*  p) const { EVP_PKEY_CTX_free(p);    } };
struct EvpCipherCtxDeleter{ void operator()(EVP_CIPHER_CTX* p) const { EVP_CIPHER_CTX_free(p); } };
struct EvpMdCtxDeleter    { void operator()(EVP_MD_CTX*     p) const { EVP_MD_CTX_free(p);     } };

using EvpPkeyPtr      = std::unique_ptr<EVP_PKEY,       EvpPkeyDeleter>;
using EvpPkeyCtxPtr   = std::unique_ptr<EVP_PKEY_CTX,  EvpPkeyCtxDeleter>;
using EvpCipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, EvpCipherCtxDeleter>;
using EvpMdCtxPtr     = std::unique_ptr<EVP_MD_CTX,     EvpMdCtxDeleter>;

std::optional<KeyPair> generate_raw_keypair(int nid) {
    EvpPkeyCtxPtr ctx{ EVP_PKEY_CTX_new_id(nid, nullptr) };
    if (!ctx) return std::nullopt;
    if (EVP_PKEY_keygen_init(ctx.get()) <= 0) return std::nullopt;

    EVP_PKEY* raw = nullptr;
    if (EVP_PKEY_keygen(ctx.get(), &raw) <= 0) return std::nullopt;
    EvpPkeyPtr key{ raw };

    KeyPair kp{};
    size_t priv_len = kp.priv.size();
    size_t pub_len  = kp.pub.size();
    if (EVP_PKEY_get_raw_private_key(key.get(), kp.priv.data(), &priv_len) <= 0) return std::nullopt;
    if (EVP_PKEY_get_raw_public_key (key.get(), kp.pub.data(),  &pub_len)  <= 0) return std::nullopt;
    return kp;
}

} // anonymous namespace

// ── Key generation ──────────────────────────────────────────────────���────────

std::optional<KeyPair> generate_x25519_keypair() { return generate_raw_keypair(EVP_PKEY_X25519);  }
std::optional<KeyPair> generate_ed25519_keypair() { return generate_raw_keypair(EVP_PKEY_ED25519); }

// ── X25519 ECDH ──────────────────────────────────────────────────────────────

std::optional<std::array<uint8_t, 32>> x25519_exchange(
    const PrivateKey& our_private,
    const PublicKey&  their_public)
{
    EvpPkeyPtr our_key{
        EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr,
                                     our_private.data(), our_private.size()) };
    if (!our_key) return std::nullopt;

    EvpPkeyPtr their_key{
        EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr,
                                    their_public.data(), their_public.size()) };
    if (!their_key) return std::nullopt;

    EvpPkeyCtxPtr ctx{ EVP_PKEY_CTX_new(our_key.get(), nullptr) };
    if (!ctx) return std::nullopt;
    if (EVP_PKEY_derive_init(ctx.get()) <= 0) return std::nullopt;
    if (EVP_PKEY_derive_set_peer(ctx.get(), their_key.get()) <= 0) return std::nullopt;

    std::array<uint8_t, 32> secret{};
    size_t secret_len = secret.size();
    if (EVP_PKEY_derive(ctx.get(), secret.data(), &secret_len) <= 0) return std::nullopt;
    return secret;
}

// ── HKDF-SHA256 ────────────────────────────────���─────────────────────────────

std::vector<uint8_t> hkdf_derive(
    std::span<const uint8_t> shared_secret,
    const std::string&       info,
    std::span<const uint8_t> salt,
    size_t                   out_len)
{
    std::vector<uint8_t> out(out_len);

    EvpPkeyCtxPtr ctx{ EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr) };
    if (!ctx) return {};
    if (EVP_PKEY_derive_init(ctx.get()) <= 0) return {};
    if (EVP_PKEY_CTX_set_hkdf_md(ctx.get(), EVP_sha256()) <= 0) return {};

    if (!salt.empty()) {
        if (EVP_PKEY_CTX_set1_hkdf_salt(ctx.get(), salt.data(),
                                         static_cast<int>(salt.size())) <= 0) return {};
    }
    if (EVP_PKEY_CTX_set1_hkdf_key(ctx.get(), shared_secret.data(),
                                    static_cast<int>(shared_secret.size())) <= 0) return {};
    if (EVP_PKEY_CTX_add1_hkdf_info(ctx.get(),
                                     reinterpret_cast<const uint8_t*>(info.data()),
                                     static_cast<int>(info.size())) <= 0) return {};

    size_t derived_len = out_len;
    if (EVP_PKEY_derive(ctx.get(), out.data(), &derived_len) <= 0) return {};
    out.resize(derived_len);
    return out;
}

// ── AES-256-GCM encrypt ────────────────────────────���─────────────────────────

std::vector<uint8_t> aes_gcm_encrypt(
    std::span<const uint8_t> key,
    std::span<const uint8_t> plaintext,
    std::span<const uint8_t> aad)
{
    if (key.size() != 32) return {};

    // Generate random nonce
    auto nonce = random_bytes(NONCE_SIZE);
    if (nonce.size() != NONCE_SIZE) return {};

    EvpCipherCtxPtr ctx{ EVP_CIPHER_CTX_new() };
    if (!ctx) return {};
    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) return {};
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, NONCE_SIZE, nullptr) != 1) return {};
    if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) != 1) return {};

    if (!aad.empty()) {
        int aad_len = 0;
        if (EVP_EncryptUpdate(ctx.get(), nullptr, &aad_len,
                              aad.data(), static_cast<int>(aad.size())) != 1) return {};
    }

    std::vector<uint8_t> ciphertext(plaintext.size());
    int len = 0;
    if (!plaintext.empty()) {
        if (EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &len,
                              plaintext.data(), static_cast<int>(plaintext.size())) != 1) return {};
    }
    int final_len = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + len, &final_len) != 1) return {};
    ciphertext.resize(static_cast<size_t>(len + final_len));

    // Extract auth tag
    std::vector<uint8_t> tag(TAG_SIZE);
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag.data()) != 1) return {};

    // Output: nonce || ciphertext || tag
    std::vector<uint8_t> out;
    out.reserve(NONCE_SIZE + ciphertext.size() + TAG_SIZE);
    out.insert(out.end(), nonce.begin(),      nonce.end());
    out.insert(out.end(), ciphertext.begin(), ciphertext.end());
    out.insert(out.end(), tag.begin(),        tag.end());
    return out;
}

// ── AES-256-GCM decrypt ──────────────────────────────────────────────────────

std::vector<uint8_t> aes_gcm_decrypt(
    std::span<const uint8_t> key,
    std::span<const uint8_t> input,
    std::span<const uint8_t> aad)
{
    if (key.size() != 32) return {};
    if (input.size() < NONCE_SIZE + TAG_SIZE) return {};

    const uint8_t* nonce_ptr   = input.data();
    const uint8_t* ct_ptr      = input.data() + NONCE_SIZE;
    size_t         ct_len      = input.size() - NONCE_SIZE - TAG_SIZE;
    const uint8_t* tag_ptr     = input.data() + NONCE_SIZE + ct_len;

    EvpCipherCtxPtr ctx{ EVP_CIPHER_CTX_new() };
    if (!ctx) return {};
    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) return {};
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, NONCE_SIZE, nullptr) != 1) return {};
    if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce_ptr) != 1) return {};

    if (!aad.empty()) {
        int aad_len = 0;
        if (EVP_DecryptUpdate(ctx.get(), nullptr, &aad_len,
                              aad.data(), static_cast<int>(aad.size())) != 1) return {};
    }

    std::vector<uint8_t> plaintext(ct_len);
    int len = 0;
    if (ct_len > 0) {
        if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &len,
                              ct_ptr, static_cast<int>(ct_len)) != 1) return {};
    }

    // Set expected tag
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, TAG_SIZE,
                             const_cast<uint8_t*>(tag_ptr)) != 1) return {};

    int final_len = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + len, &final_len) <= 0)
        return {};  // Authentication failed

    plaintext.resize(static_cast<size_t>(len + final_len));
    return plaintext;
}

// ── Ed25519 sign ─────────────────���───────────────────────────────────────────

std::optional<Signature> ed25519_sign(
    const PrivateKey&        private_key,
    std::span<const uint8_t> message)
{
    EvpPkeyPtr key{
        EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                     private_key.data(), private_key.size()) };
    if (!key) return std::nullopt;

    EvpMdCtxPtr ctx{ EVP_MD_CTX_new() };
    if (!ctx) return std::nullopt;
    if (EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) <= 0)
        return std::nullopt;

    Signature sig{};
    size_t sig_len = sig.size();
    if (EVP_DigestSign(ctx.get(), sig.data(), &sig_len,
                       message.data(), message.size()) <= 0)
        return std::nullopt;
    return sig;
}

// ── Ed25519 verify ────────────────────────���──────────────────────────────────

bool ed25519_verify(
    const PublicKey&         public_key,
    std::span<const uint8_t> message,
    const Signature&         signature)
{
    EvpPkeyPtr key{
        EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                    public_key.data(), public_key.size()) };
    if (!key) return false;

    EvpMdCtxPtr ctx{ EVP_MD_CTX_new() };
    if (!ctx) return false;
    if (EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) <= 0)
        return false;

    return EVP_DigestVerify(ctx.get(),
                            signature.data(), signature.size(),
                            message.data(),   message.size()) == 1;
}

// ── Utilities ──────────────���─────────────────────────────────────────────────

std::vector<uint8_t> random_bytes(size_t count) {
    std::vector<uint8_t> buf(count);
    if (RAND_bytes(buf.data(), static_cast<int>(count)) != 1) return {};
    return buf;
}

std::vector<uint8_t> sha256(std::span<const uint8_t> data) {
    std::vector<uint8_t> out(32);
    EvpMdCtxPtr ctx{ EVP_MD_CTX_new() };
    if (!ctx) return {};
    unsigned int len = 32;
    if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) <= 0) return {};
    if (EVP_DigestUpdate(ctx.get(), data.data(), data.size()) <= 0) return {};
    if (EVP_DigestFinal_ex(ctx.get(), out.data(), &len) <= 0) return {};
    return out;
}

std::string to_hex(std::span<const uint8_t> data) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t b : data) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0xF]);
    }
    return out;
}

std::vector<uint8_t> from_hex(const std::string& hex) {
    if (hex.size() % 2 != 0) return {};
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = nibble(hex[i]), lo = nibble(hex[i+1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

} // namespace agentchat::crypto

namespace agentchat::crypto {

static std::optional<PublicKey> pubkey_from_private_nid(int nid, const PrivateKey& priv) {
    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(nid, nullptr, priv.data(), priv.size());
    if (!pkey) return std::nullopt;
    PublicKey pub{};
    size_t pub_len = pub.size();
    bool ok = EVP_PKEY_get_raw_public_key(pkey, pub.data(), &pub_len) > 0;
    EVP_PKEY_free(pkey);
    if (!ok) return std::nullopt;
    return pub;
}

std::optional<PublicKey> ed25519_pubkey_from_private(const PrivateKey& priv) {
    return pubkey_from_private_nid(EVP_PKEY_ED25519, priv);
}

std::optional<PublicKey> x25519_pubkey_from_private(const PrivateKey& priv) {
    return pubkey_from_private_nid(EVP_PKEY_X25519, priv);
}

} // namespace agentchat::crypto (pubkey derivation extension)
