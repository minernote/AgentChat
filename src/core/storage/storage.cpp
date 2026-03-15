#include "storage.h"

#include <sqlite3.h>

#include <chrono>
#include <sstream>
#include <stdexcept>

namespace agentchat::storage {

// ── internal helpers ────────���──────────────────────────────────────────���──────

static void sql_check(int rc, sqlite3* db, const char* ctx) {
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE) {
        throw StorageError(std::string(ctx) + ": " + sqlite3_errmsg(db));
    }
}

static void exec_sql(sqlite3* db, const char* sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown sqlite error";
        sqlite3_free(err);
        throw StorageError("exec_sql: " + msg);
    }
}

static sqlite3_stmt* make_stmt(sqlite3* db, const char* sql) {
    sqlite3_stmt* s = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &s, nullptr);
    sql_check(rc, db, sql);
    return s;
}

static int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static inline uint64_t uid(AgentId id)   { return static_cast<uint64_t>(id); }
static inline uint64_t uid(ChannelId id) { return static_cast<uint64_t>(id); }
static inline uint64_t uid(MessageId id) { return static_cast<uint64_t>(id); }

// ── MessageStore ──────────────────────────────────────���───────────────────────

MessageStore::MessageStore(sqlite3* db) : db_(db) {
    init_schema();
    prepare_stmts();
}

MessageStore::~MessageStore() {
    sqlite3_finalize(stmt_insert_);
    sqlite3_finalize(stmt_channel_);
    sqlite3_finalize(stmt_dm_);
    sqlite3_finalize(stmt_delivered_);
    sqlite3_finalize(stmt_read_);
}

void MessageStore::init_schema() {
    exec_sql(db_,
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id           INTEGER PRIMARY KEY,"
        "  sender_id    INTEGER NOT NULL,"
        "  channel_id   INTEGER NOT NULL DEFAULT 0,"
        "  recipient_id INTEGER NOT NULL DEFAULT 0,"
        "  content      BLOB    NOT NULL DEFAULT X'',"
        "  nonce        BLOB    NOT NULL DEFAULT X'',"
        "  timestamp_ms INTEGER NOT NULL,"
        "  msg_type     INTEGER NOT NULL DEFAULT 1,"
        "  status       INTEGER NOT NULL DEFAULT 1"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_msg_ch ON messages(channel_id,timestamp_ms);"
        "CREATE INDEX IF NOT EXISTS idx_msg_dm ON messages(sender_id,recipient_id,timestamp_ms);"
    );
}

void MessageStore::prepare_stmts() {
    stmt_insert_ = make_stmt(db_,
        "INSERT OR REPLACE INTO messages"
        " (id,sender_id,channel_id,recipient_id,content,nonce,timestamp_ms,msg_type,status)"
        " VALUES (?,?,?,?,?,?,?,?,?)");

    stmt_channel_ = make_stmt(db_,
        "SELECT id,sender_id,channel_id,recipient_id,content,nonce,"
        "       timestamp_ms,msg_type,status"
        " FROM messages"
        " WHERE channel_id=? AND channel_id!=0"
        " ORDER BY timestamp_ms DESC LIMIT ?");

    stmt_dm_ = make_stmt(db_,
        "SELECT id,sender_id,channel_id,recipient_id,content,nonce,"
        "       timestamp_ms,msg_type,status"
        " FROM messages"
        " WHERE channel_id=0"
        "   AND ((sender_id=? AND recipient_id=?) OR (sender_id=? AND recipient_id=?))"
        " ORDER BY timestamp_ms DESC LIMIT ?");

    stmt_delivered_ = make_stmt(db_, "UPDATE messages SET status=2 WHERE id=?");
    stmt_read_      = make_stmt(db_, "UPDATE messages SET status=3 WHERE id=?");
}

