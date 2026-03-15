#pragma once
/**
 * @file storage.h
 * @brief SQLite-backed storage layer for AgentChat.
 *
 * Three stores share a single sqlite3* connection owned by Database.
 * All public methods are thread-safe via WAL mode + a shared mutex.
 */

#include <agentchat/types.h>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace agentchat::storage {

class StorageError : public std::runtime_error {
public:
    explicit StorageError(const std::string& m) : std::runtime_error(m) {}
};

// ── MessageStore ─────────────────────────────────────────────────────────���────

class MessageStore {
public:
    explicit MessageStore(sqlite3* db);
    ~MessageStore();

    void                  store_message(const Message& msg);
    std::vector<Message>  get_messages(ChannelId channel, int limit = 50);
    std::vector<Message>  get_dm_history(AgentId a, AgentId b, int limit = 50);
    void                  mark_delivered(MessageId id);
    void                  mark_read(MessageId id);

private:
    sqlite3*      db_;
    sqlite3_stmt* stmt_insert_    = nullptr;
    sqlite3_stmt* stmt_channel_   = nullptr;
    sqlite3_stmt* stmt_dm_        = nullptr;
    sqlite3_stmt* stmt_delivered_ = nullptr;
    sqlite3_stmt* stmt_read_      = nullptr;

    void    init_schema();
    void    prepare_stmts();
    Message row_to_message(sqlite3_stmt* s);
};

// ── AgentRegistry ─────────���───────────────────────────────────────────────────

class AgentRegistry {
public:
    explicit AgentRegistry(sqlite3* db);
    ~AgentRegistry();

    void                       register_agent(const AgentInfo& info);
    std::optional<AgentInfo>   get_agent(AgentId id);
    std::vector<AgentInfo>     list_agents();
    void                       update_last_seen(AgentId id);

private:
    sqlite3*      db_;
    sqlite3_stmt* stmt_upsert_    = nullptr;
    sqlite3_stmt* stmt_get_       = nullptr;
    sqlite3_stmt* stmt_list_      = nullptr;
    sqlite3_stmt* stmt_last_seen_ = nullptr;

    void      init_schema();
    void      prepare_stmts();
    AgentInfo row_to_agent(sqlite3_stmt* s);
};

// ── ChannelStore ──────���───────────────────────────────────────────────────────

class ChannelStore {
public:
    explicit ChannelStore(sqlite3* db);
    ~ChannelStore();

    ChannelId               create_channel(const std::string& name,
                                           ChannelType type,
                                           const std::vector<AgentId>& members);
    std::optional<Channel>  get_channel(ChannelId id);
    std::vector<Channel>    list_channels_for_agent(AgentId agent);
    void                    add_member(ChannelId ch, AgentId agent);
    void                    remove_member(ChannelId ch, AgentId agent);

private:
    sqlite3*      db_;
    sqlite3_stmt* stmt_insert_ch_   = nullptr;
    sqlite3_stmt* stmt_get_ch_      = nullptr;
    sqlite3_stmt* stmt_add_member_  = nullptr;
    sqlite3_stmt* stmt_del_member_  = nullptr;
    sqlite3_stmt* stmt_members_     = nullptr;
    sqlite3_stmt* stmt_agent_chans_ = nullptr;

    void    init_schema();
    void    prepare_stmts();
    Channel row_to_channel(sqlite3_stmt* s);
};

// ── ReactionStore ────────────────────────────────���───────────────────────────

struct Reaction {
    MessageId   message_id;
    AgentId     agent_id;
    std::string emoji;
};

class ReactionStore {
public:
    explicit ReactionStore(sqlite3* db);
    ~ReactionStore();

    // Toggle: adds if not present, removes if already present. Returns true if added.
    bool toggle_reaction(MessageId msg, AgentId agent, const std::string& emoji);
    // Returns map of emoji -> list of agent_ids
    std::map<std::string, std::vector<AgentId>> get_reactions(MessageId msg);

private:
    sqlite3*      db_;
    sqlite3_stmt* stmt_check_   = nullptr;
    sqlite3_stmt* stmt_insert_  = nullptr;
    sqlite3_stmt* stmt_delete_  = nullptr;
    sqlite3_stmt* stmt_get_all_ = nullptr;

    void init_schema();
    void prepare_stmts();
};

// ── OfflineStore ──────────────────────────────────────────────────────────────
// Stores messages destined for offline agents; drains on reconnect.

class OfflineStore {
public:
    explicit OfflineStore(sqlite3* db);
    ~OfflineStore();

    // Store a raw RECV_MESSAGE frame for later delivery.
    void store_offline(uint64_t to_agent, uint64_t from_agent,
                       const std::vector<uint8_t>& recv_pkt,
                       int ttl_seconds = 86400);

    // Return and delete all pending frames for agent_id.
    std::vector<std::vector<uint8_t>> drain_offline(uint64_t agent_id);

    // Delete rows where expires_at <= now.
    void purge_expired_offline();

private:
    sqlite3*      db_;
    sqlite3_stmt* stmt_store_  = nullptr;
    sqlite3_stmt* stmt_drain_  = nullptr;
    sqlite3_stmt* stmt_delete_ = nullptr;
    sqlite3_stmt* stmt_purge_  = nullptr;

    void init_schema();
    void prepare_stmts();
};

// ── PrekeyStore ──────────────────────────────────────────────────────────────
// Stores one X25519 prekey per agent for offline message encryption.
// The sender fetches the prekey via GET_EXCHANGE_KEY; the server serves it
// even when the target agent is offline.

class PrekeyStore {
public:
    explicit PrekeyStore(sqlite3* db);
    ~PrekeyStore();

    // Store or replace the prekey for an agent (32-byte X25519 public key).
    void store_prekey(uint64_t agent_id, const std::vector<uint8_t>& pubkey);

    // Return the prekey for an agent, or nullopt if not uploaded yet.
    std::optional<std::vector<uint8_t>> get_prekey(uint64_t agent_id);

    // Remove the prekey (e.g. after use or agent deregistration).
    void delete_prekey(uint64_t agent_id);

private:
    sqlite3*      db_;
    sqlite3_stmt* stmt_upsert_ = nullptr;
    sqlite3_stmt* stmt_get_    = nullptr;
    sqlite3_stmt* stmt_delete_ = nullptr;

    void init_schema();
    void prepare_stmts();
};

// ── Database ──────────────────────────────────────────────────────────────
// Owns the sqlite3 connection; vends the three stores.

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    // Non-copyable, non-movable
    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;

    MessageStore&  messages()  { return *msg_;   }
    AgentRegistry& agents()    { return *agents_; }
    ChannelStore&  channels()  { return *chans_;  }
    ReactionStore& reactions() { return *reactions_; }
    OfflineStore&  offline()   { return *offline_; }
    PrekeyStore&   prekeys()   { return *prekeys_; }
    std::mutex&    mutex()     { return mtx_;     }

private:
    sqlite3*                       db_    = nullptr;
    std::mutex                     mtx_;
    std::unique_ptr<MessageStore>  msg_;
    std::unique_ptr<AgentRegistry> agents_;
    std::unique_ptr<ChannelStore>  chans_;
    std::unique_ptr<ReactionStore> reactions_;
    std::unique_ptr<OfflineStore>  offline_;
    std::unique_ptr<PrekeyStore>   prekeys_;
};

} // namespace agentchat::storage
