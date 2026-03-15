#include "acl.h"

#include <sqlite3.h>
#include <stdexcept>
#include <string>

namespace agentchat::server {

// ── helpers ─────────────────────────���─────────────────────────────────────────

static void acl_check(int rc, sqlite3* db, const char* ctx) {
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE)
        throw std::runtime_error(std::string(ctx) + ": " + sqlite3_errmsg(db));
}

static void acl_exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error(std::string("acl_exec: ") + msg);
    }
}

static sqlite3_stmt* acl_stmt(sqlite3* db, const char* sql) {
    sqlite3_stmt* s = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &s, nullptr);
    acl_check(rc, db, sql);
    return s;
}

static inline int64_t ai(AgentId id)   { return static_cast<int64_t>(static_cast<uint64_t>(id)); }
static inline int64_t ci(ChannelId id) { return static_cast<int64_t>(static_cast<uint64_t>(id)); }

// ── Acl ─────────────────────────────────────���─────────────────────────────────

Acl::Acl(sqlite3* db) : db_(db) {
    init_schema();
    prepare_stmts();
}

Acl::~Acl() {
    sqlite3_finalize(stmt_insert_rule_);
    sqlite3_finalize(stmt_delete_rule_);
    sqlite3_finalize(stmt_list_rules_);
    sqlite3_finalize(stmt_deny_check_);
    sqlite3_finalize(stmt_wl_check_);
    sqlite3_finalize(stmt_allow_check_);
}

void Acl::init_schema() {
    acl_exec(db_,
        "CREATE TABLE IF NOT EXISTS acl_rules ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  rule_type   INTEGER NOT NULL,"
        "  scope       INTEGER NOT NULL,"
        "  from_agent  INTEGER NOT NULL DEFAULT 0,"
        "  to_agent    INTEGER NOT NULL DEFAULT 0,"
        "  channel_id  INTEGER NOT NULL DEFAULT 0,"
        "  comment     TEXT    NOT NULL DEFAULT ''"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_acl_from ON acl_rules(from_agent);"
        "CREATE INDEX IF NOT EXISTS idx_acl_ch   ON acl_rules(channel_id);"
    );
}

void Acl::prepare_stmts() {
    stmt_insert_rule_ = acl_stmt(db_,
        "INSERT OR REPLACE INTO acl_rules"
        " (id,rule_type,scope,from_agent,to_agent,channel_id,comment)"
        " VALUES (CASE WHEN ?1=0 THEN NULL ELSE ?1 END,?2,?3,?4,?5,?6,?7)");

    stmt_delete_rule_ = acl_stmt(db_,
        "DELETE FROM acl_rules WHERE id=?");

    stmt_list_rules_ = acl_stmt(db_,
        "SELECT id,rule_type,scope,from_agent,to_agent,channel_id,comment"
        " FROM acl_rules ORDER BY id");

    // DENY check: any DENY rule matching from_agent (or global), or to_agent, or channel
    stmt_deny_check_ = acl_stmt(db_,
        "SELECT 1 FROM acl_rules"
        " WHERE rule_type=2"
        "   AND (from_agent=0 OR from_agent=?1)"
        "   AND (to_agent=0   OR to_agent=?2)"
        "   AND (channel_id=0 OR channel_id=?3)"
        " LIMIT 1");

    // WHITELIST_ONLY check: is there a WL rule for this channel with no ALLOW for from_agent?
    // Step 1 — does a WHITELIST_ONLY rule exist for this channel?
    stmt_wl_check_ = acl_stmt(db_,
        "SELECT 1 FROM acl_rules"
        " WHERE rule_type=3 AND (channel_id=?1 OR (channel_id=0 AND scope=1))"
        " LIMIT 1");

    // ALLOW check: explicit ALLOW for this from+to+channel combo
    stmt_allow_check_ = acl_stmt(db_,
        "SELECT 1 FROM acl_rules"
        " WHERE rule_type=1"
        "   AND (from_agent=0 OR from_agent=?1)"
        "   AND (to_agent=0   OR to_agent=?2)"
        "   AND (channel_id=0 OR channel_id=?3)"
        " LIMIT 1");
}

