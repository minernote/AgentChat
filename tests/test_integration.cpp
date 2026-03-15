/**
 * @file tests/test_integration.cpp
 * @brief End-to-end integration test: server + two AgentClient instances.
 *
 * Flow:
 *   1. Start agentchat_server on a random port in a background thread.
 *   2. Create two clients with generated keypairs (agent1, agent2).
 *   3. Both connect and authenticate (AUTH challenge-response).
 *   4. Agent1 sends a text message to Agent2.
 *   5. Agent2 receives the correct message.
 *   6. Agent2 replies to Agent1.
 *   7. Agent1 receives the reply.
 *   8. Clean shutdown.
 */

#include <agentchat/crypto.h>
#include <agentchat/types.h>
#include "../src/agent/agent_client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// ── Test harness ───────────────────────────────────���─────────────────────────

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond) do { \
    if (!(cond)) throw std::runtime_error( \
        std::string("ASSERT failed: " #cond " at L") + std::to_string(__LINE__)); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if (!((a) == (b))) throw std::runtime_error( \
        std::string("ASSERT_EQ failed at L") + std::to_string(__LINE__)); \
} while(0)

#define RUN(name) do { \
    std::cout << "  " #name " ... " << std::flush; \
    try { test_##name(); std::cout << "PASS\n"; ++g_pass; } \
    catch (const std::exception& e) { \
        std::cout << "FAIL: " << e.what() << "\n"; ++g_fail; } \
} while(0)

// ── Helpers ─────────────────���─────────────────────────────────────────────────

/** Pick a free ephemeral port by binding to :0 and releasing it. */
static uint16_t pick_free_port() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket() failed");
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = 0;
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        throw std::runtime_error("bind(:0) failed");
    }
    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
    uint16_t port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

/**
 * Minimal in-process server launcher.
 * Spawns agentchat_server binary as a child process on the given port.
 * Returns the child PID; caller kills/waits it.
 */
struct ServerProcess {
    pid_t pid{-1};
    uint16_t port{0};

    ServerProcess() = default;
    ~ServerProcess() { stop(); }

