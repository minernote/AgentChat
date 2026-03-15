#pragma once
/**
 * @file acl.h
 * @brief Access Control List engine for AgentChat server.
 */

#include <agentchat/types.h>
#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace agentchat::server {

enum class AclRuleType : int {
    ALLOW          = 1,
    DENY           = 2,
    WHITELIST_ONLY = 3,
};

enum class AclScope : int {
    GLOBAL      = 0,
    PER_AGENT   = 1,
    PER_CHANNEL = 2,
};

struct AclRule {
    int64_t     id{0};           ///< 0 = auto-assign
    AclRuleType rule_type{AclRuleType::ALLOW};
    AclScope    scope{AclScope::GLOBAL};
    AgentId     from_agent{0};   ///< 0 = wildcard
    AgentId     to_agent{0};     ///< 0 = wildcard
    ChannelId   channel{0};      ///< 0 = wildcard
    std::string comment;
};

class Acl {
public:
    explicit Acl(sqlite3* db);
    ~Acl();

    Acl(const Acl&)            = delete;
    Acl& operator=(const Acl&) = delete;

    /** Returns true if the from→to message on channel is permitted. */
    bool is_allowed(AgentId from, AgentId to, ChannelId channel);

    /** Insert or replace a rule. */
    void set_rule(const AclRule& rule);

    /** Delete a rule by its id. */
    void remove_rule(int64_t rule_id);

    /** List all rules ordered by id. */
    std::vector<AclRule> list_rules();

    // ── Convenience helpers ────────���─────────────────────────────────────────

    /** Add a global DENY for from_agent (any destination). */
    void deny_agent(AgentId from);

    /** Mark a channel as whitelist-only (default-deny). */
    void whitelist_channel(ChannelId channel);

    /** Add an explicit ALLOW for from_agent in a whitelist-only channel. */
    void allow_agent_in_channel(AgentId from, ChannelId channel);

private:
    sqlite3*      db_;
    sqlite3_stmt* stmt_insert_rule_  = nullptr;
    sqlite3_stmt* stmt_delete_rule_  = nullptr;
    sqlite3_stmt* stmt_list_rules_   = nullptr;
    sqlite3_stmt* stmt_deny_check_   = nullptr;
    sqlite3_stmt* stmt_wl_check_     = nullptr;
    sqlite3_stmt* stmt_allow_check_  = nullptr;

    void     init_schema();
    void     prepare_stmts();
    AclRule  row_to_rule(sqlite3_stmt* s);
};

} // namespace agentchat::server