AclRule Acl::row_to_rule(sqlite3_stmt* s) {
    AclRule r;
    r.id        = sqlite3_column_int64(s, 0);
    r.rule_type = static_cast<AclRuleType>(sqlite3_column_int(s, 1));
    r.scope     = static_cast<AclScope>   (sqlite3_column_int(s, 2));
    r.from_agent= AgentId {static_cast<uint64_t>(sqlite3_column_int64(s, 3))};
    r.to_agent  = AgentId {static_cast<uint64_t>(sqlite3_column_int64(s, 4))};
    r.channel   = ChannelId{static_cast<uint64_t>(sqlite3_column_int64(s, 5))};
    const char* cmt = reinterpret_cast<const char*>(sqlite3_column_text(s, 6));
    r.comment = cmt ? cmt : "";
    return r;
}

bool Acl::is_allowed(AgentId from, AgentId to, ChannelId channel) {
    // 1. DENY check
    sqlite3_reset(stmt_deny_check_);
    sqlite3_bind_int64(stmt_deny_check_, 1, ai(from));
    sqlite3_bind_int64(stmt_deny_check_, 2, ai(to));
    sqlite3_bind_int64(stmt_deny_check_, 3, ci(channel));
    if (sqlite3_step(stmt_deny_check_) == SQLITE_ROW)
        return false;

    // 2. WHITELIST_ONLY check for channel
    if (static_cast<uint64_t>(channel) != 0) {
        sqlite3_reset(stmt_wl_check_);
        sqlite3_bind_int64(stmt_wl_check_, 1, ci(channel));
        if (sqlite3_step(stmt_wl_check_) == SQLITE_ROW) {
            // Channel is whitelist-only — must have explicit ALLOW
            sqlite3_reset(stmt_allow_check_);
            sqlite3_bind_int64(stmt_allow_check_, 1, ai(from));
            sqlite3_bind_int64(stmt_allow_check_, 2, ai(to));
            sqlite3_bind_int64(stmt_allow_check_, 3, ci(channel));
            return sqlite3_step(stmt_allow_check_) == SQLITE_ROW;
        }
    }

    // 3. Default: allow
    return true;
}

void Acl::set_rule(const AclRule& rule) {
    sqlite3_reset(stmt_insert_rule_);
    sqlite3_bind_int64(stmt_insert_rule_, 1, rule.id);
    sqlite3_bind_int  (stmt_insert_rule_, 2, static_cast<int>(rule.rule_type));
    sqlite3_bind_int  (stmt_insert_rule_, 3, static_cast<int>(rule.scope));
    sqlite3_bind_int64(stmt_insert_rule_, 4, ai(rule.from_agent));
    sqlite3_bind_int64(stmt_insert_rule_, 5, ai(rule.to_agent));
    sqlite3_bind_int64(stmt_insert_rule_, 6, ci(rule.channel));
    sqlite3_bind_text (stmt_insert_rule_, 7, rule.comment.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt_insert_rule_);
    if (rc != SQLITE_DONE) acl_check(rc, db_, "set_rule");
}

void Acl::remove_rule(int64_t rule_id) {
    sqlite3_reset(stmt_delete_rule_);
    sqlite3_bind_int64(stmt_delete_rule_, 1, rule_id);
    sqlite3_step(stmt_delete_rule_);
}

std::vector<AclRule> Acl::list_rules() {
    sqlite3_reset(stmt_list_rules_);
    std::vector<AclRule> out;
    while (sqlite3_step(stmt_list_rules_) == SQLITE_ROW)
        out.push_back(row_to_rule(stmt_list_rules_));
    return out;
}

void Acl::deny_agent(AgentId from) {
    AclRule r;
    r.rule_type  = AclRuleType::DENY;
    r.scope      = AclScope::PER_AGENT;
    r.from_agent = from;
    r.comment    = "auto-deny";
    set_rule(r);
}

void Acl::whitelist_channel(ChannelId channel) {
    AclRule r;
    r.rule_type = AclRuleType::WHITELIST_ONLY;
    r.scope     = AclScope::PER_CHANNEL;
    r.channel   = channel;
    r.comment   = "whitelist-only";
    set_rule(r);
}

void Acl::allow_agent_in_channel(AgentId from, ChannelId channel) {
    AclRule r;
    r.rule_type  = AclRuleType::ALLOW;
    r.scope      = AclScope::PER_CHANNEL;
    r.from_agent = from;
    r.channel    = channel;
    r.comment    = "whitelist-member";
    set_rule(r);
}

} // namespace agentchat::server
