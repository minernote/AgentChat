/**
 * @file tests/test_acl.cpp
 * @brief Unit tests for the ACL engine.
 */

#include "../src/server/acl.h"
#include <agentchat/types.h>

#include <iostream>
#include <sqlite3.h>
#include <stdexcept>
#include <string>

using namespace agentchat;
using namespace agentchat::server;

// ── Test harness ──────────────────────────────────────────────────────────────

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond) \
    do { if (!(cond)) throw std::runtime_error( \
        std::string("ASSERT failed: " #cond " at L") + std::to_string(__LINE__)); \
    } while(0)

#define RUN(name) \
    do { \
        std::cout << "  " #name " ... " << std::flush; \
        try { test_##name(); std::cout << "PASS\n"; ++g_pass; } \
        catch (const std::exception& e) { \
            std::cout << "FAIL: " << e.what() << "\n"; ++g_fail; } \
    } while(0)

// ── Fixture ─────────────────────────────���─────────────────────────────────────

struct AclFixture {
    sqlite3* db{nullptr};
    Acl*     acl{nullptr};

    AclFixture() {
        if (sqlite3_open(":memory:", &db) != SQLITE_OK)
            throw std::runtime_error("sqlite3_open failed");
        acl = new Acl(db);
    }
    ~AclFixture() {
        delete acl;
        sqlite3_close(db);
    }
};

// ── Tests ──────────────────────────────────────────────────────���──────────────

static void test_default_allow() {
    AclFixture f;
    ASSERT(f.acl->is_allowed(AgentId{1}, AgentId{2}, ChannelId{0}));
    ASSERT(f.acl->is_allowed(AgentId{99}, AgentId{100}, ChannelId{42}));
}

static void test_deny_specific_agent() {
    AclFixture f;
    f.acl->deny_agent(AgentId{5});
    ASSERT(!f.acl->is_allowed(AgentId{5}, AgentId{1}, ChannelId{0}));
    ASSERT( f.acl->is_allowed(AgentId{6}, AgentId{1}, ChannelId{0}));
}

static void test_deny_does_not_affect_other_agents() {
    AclFixture f;
    f.acl->deny_agent(AgentId{10});
    ASSERT(!f.acl->is_allowed(AgentId{10}, AgentId{2}, ChannelId{0}));
    ASSERT( f.acl->is_allowed(AgentId{11}, AgentId{2}, ChannelId{0}));
    ASSERT( f.acl->is_allowed(AgentId{12}, AgentId{3}, ChannelId{0}));
}

static void test_whitelist_only_blocks_unlisted() {
    AclFixture f;
    ChannelId ch{100};
    f.acl->whitelist_channel(ch);
    ASSERT(!f.acl->is_allowed(AgentId{7}, AgentId{0}, ch));
}

static void test_whitelist_only_allows_listed() {
    AclFixture f;
    ChannelId ch{200};
    f.acl->whitelist_channel(ch);
    f.acl->allow_agent_in_channel(AgentId{8}, ch);
    ASSERT( f.acl->is_allowed(AgentId{8}, AgentId{0}, ch));
    ASSERT(!f.acl->is_allowed(AgentId{9}, AgentId{0}, ch));
}

static void test_remove_rule_restores_access() {
    AclFixture f;
    f.acl->deny_agent(AgentId{3});
    ASSERT(!f.acl->is_allowed(AgentId{3}, AgentId{1}, ChannelId{0}));

    auto rules = f.acl->list_rules();
    ASSERT(!rules.empty());
    int64_t rule_id = rules[0].id;

    f.acl->remove_rule(rule_id);
    ASSERT(f.acl->is_allowed(AgentId{3}, AgentId{1}, ChannelId{0}));
}

static void test_list_rules_returns_all() {
    AclFixture f;
    f.acl->deny_agent(AgentId{1});
    f.acl->deny_agent(AgentId{2});
    f.acl->whitelist_channel(ChannelId{10});
    f.acl->allow_agent_in_channel(AgentId{3}, ChannelId{10});

    auto rules = f.acl->list_rules();
    ASSERT(rules.size() == 4);
}

static void test_deny_and_whitelist_combined() {
    AclFixture f;
    ChannelId ch{300};
    f.acl->deny_agent(AgentId{20});
    f.acl->whitelist_channel(ch);
    f.acl->allow_agent_in_channel(AgentId{21}, ch);

    ASSERT(!f.acl->is_allowed(AgentId{20}, AgentId{0}, ch));
    ASSERT( f.acl->is_allowed(AgentId{21}, AgentId{0}, ch));
    ASSERT(!f.acl->is_allowed(AgentId{22}, AgentId{0}, ch));
}

static void test_global_deny_wildcard_from() {
    AclFixture f;
    AclRule r;
    r.rule_type  = AclRuleType::DENY;
    r.scope      = AclScope::PER_AGENT;
    r.from_agent = AgentId{0};   // wildcard: any sender
    r.to_agent   = AgentId{99};
    r.channel    = ChannelId{0};
    r.comment    = "block all to 99";
    f.acl->set_rule(r);

    ASSERT(!f.acl->is_allowed(AgentId{1},  AgentId{99}, ChannelId{0}));
    ASSERT(!f.acl->is_allowed(AgentId{42}, AgentId{99}, ChannelId{0}));
    ASSERT( f.acl->is_allowed(AgentId{1},  AgentId{98}, ChannelId{0}));
}

static void test_whitelist_non_channel_traffic_unaffected() {
    AclFixture f;
    ChannelId ch{400};
    f.acl->whitelist_channel(ch);

    ASSERT(!f.acl->is_allowed(AgentId{5}, AgentId{0}, ch));
    ASSERT( f.acl->is_allowed(AgentId{5}, AgentId{0}, ChannelId{401}));
    ASSERT( f.acl->is_allowed(AgentId{5}, AgentId{6}, ChannelId{0}));
}

int main() {
    std::cout << "=== AgentChat ACL Tests ===\n";
    RUN(default_allow);
    RUN(deny_specific_agent);
    RUN(deny_does_not_affect_other_agents);
    RUN(whitelist_only_blocks_unlisted);
    RUN(whitelist_only_allows_listed);
    RUN(remove_rule_restores_access);
    RUN(list_rules_returns_all);
    RUN(deny_and_whitelist_combined);
    RUN(global_deny_wildcard_from);
    RUN(whitelist_non_channel_traffic_unaffected);
    std::cout << "\nResult: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail ? 1 : 0;
}
