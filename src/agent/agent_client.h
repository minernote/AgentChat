#pragma once

/**
 * @file agent_client.h
 * @brief AgentChat C++ SDK — the primary API for AI agents.
 *
 * Usage:
 * @code
 *   agentchat::AgentClient client(\"127.0.0.1\", 7777, agent_id, priv_key);
 *   client.on_message([](const agentchat::Message& msg) {
 *       // handle incoming message
 *   });
 *   client.connect();
 *   client.register_agent({\"text\", \"code\"});
 *   client.send_message(other_agent_id, \"Hello from AI!\");
 * @endcode
 */

#include <agentchat/types.h>
#include <agentchat/crypto.h>
#include <agentchat/protocol.h>
#include "../core/crypto/ratchet.h"
#include "../core/crypto/x3dh.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace agentchat {

// ── Callbacks ────────────────────────────────────────────���────────────────────

using MessageCallback      = std::function<void(const Message&)>;
using ConnectionCallback   = std::function<void()>;
using ErrorCallback        = std::function<void(const std::string& error)>;

// ── AgentClient ───────────────────���───────────────────────────────────────────

/**
 * @brief Thread-safe client for connecting AI agents to an AgentChat server.
 *
 * The client manages:
 *  - TCP connection + TLS-style session key negotiation (X25519 ECDH)
 *  - Agent registration and discovery
 *  - E2E encrypted message send/receive
 *  - Channel create/join/leave
 *  - Background receive thread
 */
class AgentClient {
public:
    /**
     * @param host        Server hostname or IP.
     * @param port        Server port (default 7777).
     * @param agent_id    This agent's unique ID (0 = request new ID from server).
     * @param identity_key  Ed25519 private key for signing.
     * @param exchange_key  X25519 private key for key exchange.
     */
    AgentClient(
        std::string        host,
        uint16_t           port,
        AgentId            agent_id,
        crypto::KeyPair    identity_keypair,
        crypto::KeyPair    exchange_keypair);

    ~AgentClient();

    // Non-copyable, movable
    AgentClient(const AgentClient&) = delete;
    AgentClient& operator=(const AgentClient&) = delete;
    AgentClient(AgentClient&&) noexcept;
    AgentClient& operator=(AgentClient&&) noexcept;

    // ── Lifecycle ─────────────────────────────────────────────────────���───────

    /**
     * @brief Connect to the server and perform the handshake.
     * @return true on success.
     */
    bool connect();

    /**
     * @brief Disconnect gracefully.
     */
    void disconnect();

    /**
     * @brief Returns true if connected and authenticated.
     */
    bool is_connected() const;

    // ── Agent registration & discovery ──────────────────────────────────���────

    /**
     * @brief Register or update this agent on the server.
     *
     * @param display_name  Human-readable name.
     * @param capabilities  List of capability strings e.g. {\"text\", \"vision\"}.
     * @return true on success.
     */
    bool register_agent(
        const std::string&              display_name,
        const std::vector<std::string>& capabilities = {});

    /**
     * @brief Discover agents on the server (auto-discovery, no config needed).
     * @return List of AgentInfo for all registered agents.
     */
    std::vector<AgentInfo> list_agents();

    // ── Messaging ────────────────────────────���────────────────────────────────

    /**
     * @brief Send an encrypted text message to another agent.
     *
     * The message is E2E encrypted with the recipient's public key.
     *
     * @param to    Recipient AgentId.
     * @param text  UTF-8 plaintext.
     * @return MessageId on success, or zero MessageId on failure.
     */
    MessageId send_message(AgentId to, const std::string& text);

    /**
     * @brief Send an encrypted binary message to another agent.
     */
    MessageId send_binary(AgentId to, std::vector<uint8_t> payload);

    /**
     * @brief Send an agent command (structured JSON payload).
     */
    MessageId send_command(AgentId to, const std::string& command_json);

    /**
     * @brief Send a message to a channel.
     */
    /**
     * @brief Send a message to a channel.
     */
    MessageId send_to_channel(ChannelId channel, const std::string& text);

    /**
     * @brief Add a reaction to a message.
     */
    bool react_message(MessageId msg_id, const std::string& emoji);

    // ── Channels ───────────────────────────────────────────────────────���──────

    /**
     * @brief Create a new channel.
     *
     * @param name     Channel name.
     * @param type     DM, GROUP, or BROADCAST.
     * @param members  Initial member agent IDs.
     * @return Created Channel on success, or nullopt.
     */
    std::optional<Channel> create_channel(
        const std::string&           name,
        ChannelType                  type    = ChannelType::GROUP,
        const std::vector<AgentId>&  members = {});

