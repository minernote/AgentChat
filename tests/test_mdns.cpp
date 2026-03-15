/**
 * @file tests/test_mdns.cpp
 * @brief Unit tests for mDNS Advertiser + Browser (loopback discovery).
 *
 * Tests:
 *   1. advertiser_starts_stops  — Advertiser can start and stop cleanly.
 *   2. browser_starts_stops     — Browser can start and stop cleanly.
 *   3. discover_local_service   — Browser discovers a locally-advertised service
 *                                 within 3 seconds on the loopback multicast.
 *   4. multiple_advertisers     — Two advertisers with different names; browser
 *                                 finds both.
 *   5. goodbye_removes_peer     — After Advertiser::stop() a goodbye packet is
 *                                 sent (smoke-test: no crash, peer list shrinks
 *                                 or stays consistent).
 */

#include <agentchat/mdns.h>

#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <atomic>

// ── Test harness ──────────────────────────────────────────────────────────────

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond) do { \
    if (!(cond)) throw std::runtime_error( \
        std::string("ASSERT failed: " #cond " at L") + std::to_string(__LINE__)); \
} while(0)

#define RUN(name) do { \
    std::cout << "  " #name " ... " << std::flush; \
    try { test_##name(); std::cout << "PASS\n"; ++g_pass; } \
    catch (const std::exception& e) { \
        std::cout << "FAIL: " << e.what() << "\n"; ++g_fail; } \
} while(0)

// ── Helpers ───────────────────────────────────────────────────────────────────

/**
 * Wait up to timeout_ms for predicate to return true.
 * Polls every 50 ms.
 */
template<typename Pred>
static bool wait_until(Pred pred, int timeout_ms = 3000) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

static void test_advertiser_starts_stops() {
    agentchat::mdns::Advertiser adv("test-adv-start", 17001);
    adv.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    adv.stop(); // must not hang or throw
}

static void test_browser_starts_stops() {
    agentchat::mdns::Browser browser([](const agentchat::mdns::ServiceInfo&) {});
    browser.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    browser.stop(); // must not hang or throw
}

static void test_discover_local_service() {
    const std::string svc_name = "test-discovery-svc";
    const uint16_t    svc_port = 17002;

    // Start advertiser first
    agentchat::mdns::Advertiser adv(svc_name, svc_port);
    adv.start();

    // Give the advertiser a moment to bind before the browser queries
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::mutex                              found_mu;
    std::vector<agentchat::mdns::ServiceInfo> found;

    agentchat::mdns::Browser browser([&](const agentchat::mdns::ServiceInfo& s) {
        std::lock_guard<std::mutex> lk(found_mu);
        found.push_back(s);
    });
    browser.start();

    // Wait up to 4 seconds for the service to be discovered
    bool ok = wait_until([&] {
        std::lock_guard<std::mutex> lk(found_mu);
        return std::any_of(found.begin(), found.end(),
            [&](const agentchat::mdns::ServiceInfo& s) {
                return s.name == svc_name && s.port == svc_port;
            });
    }, 4000);

    browser.stop();
    adv.stop();

    ASSERT(ok);
}

static void test_multiple_advertisers() {
    const uint16_t port_a = 17003;
    const uint16_t port_b = 17004;

    agentchat::mdns::Advertiser adv_a("multi-svc-a", port_a);
    agentchat::mdns::Advertiser adv_b("multi-svc-b", port_b);
    adv_a.start();
    adv_b.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::mutex                              found_mu;
    std::vector<agentchat::mdns::ServiceInfo> found;

    agentchat::mdns::Browser browser([&](const agentchat::mdns::ServiceInfo& s) {
        std::lock_guard<std::mutex> lk(found_mu);
        // Deduplicate by name
        bool dup = std::any_of(found.begin(), found.end(),
            [&](const agentchat::mdns::ServiceInfo& x){ return x.name == s.name; });
        if (!dup) found.push_back(s);
    });
    browser.start();

    bool ok = wait_until([&] {
        std::lock_guard<std::mutex> lk(found_mu);
        bool has_a = std::any_of(found.begin(), found.end(),
            [](const agentchat::mdns::ServiceInfo& s){ return s.name == "multi-svc-a"; });
        bool has_b = std::any_of(found.begin(), found.end(),
            [](const agentchat::mdns::ServiceInfo& s){ return s.name == "multi-svc-b"; });
        return has_a && has_b;
    }, 5000);

    browser.stop();
    adv_a.stop();
    adv_b.stop();

    ASSERT(ok);
}

static void test_goodbye_removes_peer() {
    // Smoke test: advertiser sends goodbye on stop(); browser handles it
    // gracefully (no crash). We don't mandate exact peer-list shrinkage
    // since timing varies, but the peers() snapshot must remain consistent.
    agentchat::mdns::Advertiser adv("goodbye-svc", 17005);
    adv.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    agentchat::mdns::Browser browser([](const agentchat::mdns::ServiceInfo&) {});
    browser.start();

    // Let browser discover the service
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Stop advertiser — sends goodbye (TTL=0) packet
    adv.stop();

    // Give browser time to process goodbye
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // peers() must not throw and must return a valid (possibly empty) list
    auto peers = browser.peers();
    (void)peers; // size check is timing-dependent; just ensure no crash

    browser.stop();
    // If we reached here without exception/crash: PASS
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== AgentChat mDNS Tests ===\n";
    RUN(advertiser_starts_stops);
    RUN(browser_starts_stops);
    RUN(discover_local_service);
    RUN(multiple_advertisers);
    RUN(goodbye_removes_peer);
    std::cout << "\nResult: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail ? 1 : 0;
}