Message MessageStore::row_to_message(sqlite3_stmt* s) {
    Message m;
    m.id      = MessageId{static_cast<uint64_t>(sqlite3_column_int64(s, 0))};
    m.from    = AgentId  {static_cast<uint64_t>(sqlite3_column_int64(s, 1))};
    m.channel = ChannelId{static_cast<uint64_t>(sqlite3_column_int64(s, 2))};
    m.to      = AgentId  {static_cast<uint64_t>(sqlite3_column_int64(s, 3))};

    const void* cb = sqlite3_column_blob(s, 4);
    int         cl = sqlite3_column_bytes(s, 4);
    if (cb && cl > 0)
        m.payload.assign(static_cast<const uint8_t*>(cb),
                         static_cast<const uint8_t*>(cb) + cl);

    const void* nb = sqlite3_column_blob(s, 5);
    int         nl = sqlite3_column_bytes(s, 5);
    if (nb && nl > 0)
        m.nonce.assign(static_cast<const uint8_t*>(nb),
                       static_cast<const uint8_t*>(nb) + nl);

    int64_t ms = sqlite3_column_int64(s, 6);
    m.timestamp = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::time_point(std::chrono::milliseconds(ms)));

    m.type   = static_cast<MessageType>  (sqlite3_column_int(s, 7));
    m.status = static_cast<DeliveryStatus>(sqlite3_column_int(s, 8));
    return m;
}

void MessageStore::store_message(const Message& msg) {
    sqlite3_reset(stmt_insert_);
    sqlite3_bind_int64(stmt_insert_, 1, static_cast<int64_t>(uid(msg.id)));
    sqlite3_bind_int64(stmt_insert_, 2, static_cast<int64_t>(uid(msg.from)));
    sqlite3_bind_int64(stmt_insert_, 3, static_cast<int64_t>(uid(msg.channel)));
    sqlite3_bind_int64(stmt_insert_, 4, static_cast<int64_t>(uid(msg.to)));
    sqlite3_bind_blob (stmt_insert_, 5,
        msg.payload.empty() ? nullptr : msg.payload.data(),
        static_cast<int>(msg.payload.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob (stmt_insert_, 6,
        msg.nonce.empty() ? nullptr : msg.nonce.data(),
        static_cast<int>(msg.nonce.size()), SQLITE_TRANSIENT);
    int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        msg.timestamp.time_since_epoch()).count();
    sqlite3_bind_int64(stmt_insert_, 7, ms);
    sqlite3_bind_int  (stmt_insert_, 8, static_cast<int>(msg.type));
    sqlite3_bind_int  (stmt_insert_, 9, static_cast<int>(msg.status));
    int rc = sqlite3_step(stmt_insert_);
    if (rc != SQLITE_DONE) sql_check(rc, db_, "store_message");
}

std::vector<Message> MessageStore::get_messages(ChannelId channel, int limit) {
    sqlite3_reset(stmt_channel_);
    sqlite3_bind_int64(stmt_channel_, 1, static_cast<int64_t>(uid(channel)));
    sqlite3_bind_int  (stmt_channel_, 2, limit);
    std::vector<Message> out;
    int rc;
    while ((rc = sqlite3_step(stmt_channel_)) == SQLITE_ROW)
        out.push_back(row_to_message(stmt_channel_));
    if (rc != SQLITE_DONE) sql_check(rc, db_, "get_messages");
    return out;
}

std::vector<Message> MessageStore::get_dm_history(AgentId a, AgentId b, int limit) {
    sqlite3_reset(stmt_dm_);
    sqlite3_bind_int64(stmt_dm_, 1, static_cast<int64_t>(uid(a)));
    sqlite3_bind_int64(stmt_dm_, 2, static_cast<int64_t>(uid(b)));
    sqlite3_bind_int64(stmt_dm_, 3, static_cast<int64_t>(uid(b)));
    sqlite3_bind_int64(stmt_dm_, 4, static_cast<int64_t>(uid(a)));
    sqlite3_bind_int  (stmt_dm_, 5, limit);
    std::vector<Message> out;
    int rc;
    while ((rc = sqlite3_step(stmt_dm_)) == SQLITE_ROW)
        out.push_back(row_to_message(stmt_dm_));
    if (rc != SQLITE_DONE) sql_check(rc, db_, "get_dm_history");
    return out;
}

void MessageStore::mark_delivered(MessageId id) {
    sqlite3_reset(stmt_delivered_);
    sqlite3_bind_int64(stmt_delivered_, 1, static_cast<int64_t>(uid(id)));
    sqlite3_step(stmt_delivered_);
}

void MessageStore::mark_read(MessageId id) {
    sqlite3_reset(stmt_read_);
    sqlite3_bind_int64(stmt_read_, 1, static_cast<int64_t>(uid(id)));
    sqlite3_step(stmt_read_);
}

// ── AgentRegistry ─────────────���──────────────────────────────────────────���────

AgentRegistry::AgentRegistry(sqlite3* db) : db_(db) {
    init_schema();
    prepare_stmts();
}

AgentRegistry::~AgentRegistry() {
    sqlite3_finalize(stmt_upsert_);
    sqlite3_finalize(stmt_get_);
    sqlite3_finalize(stmt_list_);
    sqlite3_finalize(stmt_last_seen_);
}

void AgentRegistry::init_schema() {
    exec_sql(db_,
        "CREATE TABLE IF NOT EXISTS agents ("
        "  id           INTEGER PRIMARY KEY,"
        "  name         TEXT    NOT NULL DEFAULT '',"
        "  public_key   BLOB    NOT NULL DEFAULT X'',"
        "  last_seen_ms INTEGER NOT NULL DEFAULT 0,"
        "  online       INTEGER NOT NULL DEFAULT 0"
        ");"
    );
}

void AgentRegistry::prepare_stmts() {
    stmt_upsert_ = make_stmt(db_,
        "INSERT INTO agents (id,name,public_key,last_seen_ms,online)"
        " VALUES (?,?,?,?,?)"
        " ON CONFLICT(id) DO UPDATE SET"
        "   name=excluded.name,"
        "   public_key=excluded.public_key,"
        "   last_seen_ms=excluded.last_seen_ms,"
        "   online=excluded.online");

    stmt_get_ = make_stmt(db_,
        "SELECT id,name,public_key,last_seen_ms,online FROM agents WHERE id=?");

    stmt_list_ = make_stmt(db_,
        "SELECT id,name,public_key,last_seen_ms,online FROM agents");

    stmt_last_seen_ = make_stmt(db_,
        "UPDATE agents SET last_seen_ms=?, online=1 WHERE id=?");
}

AgentInfo AgentRegistry::row_to_agent(sqlite3_stmt* s) {
    AgentInfo a;
    a.id = AgentId{static_cast<uint64_t>(sqlite3_column_int64(s, 0))};
    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
    a.name = name ? name : "";

    const void* kb = sqlite3_column_blob(s, 2);
    int         kl = sqlite3_column_bytes(s, 2);
    if (kb && kl == static_cast<int>(a.public_key.size()))
        std::copy(static_cast<const uint8_t*>(kb),
                  static_cast<const uint8_t*>(kb) + kl,
                  a.public_key.begin());

    int64_t ms = sqlite3_column_int64(s, 3);
    a.registered_at = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::time_point(std::chrono::milliseconds(ms)));
    a.online = sqlite3_column_int(s, 4) != 0;
    return a;
}