    void start(uint16_t p, const std::string& server_bin) {
        port = p;
        uint16_t ws_port   = pick_free_port();
        uint16_t rest_port = pick_free_port();
        pid = ::fork();
        if (pid < 0) throw std::runtime_error("fork() failed");
        if (pid == 0) {
            // child
            std::string port_str      = std::to_string(port);
            std::string ws_port_str   = std::to_string(ws_port);
            std::string rest_port_str = std::to_string(rest_port);
            ::execlp(server_bin.c_str(), server_bin.c_str(),
                     "--port",      port_str.c_str(),
                     "--ws-port",   ws_port_str.c_str(),
                     "--rest-port", rest_port_str.c_str(),
                     nullptr);
            ::_exit(1);
        }
        // Poll until the server is listening (up to 2 seconds)
        {
            auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(2000);
            bool ready = false;
            while (std::chrono::steady_clock::now() < deadline) {
                int probe = ::socket(AF_INET, SOCK_STREAM, 0);
                if (probe >= 0) {
                    struct sockaddr_in sa{};
                    sa.sin_family      = AF_INET;
                    sa.sin_port        = htons(port);
                    ::inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
                    if (::connect(probe,
                                  reinterpret_cast<struct sockaddr*>(&sa),
                                  sizeof(sa)) == 0) {
                        ::close(probe);
                        ready = true;
                        break;
                    }
                    ::close(probe);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!ready)
                throw std::runtime_error("server did not start in time on port " +
                                         std::to_string(port));
        }
    }

    void stop() {
        if (pid > 0) {
            ::kill(pid, SIGTERM);
            int status = 0;
            ::waitpid(pid, &status, 0);
            pid = -1;
        }
    }
};

/**
 * Wait up to `timeout_ms` for `flag` to become true.
 * Returns true if flag became true in time.
 */
static bool wait_for(const std::atomic<bool>& flag, int timeout_ms = 3000) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (!flag.load()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

// ── Integration test ────────────────────────────────────────────────────���─────

static std::string g_server_bin;

static void test_two_agent_roundtrip() {
    uint16_t port = pick_free_port();
    ServerProcess srv;
    srv.start(port, g_server_bin);

    // ── Create agent1 with fresh keypairs ────────────────────────────────────
    agentchat::crypto::KeyPair id1, ex1;
    auto client1 = agentchat::make_agent_client("127.0.0.1", port,
                                                 agentchat::AgentId{1},
                                                 &id1, &ex1);
    ASSERT(client1 != nullptr);

    // ── Create agent2 with fresh keypairs ───────────────────────���────────────
    agentchat::crypto::KeyPair id2, ex2;
    auto client2 = agentchat::make_agent_client("127.0.0.1", port,
                                                 agentchat::AgentId{2},
                                                 &id2, &ex2);
    ASSERT(client2 != nullptr);

    // ── Message-received flags ────���──────────────────────────────────────────���
    std::atomic<bool> agent2_got_msg{false};
    std::atomic<bool> agent1_got_reply{false};
    std::string agent2_received_text;
    std::string agent1_received_text;
    std::mutex  text_mu;

    // ── Set callbacks BEFORE connect ─────────────────────────────────────────
    client2->on_message([&](const agentchat::Message& msg) {
        {
            std::lock_guard<std::mutex> lk(text_mu);
            // Record the from-agent ID to verify routing
            agent2_received_text = "from:" + std::to_string(static_cast<uint64_t>(msg.from));
        }
        agent2_got_msg.store(true);

        // Agent2 replies to Agent1
        client2->send_message(agentchat::AgentId{1}, "Hello back from Agent2!");
    });

    client1->on_message([&](const agentchat::Message& msg) {
        {
            std::lock_guard<std::mutex> lk(text_mu);
            agent1_received_text = "from:" + std::to_string(static_cast<uint64_t>(msg.from));
        }
        agent1_got_reply.store(true);
    });

    // ── Connect both clients ──────────────────────────���───────────────────────
    ASSERT(client1->connect());
    ASSERT(client2->connect());
    ASSERT(client1->is_connected());
    ASSERT(client2->is_connected());

    // ── Register agents ───────────────────────────────────────────────────────
    ASSERT(client1->register_agent("Agent1", {"text"}));
    ASSERT(client2->register_agent("Agent2", {"text"}));

    // Small delay for REGISTER_ACK to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // ── Agent1 sends to Agent2 ─────────────���──────────────────────────────────
    auto mid = client1->send_message(agentchat::AgentId{2}, "Hello from Agent1!");
    ASSERT(static_cast<uint64_t>(mid) != 0);

    // ── Verify Agent2 receives message ────────────────────────────────────────
    ASSERT(wait_for(agent2_got_msg, 3000));
    {
        std::lock_guard<std::mutex> lk(text_mu);
        ASSERT(agent2_received_text == "from:1");
    }

    // ── Verify Agent1 receives reply ──────────────────────────────────────────
    ASSERT(wait_for(agent1_got_reply, 3000));
    {
        std::lock_guard<std::mutex> lk(text_mu);
        ASSERT(agent1_received_text == "from:2");
    }

    // ── Clean shutdown ───────���──────────────────────────────────────────���─────
    client1->disconnect();
    client2->disconnect();
    srv.stop();
}

// ── Channel tests ──────────────────────────────────────────────────────────

static void test_channel_create_join_leave() {
    uint16_t port = pick_free_port();
    ServerProcess srv;
    srv.start(port, g_server_bin);

    agentchat::crypto::KeyPair id1, ex1, id2, ex2;
    auto client1 = agentchat::make_agent_client("127.0.0.1", port,
                                                 agentchat::AgentId{20}, &id1, &ex1);
    auto client2 = agentchat::make_agent_client("127.0.0.1", port,
                                                 agentchat::AgentId{21}, &id2, &ex2);
    ASSERT(client1->connect());
    ASSERT(client2->connect());
    ASSERT(client1->register_agent("ChanAgent1", {"text"}));
    ASSERT(client2->register_agent("ChanAgent2", {"text"}));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Agent1 creates a group channel with agent2 as initial member
    auto ch = client1->create_channel("test-room",
                                       agentchat::ChannelType::GROUP,
                                       {agentchat::AgentId{21}});
    ASSERT(ch.has_value());
    ASSERT(static_cast<uint64_t>(ch->id) != 0);
    ASSERT(ch->name == "test-room");

    // Agent2 can join the channel explicitly
    ASSERT(client2->join_channel(ch->id));

    // Agent1 leaves
    ASSERT(client1->leave_channel(ch->id));

    client1->disconnect();
    client2->disconnect();
    srv.stop();
}

static void test_channel_messaging() {
    uint16_t port = pick_free_port();
    ServerProcess srv;
    srv.start(port, g_server_bin);

    agentchat::crypto::KeyPair id1, ex1, id2, ex2, id3, ex3;
    auto client1 = agentchat::make_agent_client("127.0.0.1", port,
                                                 agentchat::AgentId{30}, &id1, &ex1);
    auto client2 = agentchat::make_agent_client("127.0.0.1", port,
                                                 agentchat::AgentId{31}, &id2, &ex2);
    auto client3 = agentchat::make_agent_client("127.0.0.1", port,
                                                 agentchat::AgentId{32}, &id3, &ex3);
    ASSERT(client1->connect());
    ASSERT(client2->connect());
    ASSERT(client3->connect());
    ASSERT(client1->register_agent("BroadA", {"text"}));
    ASSERT(client2->register_agent("BroadB", {"text"}));
    ASSERT(client3->register_agent("BroadC", {"text"}));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Create channel with all three as members
    auto ch = client1->create_channel("broadcast-room",
                                       agentchat::ChannelType::GROUP,
                                       {agentchat::AgentId{31},
                                        agentchat::AgentId{32}});
    ASSERT(ch.has_value());

    std::atomic<int> received_count{0};
    std::mutex       text_mu;
    std::vector<std::string> received_texts;

    auto recv_cb = [&](const agentchat::Message& msg) {
        {
            std::lock_guard<std::mutex> lk(text_mu);
            received_texts.push_back("agent:" +
                std::to_string(static_cast<uint64_t>(msg.from)));
        }
        received_count.fetch_add(1);
    };
    client2->on_message(recv_cb);
    client3->on_message(recv_cb);

    // Agent1 sends to channel — both agent2 and agent3 should receive
    auto mid = client1->send_to_channel(ch->id, "Hello channel!");
    ASSERT(static_cast<uint64_t>(mid) != 0);

    // Wait for both to receive (up to 4s)
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(4000);
    while (received_count.load() < 2 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(received_count.load(), 2);

    client1->disconnect();
    client2->disconnect();
    client3->disconnect();
    srv.stop();
}

static void test_list_agents() {
    uint16_t port = pick_free_port();
    ServerProcess srv;
    srv.start(port, g_server_bin);

    agentchat::crypto::KeyPair id1, ex1, id2, ex2;
    auto client1 = agentchat::make_agent_client("127.0.0.1", port,
                                                 agentchat::AgentId{40}, &id1, &ex1);
    auto client2 = agentchat::make_agent_client("127.0.0.1", port,
                                                 agentchat::AgentId{41}, &id2, &ex2);
    ASSERT(client1->connect());
    ASSERT(client2->connect());
    ASSERT(client1->register_agent("ListAgent1", {"text", "code"}));
    ASSERT(client2->register_agent("ListAgent2", {"vision"}));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    auto agents = client1->list_agents();
    // At minimum we should see both registered agents
    ASSERT(agents.size() >= 2);

    bool found1 = false, found2 = false;
    for (const auto& a : agents) {
        if (a.id == agentchat::AgentId{40}) found1 = true;
        if (a.id == agentchat::AgentId{41}) found2 = true;
    }
    ASSERT(found1);
    ASSERT(found2);

    client1->disconnect();
    client2->disconnect();
    srv.stop();
}

static void test_reaction() {
    uint16_t port = pick_free_port();
    ServerProcess srv;
    srv.start(port, g_server_bin);

    agentchat::crypto::KeyPair id1, ex1, id2, ex2;
    auto client1 = agentchat::make_agent_client("127.0.0.1", port,
                                                 agentchat::AgentId{50}, &id1, &ex1);
    auto client2 = agentchat::make_agent_client("127.0.0.1", port,
                                                 agentchat::AgentId{51}, &id2, &ex2);
    ASSERT(client1->connect());
    ASSERT(client2->connect());
    ASSERT(client1->register_agent("ReactAgent1", {"text"}));
    ASSERT(client2->register_agent("ReactAgent2", {"text"}));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Agent1 sends a message; capture the MessageId for reaction
    std::atomic<bool> got_msg{false};
    agentchat::MessageId received_mid{0};
    std::mutex mid_mu;

    client2->on_message([&](const agentchat::Message& msg) {
        {
            std::lock_guard<std::mutex> lk(mid_mu);
            received_mid = msg.id;
        }
        got_msg.store(true);
    });

    auto sent_mid = client1->send_message(agentchat::AgentId{51}, "React to this!");
    ASSERT(static_cast<uint64_t>(sent_mid) != 0);
    ASSERT(wait_for(got_msg, 3000));

    // Agent2 reacts with a thumbs up
    {
        std::lock_guard<std::mutex> lk(mid_mu);
        ASSERT(client2->react_message(received_mid, "👍"));
    }

    client1->disconnect();
    client2->disconnect();
    srv.stop();
}

// ── Original tests ────────────────────────────────────────────────────────────

static void test_auth_completes() {
    uint16_t port = pick_free_port();
    ServerProcess srv;
    srv.start(port, g_server_bin);

    agentchat::crypto::KeyPair id1, ex1;
    auto client = agentchat::make_agent_client("127.0.0.1", port,
                                                agentchat::AgentId{10},
                                                &id1, &ex1);
    ASSERT(client != nullptr);
    ASSERT(client->connect());
    ASSERT(client->is_connected());
    client->disconnect();
    srv.stop();
}

int main(int argc, char* argv[]) {
    // The server binary path can be passed as argv[1], defaulting to ./agentchat_server
    if (argc > 1) {
        g_server_bin = argv[1];
    } else {
        // Try to find the server binary relative to the test binary location
        g_server_bin = "./agentchat_server";
    }

    std::cout << "=== AgentChat Integration Tests ===\n";
    std::cout << "Server binary: " << g_server_bin << "\n";
    RUN(auth_completes);
    RUN(two_agent_roundtrip);
    RUN(channel_create_join_leave);
    RUN(channel_messaging);
    RUN(list_agents);
    RUN(reaction);
    std::cout << "\nResult: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail ? 1 : 0;
}
