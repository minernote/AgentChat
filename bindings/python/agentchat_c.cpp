// C ABI wrapper around agentchat::AgentClient
#include "agentchat.h"
#include "agent_client.h"
#include "agentchat/crypto.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include <stdint.h>

using namespace agentchat;

// Helper: array<uint8_t,32> to hex string
static std::string arr_to_hex(const std::array<uint8_t, 32>& arr) {
    char buf[65]; buf[64] = 0;
    for (int i = 0; i < 32; i++) snprintf(buf + i*2, 3, "%02x", arr[i]);
    return std::string(buf);
}

// Helper: hex string to array<uint8_t,32>
static std::array<uint8_t, 32> hex_to_arr(const char* hex) {
    std::array<uint8_t, 32> arr{};
    for (int i = 0; i < 32 && hex[i*2]; i++)
        arr[i] = (uint8_t)std::stoul(std::string(hex + i*2, 2), nullptr, 16);
    return arr;
}

struct CWrapper {
    AgentClient* client = nullptr;
    void (*c_callback)(uint64_t from, const uint8_t* payload, size_t len, void* ud) = nullptr;
    void* userdata = nullptr;
};

extern "C" {

// Create: agent_id is uint64, key hex strings are 64 hex chars (32 bytes)
AgentHandle agentchat_create(const char* host, uint16_t port,
                              uint64_t agent_id,
                              const char* identity_priv_hex,
                              const char* exchange_priv_hex) {
    if (!host || !identity_priv_hex || !exchange_priv_hex) return nullptr;
    try {
        auto id_kp = crypto::generate_ed25519_keypair();
        auto ex_kp = crypto::generate_x25519_keypair();
        if (!id_kp || !ex_kp) return nullptr;
        // Override private keys if provided (non-empty hex), and derive matching public keys
        if (strlen(identity_priv_hex) == 64) {
            id_kp->priv = hex_to_arr(identity_priv_hex);
            auto pub = crypto::ed25519_pubkey_from_private(id_kp->priv);
            if (!pub) return nullptr;
            id_kp->pub = *pub;
        }
        if (strlen(exchange_priv_hex) == 64) {
            ex_kp->priv = hex_to_arr(exchange_priv_hex);
            auto pub = crypto::x25519_pubkey_from_private(ex_kp->priv);
            if (!pub) return nullptr;
            ex_kp->pub = *pub;
        }
        auto* w = new CWrapper();
        w->client = new AgentClient(
            std::string(host), port,
            AgentId(agent_id),
            std::move(*id_kp),
            std::move(*ex_kp)
        );
        return w;
    } catch (...) { return nullptr; }
}

int agentchat_connect(AgentHandle handle) {
    if (!handle) return 0;
    try { return static_cast<CWrapper*>(handle)->client->connect() ? 1 : 0; }
    catch (...) { return 0; }
}

// send plaintext — AgentClient encrypts internally
int agentchat_send_text(AgentHandle handle, uint64_t to_agent_id,
                         const char* text, size_t text_len) {
    if (!handle || !text) return 0;
    try {
        auto* w = static_cast<CWrapper*>(handle);
        w->client->send_message(AgentId(to_agent_id),
                                std::string(text, text_len));
        return 1;
    } catch (...) { return 0; }
}

uint64_t agentchat_create_channel(AgentHandle handle, const char* name) {
    if (!handle || !name) return 0;
    try {
        auto* w = static_cast<CWrapper*>(handle);
        auto ch = w->client->create_channel(std::string(name));
        return ch ? static_cast<uint64_t>(ch->id) : 0;
    } catch (...) { return 0; }
}

int agentchat_join_channel(AgentHandle handle, uint64_t channel_id) {
    if (!handle) return 0;
    try {
        auto* w = static_cast<CWrapper*>(handle);
        return w->client->join_channel(ChannelId(channel_id)) ? 1 : 0;
    } catch (...) { return 0; }
}

int agentchat_leave_channel(AgentHandle handle, uint64_t channel_id) {
    if (!handle) return 0;
    try {
        auto* w = static_cast<CWrapper*>(handle);
        return w->client->leave_channel(ChannelId(channel_id)) ? 1 : 0;
    } catch (...) { return 0; }
}

int agentchat_send_to_channel(AgentHandle handle, uint64_t channel_id, const char* text, size_t text_len) {
    if (!handle || !text) return 0;
    try {
        auto* w = static_cast<CWrapper*>(handle);
        return w->client->send_to_channel(ChannelId(channel_id), std::string(text, text_len)) ? 1 : 0;
    } catch (...) { return 0; }
}

int agentchat_react_message(AgentHandle handle, uint64_t message_id, const char* emoji) {
    if (!handle || !emoji) return 0;
    try {
        auto* w = static_cast<CWrapper*>(handle);
        return w->client->react_message(MessageId(message_id), std::string(emoji)) ? 1 : 0;
    } catch (...) { return 0; }
}

void agentchat_on_message(AgentHandle handle,
    void (*cb)(uint64_t from, const uint8_t* payload, size_t len, void* ud),
    void* userdata) {
    if (!handle) return;
    auto* w = static_cast<CWrapper*>(handle);
    w->c_callback = cb;
    w->userdata = userdata;
    if (cb) {
        w->client->on_message([w](const Message& msg) {
            if (w->c_callback)
                w->c_callback(static_cast<uint64_t>(msg.from),
                              msg.payload.data(), msg.payload.size(),
                              w->userdata);
        });
    }
}

void agentchat_disconnect(AgentHandle handle) {
    if (!handle) return;
    try { static_cast<CWrapper*>(handle)->client->disconnect(); } catch (...) {}
}

void agentchat_destroy(AgentHandle handle) {
    if (!handle) return;
    auto* w = static_cast<CWrapper*>(handle);
    delete w->client;
    delete w;
}

void agentchat_generate_keypair(char** pub_hex, char** priv_hex, int use_ed25519) {
    if (!pub_hex || !priv_hex) return;
    auto kp = use_ed25519 ? crypto::generate_ed25519_keypair()
                          : crypto::generate_x25519_keypair();
    if (!kp) { *pub_hex = nullptr; *priv_hex = nullptr; return; }
    *pub_hex  = strdup(arr_to_hex(kp->pub).c_str());
    *priv_hex = strdup(arr_to_hex(kp->priv).c_str());
}

void agentchat_free_string(char* s) { free(s); }

} // extern "C"