void AgentRegistry::register_agent(const AgentInfo& info) {
    sqlite3_reset(stmt_upsert_);
    sqlite3_bind_int64(stmt_upsert_, 1, static_cast<int64_t>(uid(info.id)));
    sqlite3_bind_text (stmt_upsert_, 2, info.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob (stmt_upsert_, 3, info.public_key.data(),
                       static_cast<int>(info.public_key.size()), SQLITE_TRANSIENT);
    int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        info.registered_at.time_since_epoch()).count();
    sqlite3_bind_int64(stmt_upsert_, 4, ms);
    sqlite3_bind_int  (stmt_upsert_, 5, info.online ? 1 : 0);
    int rc = sqlite3_step(stmt_upsert_);
    if (rc != SQLITE_DONE) sql_check(rc, db_, "register_agent");
}

std::optional<AgentInfo> AgentRegistry::get_agent(AgentId id) {
    sqlite3_reset(stmt_get_);
    sqlite3_bind_int64(stmt_get_, 1, static_cast<int64_t>(uid(id)));
    int rc = sqlite3_step(stmt_get_);
    if (rc == SQLITE_ROW) return row_to_agent(stmt_get_);
    return std::nullopt;
}

std::vector<AgentInfo> AgentRegistry::list_agents() {
    sqlite3_reset(stmt_list_);
    std::vector<AgentInfo> out;
    int rc;
    while ((rc = sqlite3_step(stmt_list_)) == SQLITE_ROW)
        out.push_back(row_to_agent(stmt_list_));
    return out;
}

void AgentRegistry::update_last_seen(AgentId id) {
    sqlite3_reset(stmt_last_seen_);
    sqlite3_bind_int64(stmt_last_seen_, 1, now_ms());
    sqlite3_bind_int64(stmt_last_seen_, 2, static_cast<int64_t>(uid(id)));
    sqlite3_step(stmt_last_seen_);
}