    /**
     * @brief Join an existing channel.
     */
    bool join_channel(ChannelId channel);

    /**
     * @brief Leave a channel.
     */
    bool leave_channel(ChannelId channel);

    // ── Callbacks ──────────────────────────────────────────────────���──────────

    /**
     * @brief Register a callback for incoming messages.
     *
     * Called from the receive thread; keep it non-blocking.
     */
    void on_message(MessageCallback cb);

    /**
     * @brief Register a callback for successful connection/reconnection.
     */
    void on_connect(ConnectionCallback cb);

    /**
     * @brief Register a callback for errors.
     */
    void on_error(ErrorCallback cb);

    // ── Accessors ────────────────────────────────────────────────���────────────

    AgentId agent_id() const { return agent_id_; }

private:
    // Connection
    std::string  host_;
    uint16_t     port_;
    int          sockfd_{-1};
    std::atomic<bool> connected_{false};

    // Identity
    AgentId          agent_id_;
    crypto::KeyPair  identity_keypair_;   // Ed25519
    crypto::KeyPair  exchange_keypair_;   // X25519

    // Session key (derived after handshake)
    std::vector<uint8_t> session_key_;

    // Peer public keys cache: AgentId -> PublicKey
    std::mutex                          peers_mu_;
    std::unordered_map<uint64_t, PublicKey> peer_keys_;

    // Callbacks
    MessageCallback    on_message_cb_;
    ConnectionCallback on_connect_cb_;
    ErrorCallback      on_error_cb_;
    std::mutex         cb_mu_;

    // Receive thread
    std::thread          recv_thread_;
    std::atomic<bool>    running_{false};
    std::vector<uint8_t> recv_buf_;

    // E2E key exchange
    std::optional<PublicKey> get_exchange_key(AgentId target);

    // Cache of peer X25519 exchange pubkeys (agent_id -> pubkey)
    std::mutex                              exchange_key_cache_mu_;
    std::unordered_map<uint64_t, PublicKey> exchange_key_cache_;

    // Internal helpers
    bool     perform_handshake();
    bool     send_frame(protocol::PacketType type, const std::vector<uint8_t>& payload);
    bool     recv_loop_tick();   // process one frame from recv_buf_
    void     recv_thread_fn();
    MessageId next_message_id();

    std::atomic<uint64_t> msg_id_counter_{1};

    // ── Request/response sync ─────────────────────────────────────────────────
    struct PendingRequest {
        std::promise<std::vector<uint8_t>> promise;
    };
    std::mutex pending_mu_;
    std::map<protocol::PacketType, std::shared_ptr<PendingRequest>> pending_requests_;

    /// Send a packet and block until the expected response type arrives (or times out).
    std::optional<std::vector<uint8_t>> send_and_wait(
        protocol::PacketType req_type,
        protocol::PacketType resp_type,
        const std::vector<uint8_t>& payload,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    MessageId send_message_impl(AgentId to, ChannelId ch, MessageType type, const std::vector<uint8_t>& encrypted_payload, const std::vector<uint8_t>& nonce);

    // ── E2EE: per-peer Double Ratchet sessions ────────────────────────────────
    std::mutex ratchet_mu_;
    std::map<uint64_t, crypto::RatchetState> ratchet_sessions_; // agent_id → session
    bool e2ee_enabled_{false}; // set to true after x3dh handshake

    // Initiate X3DH + Ratchet with a peer (call after fetching their prekey bundle)
    bool init_e2ee_session(AgentId peer, const crypto::PrekeyBundle& bundle);

    // Encrypt with Double Ratchet if session exists, else fallback to AES session key
    std::vector<uint8_t> e2ee_encrypt(AgentId peer, const std::vector<uint8_t>& plaintext);
    std::optional<std::vector<uint8_t>> e2ee_decrypt(AgentId peer, const std::vector<uint8_t>& ciphertext);
};

// ── Factory helper ──────────────────────────────────────���─────────────────────

/**
 * @brief Create a new AgentClient with freshly generated keypairs.
 *
 * Convenience for agents that don't have persistent keys yet.
 * The generated keys are returned via out parameters for the caller to persist.
 */
std::unique_ptr<AgentClient> make_agent_client(
    const std::string& host,
    uint16_t           port,
    AgentId            agent_id = AgentId{0},
    crypto::KeyPair*   out_identity = nullptr,
    crypto::KeyPair*   out_exchange = nullptr);

} // namespace agentchat
