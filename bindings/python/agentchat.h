#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * AgentChat C ABI
 * Thin wrapper around agentchat::AgentClient for Python/Node.js/any FFI.
 */

typedef void* AgentHandle;

/**
 * Create a new AgentChat client.
 * @param host              Server hostname, e.g. "localhost"
 * @param port              Server port, e.g. 8765
 * @param agent_id          Numeric agent ID (uint64)
 * @param identity_priv_hex Ed25519 private key, 64 hex chars (32 bytes), or empty string to auto-generate
 * @param exchange_priv_hex X25519 private key, 64 hex chars (32 bytes), or empty string to auto-generate
 */
AgentHandle agentchat_create(const char* host, uint16_t port,
                              uint64_t agent_id,
                              const char* identity_priv_hex,
                              const char* exchange_priv_hex);

/** Connect to server. Returns 1 on success, 0 on failure. */
int agentchat_connect(AgentHandle handle);

/**
 * Send a text message to another agent.
 * @param to_agent_id  Numeric agent ID of recipient
 * @param text         UTF-8 text (not null-terminated length used)
 * @param text_len     Length of text in bytes
 */
int agentchat_send_text(AgentHandle handle, uint64_t to_agent_id,
                         const char* text, size_t text_len);

/**
 * Register callback for incoming messages.
 * @param cb  Called from receive thread with: sender id, raw payload bytes, length, userdata
 */
void agentchat_on_message(AgentHandle handle,
    void (*cb)(uint64_t from, const uint8_t* payload, size_t len, void* ud),
    void* userdata);

/** Disconnect and stop receive thread. */
void agentchat_disconnect(AgentHandle handle);

/** Free all resources. Call after disconnect. */
void agentchat_destroy(AgentHandle handle);

/**
 * Generate a fresh key pair.
 * @param use_ed25519  1 = Ed25519 (signing), 0 = X25519 (key exchange)
 * @param pub_hex      [out] 64-char hex, caller must free with agentchat_free_string()
 * @param priv_hex     [out] 64-char hex, caller must free
 */
void agentchat_generate_keypair(char** pub_hex, char** priv_hex, int use_ed25519);

/** Free a string returned by this API. */
void agentchat_free_string(char* s);

#ifdef __cplusplus
} // extern "C"
#endif