// ── ChannelStore ───────────────────────────���──────────────────────────────────

ChannelStore::ChannelStore(sqlite3* db) : db_(db) {
    init_schema();
    prepare_stmts();
}

ChannelStore::~ChannelStore() {
    sqlite3_finalize(stmt_insert_ch_);
    sqlite3_finalize(stmt_get_ch_);
    sqlite3_finalize(stmt_add_member_);
    sqlite3_finalize(stmt_del_member_);
    sqlite3_finalize(stmt_members_);
    sqlite3_finalize(stmt_agent_chans_);
}

void ChannelStore::init_schema() {
    exec_sql(db_,
        "CREATE TABLE IF NOT EXISTS channels ("
        "  id   INTEGER PRIMARY KEY,"
        "  name TEXT    NOT NULL,"
        "  type INTEGER NOT NULL DEFAULT 2"
        ");"
        "CREATE TABLE IF NOT EXISTS channel_members ("
        "  channel_id INTEGER NOT NULL,"
        "  agent_id   INTEGER NOT NULL,"
        "  PRIMARY KEY (channel_id, agent_id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_cm_agent ON channel_members(agent_id);"
    );
}

void ChannelStore::prepare_stmts() {
    stmt_insert_ch_ = make_stmt(db_,
        "INSERT OR IGNORE INTO channels (id,name,type) VALUES (?,?,?)");

    stmt_get_ch_ = make_stmt(db_,
        "SELECT id,name,type FROM channels WHERE id=?");

    stmt_add_member_ = make_stmt(db_,
        "INSERT OR IGNORE INTO channel_members (channel_id,agent_id) VALUES (?,?)");

    stmt_del_member_ = make_stmt(db_,
        "DELETE FROM channel_members WHERE channel_id=? AND agent_id=?");

    stmt_members_ = make_stmt(db_,
        "SELECT agent_id FROM channel_members WHERE channel_id=?");

    stmt_agent_chans_ = make_stmt(db_,
        "SELECT c.id,c.name,c.type FROM channels c"
        " JOIN channel_members cm ON cm.channel_id=c.id"
        " WHERE cm.agent_id=?");
}

Channel ChannelStore::row_to_channel(sqlite3_stmt* s) {
    Channel ch;
    ch.id   = ChannelId{static_cast<uint64_t>(sqlite3_column_int64(s, 0))};
    const char* nm = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
    ch.name = nm ? nm : "";
    ch.type = static_cast<ChannelType>(sqlite3_column_int(s, 2));
    return ch;
}

