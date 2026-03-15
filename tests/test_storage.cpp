/**
 * @file tests/test_storage.cpp
 * @brief Unit tests for the SQLite storage layer.
 */

#include "../src/core/storage/storage.h"
#include <agentchat/types.h>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace agentchat;
using namespace agentchat::storage;

// ── Test harness ──────────────────────────────────���───────────────────────────

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond) do { \
    if (!(cond)) throw std::runtime_error( \
        std::string("ASSERT failed: " #cond " at L") + std::to_string(__LINE__)); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if (!((a) == (b))) throw std::runtime_error( \
        std::string("ASSERT_EQ: " #a " != " #b " at L") + std::to_string(__LINE__)); \
} while(0)

#define RUN(name) do { \
    std::cout << "  " #name " ... " << std::flush; \
    try { test_##name(); std::cout << "PASS\n"; ++g_pass; } \
    catch (const std::exception& e) { \
        std::cout << "FAIL: " << e.what() << "\n"; ++g_fail; } \
} while(0)

// ── Helpers ──────────���──────────────────────────────────────────���─────────────

static Message make_msg(uint64_t id, uint64_t from, uint64_t to,
                        uint64_t channel = 0,
                        const std::string& text = "hello") {
    Message m;
    m.id        = MessageId{id};
    m.from      = AgentId{from};
    m.to        = AgentId{to};
    m.channel   = ChannelId{channel};
    m.type      = MessageType::TEXT;
    m.status    = DeliveryStatus::SENT;
    m.timestamp = now_ms();
    m.payload.assign(text.begin(), text.end());
    m.nonce.assign(12, 0xAB);
    m.signature.fill(0x00);
    return m;
}

static AgentInfo make_agent(uint64_t id, const std::string& name) {
    AgentInfo a;
    a.id            = AgentId{id};
    a.name          = name;
    a.public_key.fill(static_cast<uint8_t>(id & 0xFF));
    a.capabilities  = {"text"};
    a.registered_at = now_ms();
    a.online        = true;
    return a;
}

// ── MessageStore tests ────────────────────────────────────────────────────���───

static void test_store_and_retrieve_channel_message() {
    Database db(":memory:");
    auto& ms = db.messages();

    Message m = make_msg(1, 10, 0, 100, "test payload");
    ms.store_message(m);

    auto msgs = ms.get_messages(ChannelId{100}, 10);
    ASSERT(msgs.size() == 1);
    ASSERT_EQ(static_cast<uint64_t>(msgs[0].id),      1u);
    ASSERT_EQ(static_cast<uint64_t>(msgs[0].from),   10u);
    ASSERT_EQ(static_cast<uint64_t>(msgs[0].channel),100u);
    ASSERT(msgs[0].payload == m.payload);
}

static void test_mark_delivered() {
    Database db(":memory:");
    auto& ms = db.messages();

    Message m = make_msg(42, 1, 2, 0, "dm message");
    ms.store_message(m);
    ms.mark_delivered(MessageId{42});

    auto history = ms.get_dm_history(AgentId{1}, AgentId{2}, 10);
    ASSERT(history.size() == 1);
    ASSERT(history[0].status == DeliveryStatus::DELIVERED);
}

static void test_mark_read() {
    Database db(":memory:");
    auto& ms = db.messages();

    Message m = make_msg(7, 3, 4, 0, "read me");
    ms.store_message(m);
    ms.mark_delivered(MessageId{7});
    ms.mark_read(MessageId{7});

    auto history = ms.get_dm_history(AgentId{3}, AgentId{4}, 10);
    ASSERT(history.size() == 1);
    ASSERT(history[0].status == DeliveryStatus::READ);
}

static void test_dm_history_ordering() {
    Database db(":memory:");
    auto& ms = db.messages();

    for (uint64_t i = 1; i <= 3; ++i)
        ms.store_message(make_msg(i, 5, 6, 0, "msg" + std::to_string(i)));

    auto history = ms.get_dm_history(AgentId{5}, AgentId{6}, 10);
    ASSERT(history.size() == 3);
}

static void test_channel_message_limit() {
    Database db(":memory:");
    auto& ms = db.messages();

    for (uint64_t i = 1; i <= 10; ++i)
        ms.store_message(make_msg(i, 1, 0, 200, "m"));

    auto msgs = ms.get_messages(ChannelId{200}, 5);
    ASSERT(msgs.size() == 5);
}

static void test_empty_channel_returns_empty() {
    Database db(":memory:");
    auto& ms = db.messages();
    auto msgs = ms.get_messages(ChannelId{9999}, 50);
    ASSERT(msgs.empty());
}

// ── AgentRegistry tests ───────────���───────────────────────────────────────────

static void test_register_and_get_agent() {
    Database db(":memory:");
    auto& reg = db.agents();

    AgentInfo a = make_agent(1, "TestBot");
    reg.register_agent(a);

    auto got = reg.get_agent(AgentId{1});
    ASSERT(got.has_value());
    ASSERT_EQ(static_cast<uint64_t>(got->id), 1u);
    ASSERT(got->name == "TestBot");
}

static void test_list_agents() {
    Database db(":memory:");
    auto& reg = db.agents();

    reg.register_agent(make_agent(1, "Bot1"));
    reg.register_agent(make_agent(2, "Bot2"));
    reg.register_agent(make_agent(3, "Bot3"));

    auto list = reg.list_agents();
    ASSERT(list.size() == 3);
}

static void test_agent_not_found() {
    Database db(":memory:");
    auto& reg = db.agents();
    auto got = reg.get_agent(AgentId{999});
    ASSERT(!got.has_value());
}

static void test_update_last_seen() {
    Database db(":memory:");
    auto& reg = db.agents();
    reg.register_agent(make_agent(5, "Ping"));
    reg.update_last_seen(AgentId{5});
    auto got = reg.get_agent(AgentId{5});
    ASSERT(got.has_value());
}

// ── ChannelStore tests ────────────────────────────────────────────────────────

static void test_create_and_get_channel() {
    Database db(":memory:");
    auto& cs = db.channels();

    ChannelId cid = cs.create_channel("general", ChannelType::GROUP,
                                       {AgentId{1}, AgentId{2}});
    ASSERT(static_cast<uint64_t>(cid) != 0);

    auto ch = cs.get_channel(cid);
    ASSERT(ch.has_value());
    ASSERT(ch->name == "general");
    ASSERT(ch->type == ChannelType::GROUP);
    ASSERT(ch->members.size() == 2);
}

static void test_add_remove_member() {
    Database db(":memory:");
    auto& cs = db.channels();

    ChannelId cid = cs.create_channel("room", ChannelType::GROUP, {AgentId{1}});
    cs.add_member(cid, AgentId{2});

    auto ch = cs.get_channel(cid);
    ASSERT(ch.has_value());
    ASSERT(ch->members.size() == 2);

    cs.remove_member(cid, AgentId{1});
    ch = cs.get_channel(cid);
    ASSERT(ch.has_value());
    ASSERT(ch->members.size() == 1);
    ASSERT_EQ(static_cast<uint64_t>(ch->members[0]), 2u);
}

static void test_list_channels_for_agent() {
    Database db(":memory:");
    auto& cs = db.channels();

    cs.create_channel("ch1", ChannelType::GROUP, {AgentId{10}});
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    cs.create_channel("ch2", ChannelType::GROUP, {AgentId{10}, AgentId{11}});
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    cs.create_channel("ch3", ChannelType::GROUP, {AgentId{11}});

    auto list = cs.list_channels_for_agent(AgentId{10});
    ASSERT(list.size() == 2);
}

static void test_prekey_store_and_retrieve() {
    Database db(":memory:");
    auto& ps = db.prekeys();

    // Initially no prekey
    auto result = ps.get_prekey(42);
    ASSERT(!result.has_value());

    // Upload a 32-byte prekey
    std::vector<uint8_t> key(32);
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i);
    ps.store_prekey(42, key);

    result = ps.get_prekey(42);
    ASSERT(result.has_value());
    ASSERT_EQ(result->size(), 32u);
    ASSERT((*result) == key);
}

static void test_prekey_overwrite() {
    Database db(":memory:");
    auto& ps = db.prekeys();

    std::vector<uint8_t> key1(32, 0xAA);
    std::vector<uint8_t> key2(32, 0xBB);
    ps.store_prekey(7, key1);
    ps.store_prekey(7, key2); // overwrite

    auto result = ps.get_prekey(7);
    ASSERT(result.has_value());
    ASSERT((*result) == key2);
}

static void test_prekey_delete() {
    Database db(":memory:");
    auto& ps = db.prekeys();

    std::vector<uint8_t> key(32, 0x01);
    ps.store_prekey(99, key);
    ps.delete_prekey(99);

    auto result = ps.get_prekey(99);
    ASSERT(!result.has_value());
}

static void test_prekey_multiple_agents() {
    Database db(":memory:");
    auto& ps = db.prekeys();

    for (uint64_t id = 1; id <= 5; ++id) {
        std::vector<uint8_t> key(32, static_cast<uint8_t>(id));
        ps.store_prekey(id, key);
    }
    for (uint64_t id = 1; id <= 5; ++id) {
        auto r = ps.get_prekey(id);
        ASSERT(r.has_value());
        ASSERT((*r)[0] == static_cast<uint8_t>(id));
    }
    // agent 6 has none
    ASSERT(!ps.get_prekey(6).has_value());
}

int main() {
    std::cout << "=== AgentChat Storage Tests ===\n";
    RUN(store_and_retrieve_channel_message);
    RUN(mark_delivered);
    RUN(mark_read);
    RUN(dm_history_ordering);
    RUN(channel_message_limit);
    RUN(empty_channel_returns_empty);
    RUN(register_and_get_agent);
    RUN(list_agents);
    RUN(agent_not_found);
    RUN(update_last_seen);
    RUN(create_and_get_channel);
    RUN(add_remove_member);
    RUN(list_channels_for_agent);
    RUN(prekey_store_and_retrieve);
    RUN(prekey_overwrite);
    RUN(prekey_delete);
    RUN(prekey_multiple_agents);
    std::cout << "\nResult: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail ? 1 : 0;
}