ChannelId ChannelStore::create_channel(const std::string& name,
                                        ChannelType type,
                                        const std::vector<AgentId>& members) {
    // Use current time as a simple unique ID
    uint64_t new_id = static_cast<uint64_t>(now_ms());
    sqlite3_reset(stmt_insert_ch_);
    sqlite3_bind_int64(stmt_insert_ch_, 1, static_cast<int64_t>(new_id));
    sqlite3_bind_text (stmt_insert_ch_, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int  (stmt_insert_ch_, 3, static_cast<int>(type));
    sqlite3_step(stmt_insert_ch_);

    ChannelId cid{new_id};
    for (const auto& agent : members)
        add_member(cid, agent);
    return cid;
}

std::optional<Channel> ChannelStore::get_channel(ChannelId id) {
    sqlite3_reset(stmt_get_ch_);
    sqlite3_bind_int64(stmt_get_ch_, 1, static_cast<int64_t>(uid(id)));
    int rc = sqlite3_step(stmt_get_ch_);
    if (rc != SQLITE_ROW) return std::nullopt;
    Channel ch = row_to_channel(stmt_get_ch_);

    // Populate members
    sqlite3_reset(stmt_members_);
    sqlite3_bind_int64(stmt_members_, 1, static_cast<int64_t>(uid(id)));
    while (sqlite3_step(stmt_members_) == SQLITE_ROW)
        ch.members.push_back(AgentId{static_cast<uint64_t>(
            sqlite3_column_int64(stmt_members_, 0))});
    return ch;
}

std::vector<Channel> ChannelStore::list_channels_for_agent(AgentId agent) {
    sqlite3_reset(stmt_agent_chans_);
    sqlite3_bind_int64(stmt_agent_chans_, 1, static_cast<int64_t>(uid(agent)));
    std::vector<Channel> out;
    int rc;
    while ((rc = sqlite3_step(stmt_agent_chans_)) == SQLITE_ROW) {
        Channel ch = row_to_channel(stmt_agent_chans_);
        // Populate members inline
        sqlite3_stmt* ms = nullptr;
        const char* msql = "SELECT agent_id FROM channel_members WHERE channel_id=?";
        if (sqlite3_prepare_v2(db_, msql, -1, &ms, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(ms, 1, static_cast<int64_t>(uid(ch.id)));
            while (sqlite3_step(ms) == SQLITE_ROW)
                ch.members.push_back(AgentId{static_cast<uint64_t>(
                    sqlite3_column_int64(ms, 0))});
            sqlite3_finalize(ms);
        }
        out.push_back(std::move(ch));
    }
    return out;
}

void ChannelStore::add_member(ChannelId ch, AgentId agent) {
    sqlite3_reset(stmt_add_member_);
    sqlite3_bind_int64(stmt_add_member_, 1, static_cast<int64_t>(uid(ch)));
    sqlite3_bind_int64(stmt_add_member_, 2, static_cast<int64_t>(uid(agent)));
    sqlite3_step(stmt_add_member_);
}

void ChannelStore::remove_member(ChannelId ch, AgentId agent) {
    sqlite3_reset(stmt_del_member_);
    sqlite3_bind_int64(stmt_del_member_, 1, static_cast<int64_t>(uid(ch)));
    sqlite3_bind_int64(stmt_del_member_, 2, static_cast<int64_t>(uid(agent)));
    sqlite3_step(stmt_del_member_);
}

// ── OfflineStore ───────────────────────────────────────────────────────────────

OfflineStore::OfflineStore(sqlite3* db) : db_(db) {
    init_schema();
    prepare_stmts();
}

OfflineStore::~OfflineStore() {
    sqlite3_finalize(stmt_store_);
    sqlite3_finalize(stmt_drain_);
    sqlite3_finalize(stmt_delete_);
    sqlite3_finalize(stmt_purge_);
}

void OfflineStore::init_schema() {
    exec_sql(db_,
        "CREATE TABLE IF NOT EXISTS offline_messages ("
        "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  to_agent   INTEGER NOT NULL,"
        "  from_agent INTEGER NOT NULL,"
        "  payload    BLOB    NOT NULL,"
        "  created_at INTEGER NOT NULL,"
        "  expires_at INTEGER NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_offline_to ON offline_messages(to_agent);"
    );
}

void OfflineStore::prepare_stmts() {
    stmt_store_ = make_stmt(db_,
        "INSERT INTO offline_messages"
        " (to_agent,from_agent,payload,created_at,expires_at)"
        " VALUES (?,?,?,?,?)");

    stmt_drain_ = make_stmt(db_,
        "SELECT id,payload FROM offline_messages"
        " WHERE to_agent=? ORDER BY id ASC");

    stmt_delete_ = make_stmt(db_,
        "DELETE FROM offline_messages WHERE to_agent=?");

    stmt_purge_ = make_stmt(db_,
        "DELETE FROM offline_messages WHERE expires_at<=?");
}

void OfflineStore::store_offline(uint64_t to_agent, uint64_t from_agent,
                                  const std::vector<uint8_t>& recv_pkt,
                                  int ttl_seconds) {
    int64_t created = now_ms();
    // expires_at in seconds (not ms) for simpler comparison
    int64_t expires = created / 1000 + ttl_seconds;

    sqlite3_reset(stmt_store_);
    sqlite3_bind_int64(stmt_store_, 1, static_cast<int64_t>(to_agent));
    sqlite3_bind_int64(stmt_store_, 2, static_cast<int64_t>(from_agent));
    sqlite3_bind_blob (stmt_store_, 3,
        recv_pkt.empty() ? nullptr : recv_pkt.data(),
        static_cast<int>(recv_pkt.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt_store_, 4, created);
    sqlite3_bind_int64(stmt_store_, 5, expires);
    int rc = sqlite3_step(stmt_store_);
    if (rc != SQLITE_DONE) sql_check(rc, db_, "store_offline");
}

std::vector<std::vector<uint8_t>> OfflineStore::drain_offline(uint64_t agent_id) {
    std::vector<std::vector<uint8_t>> out;

    sqlite3_reset(stmt_drain_);
    sqlite3_bind_int64(stmt_drain_, 1, static_cast<int64_t>(agent_id));
    while (sqlite3_step(stmt_drain_) == SQLITE_ROW) {
        const void* b = sqlite3_column_blob(stmt_drain_, 1);
        int         l = sqlite3_column_bytes(stmt_drain_, 1);
        if (b && l > 0)
            out.push_back(std::vector<uint8_t>(
                static_cast<const uint8_t*>(b),
                static_cast<const uint8_t*>(b) + l));
    }

    // Delete all drained rows
    sqlite3_reset(stmt_delete_);
    sqlite3_bind_int64(stmt_delete_, 1, static_cast<int64_t>(agent_id));
    sqlite3_step(stmt_delete_);

    return out;
}

void OfflineStore::purge_expired_offline() {
    int64_t now_sec = now_ms() / 1000;
    sqlite3_reset(stmt_purge_);
    sqlite3_bind_int64(stmt_purge_, 1, now_sec);
    sqlite3_step(stmt_purge_);
}

// ── PrekeyStore ─────────────────────────────────────────────────────────────

PrekeyStore::PrekeyStore(sqlite3* db) : db_(db) {
    init_schema();
    prepare_stmts();
}

PrekeyStore::~PrekeyStore() {
    sqlite3_finalize(stmt_upsert_);
    sqlite3_finalize(stmt_get_);
    sqlite3_finalize(stmt_delete_);
}

void PrekeyStore::init_schema() {
    exec_sql(db_,
        "CREATE TABLE IF NOT EXISTS agent_prekeys ("
        "  agent_id   INTEGER PRIMARY KEY,"
        "  pubkey     BLOB    NOT NULL,"
        "  updated_at INTEGER NOT NULL"
        ");"
    );
}

void PrekeyStore::prepare_stmts() {
    stmt_upsert_ = make_stmt(db_,
        "INSERT INTO agent_prekeys (agent_id, pubkey, updated_at)"
        " VALUES (?,?,?)"
        " ON CONFLICT(agent_id) DO UPDATE SET pubkey=excluded.pubkey, updated_at=excluded.updated_at");
    stmt_get_ = make_stmt(db_,
        "SELECT pubkey FROM agent_prekeys WHERE agent_id=?");
    stmt_delete_ = make_stmt(db_,
        "DELETE FROM agent_prekeys WHERE agent_id=?");
}

void PrekeyStore::store_prekey(uint64_t agent_id, const std::vector<uint8_t>& pubkey) {
    sqlite3_reset(stmt_upsert_);
    sqlite3_bind_int64(stmt_upsert_, 1, static_cast<int64_t>(agent_id));
    sqlite3_bind_blob (stmt_upsert_, 2,
        pubkey.empty() ? nullptr : pubkey.data(),
        static_cast<int>(pubkey.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt_upsert_, 3, now_ms());
    int rc = sqlite3_step(stmt_upsert_);
    if (rc != SQLITE_DONE) sql_check(rc, db_, "store_prekey");
}

std::optional<std::vector<uint8_t>> PrekeyStore::get_prekey(uint64_t agent_id) {
    sqlite3_reset(stmt_get_);
    sqlite3_bind_int64(stmt_get_, 1, static_cast<int64_t>(agent_id));
    if (sqlite3_step(stmt_get_) != SQLITE_ROW) return std::nullopt;
    const void* b = sqlite3_column_blob(stmt_get_, 0);
    int         l = sqlite3_column_bytes(stmt_get_, 0);
    if (!b || l <= 0) return std::nullopt;
    return std::vector<uint8_t>(
        static_cast<const uint8_t*>(b),
        static_cast<const uint8_t*>(b) + l);
}

void PrekeyStore::delete_prekey(uint64_t agent_id) {
    sqlite3_reset(stmt_delete_);
    sqlite3_bind_int64(stmt_delete_, 1, static_cast<int64_t>(agent_id));
    sqlite3_step(stmt_delete_);
}

// ── Database ─────────────────────────────────────────────────────────────────

Database::Database(const std::string& path) {
    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK)
        throw StorageError("sqlite3_open(" + path + "): " + sqlite3_errmsg(db_));

    // WAL mode for concurrent reads + writes
    exec_sql(db_, "PRAGMA journal_mode=WAL;");
    exec_sql(db_, "PRAGMA foreign_keys=ON;");
    exec_sql(db_, "PRAGMA synchronous=NORMAL;");

    msg_       = std::make_unique<MessageStore> (db_);
    agents_    = std::make_unique<AgentRegistry>(db_);
    chans_     = std::make_unique<ChannelStore> (db_);
    reactions_ = std::make_unique<ReactionStore>(db_);
    offline_   = std::make_unique<OfflineStore>  (db_);
    prekeys_   = std::make_unique<PrekeyStore>   (db_);
}

Database::~Database() {
    msg_.reset();
    agents_.reset();
    chans_.reset();
    reactions_.reset();
    offline_.reset();
    if (db_) sqlite3_close(db_);
}

// ── ReactionStore ───────────────────────────────────────────────────────────

ReactionStore::ReactionStore(sqlite3* db) : db_(db) {
    init_schema();
    prepare_stmts();
}

ReactionStore::~ReactionStore() {
    sqlite3_finalize(stmt_check_);
    sqlite3_finalize(stmt_insert_);
    sqlite3_finalize(stmt_delete_);
    sqlite3_finalize(stmt_get_all_);
}

void ReactionStore::init_schema() {
    exec_sql(db_,
        "CREATE TABLE IF NOT EXISTS reactions ("
        "  message_id INTEGER NOT NULL,"
        "  agent_id   INTEGER NOT NULL,"
        "  emoji      TEXT    NOT NULL,"
        "  PRIMARY KEY (message_id, agent_id, emoji)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_rxn_msg ON reactions(message_id);"
    );
}

void ReactionStore::prepare_stmts() {
    stmt_check_ = make_stmt(db_,
        "SELECT 1 FROM reactions WHERE message_id=? AND agent_id=? AND emoji=?");
    stmt_insert_ = make_stmt(db_,
        "INSERT OR IGNORE INTO reactions (message_id,agent_id,emoji) VALUES (?,?,?)");
    stmt_delete_ = make_stmt(db_,
        "DELETE FROM reactions WHERE message_id=? AND agent_id=? AND emoji=?");
    stmt_get_all_ = make_stmt(db_,
        "SELECT agent_id, emoji FROM reactions WHERE message_id=?");
}

bool ReactionStore::toggle_reaction(MessageId msg, AgentId agent, const std::string& emoji) {
    sqlite3_reset(stmt_check_);
    sqlite3_bind_int64(stmt_check_, 1, static_cast<int64_t>(static_cast<uint64_t>(msg)));
    sqlite3_bind_int64(stmt_check_, 2, static_cast<int64_t>(static_cast<uint64_t>(agent)));
    sqlite3_bind_text (stmt_check_, 3, emoji.c_str(), -1, SQLITE_TRANSIENT);
    bool exists = (sqlite3_step(stmt_check_) == SQLITE_ROW);

    if (exists) {
        sqlite3_reset(stmt_delete_);
        sqlite3_bind_int64(stmt_delete_, 1, static_cast<int64_t>(static_cast<uint64_t>(msg)));
        sqlite3_bind_int64(stmt_delete_, 2, static_cast<int64_t>(static_cast<uint64_t>(agent)));
        sqlite3_bind_text (stmt_delete_, 3, emoji.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt_delete_);
        return false;
    } else {
        sqlite3_reset(stmt_insert_);
        sqlite3_bind_int64(stmt_insert_, 1, static_cast<int64_t>(static_cast<uint64_t>(msg)));
        sqlite3_bind_int64(stmt_insert_, 2, static_cast<int64_t>(static_cast<uint64_t>(agent)));
        sqlite3_bind_text (stmt_insert_, 3, emoji.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt_insert_);
        return true;
    }
}

std::map<std::string, std::vector<AgentId>> ReactionStore::get_reactions(MessageId msg) {
    sqlite3_reset(stmt_get_all_);
    sqlite3_bind_int64(stmt_get_all_, 1, static_cast<int64_t>(static_cast<uint64_t>(msg)));
    std::map<std::string, std::vector<AgentId>> result;
    while (sqlite3_step(stmt_get_all_) == SQLITE_ROW) {
        AgentId aid{static_cast<uint64_t>(sqlite3_column_int64(stmt_get_all_, 0))};
        const char* e = reinterpret_cast<const char*>(sqlite3_column_text(stmt_get_all_, 1));
        if (e) result[e].push_back(aid);
    }
    return result;
}

} // namespace agentchat::storage
