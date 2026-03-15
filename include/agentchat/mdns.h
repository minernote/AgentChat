#pragma once
/**
 * @file mdns.h
 * @brief mDNS/DNS-SD zero-config LAN discovery for AgentChat.
 *
 * Advertises and discovers "_agentchat._tcp.local" services on the LAN
 * using multicast UDP (224.0.0.251:5353) without any external dependencies.
 *
 * Usage:
 *   // Advertise this server:
 *   agentchat::mdns::Advertiser adv("myserver", 7777);
 *   adv.start();   // spawns background thread
 *   // ...
 *   adv.stop();
 *
 *   // Browse for peers:
 *   agentchat::mdns::Browser browser([](const agentchat::mdns::ServiceInfo& s){
 *       std::cout << s.name << " @ " << s.address << ":" << s.port << "\n";
 *   });
 *   browser.start();
 *   // ...
 *   browser.stop();
 */

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

namespace agentchat::mdns {

// Service type registered on the LAN.
inline constexpr const char* SERVICE_TYPE = "_agentchat._tcp.local.";
inline constexpr uint16_t    MDNS_PORT    = 5353;
inline constexpr const char* MDNS_ADDR    = "224.0.0.251";

/// Information about a discovered AgentChat peer.
struct ServiceInfo {
    std::string name;     ///< Instance name (e.g. "myserver")
    std::string host;     ///< Hostname (e.g. "myserver.local.")
    std::string address;  ///< IPv4 address string
    uint16_t    port{0};  ///< TCP port the peer is listening on
};

/**
 * @brief Advertises this server via mDNS-SD.
 *
 * Sends periodic unsolicited mDNS announcements and responds to PTR queries
 * for _agentchat._tcp.local.
 */
class Advertiser {
public:
    /**
     * @param instance_name  Human-readable name (e.g. hostname).
     * @param port           TCP port this server listens on.
     */
    Advertiser(std::string instance_name, uint16_t port);
    ~Advertiser();

    /// Start the background announcement thread.
    void start();
    /// Stop announcements and leave the multicast group.
    void stop();

private:
    void run();
    std::vector<uint8_t> build_announcement() const;

    std::string  instance_name_;
    uint16_t     port_;
    int          sock_{-1};
    std::thread  thread_;
    std::atomic<bool> running_{false};
};

/**
 * @brief Discovers AgentChat peers via mDNS-SD.
 *
 * Sends a PTR query for _agentchat._tcp.local and invokes a callback
 * for each unique peer discovered (or re-announced).
 */
class Browser {
public:
    using Callback = std::function<void(const ServiceInfo&)>;

    explicit Browser(Callback on_found);
    ~Browser();

    /// Start the background discovery thread.
    void start();
    /// Stop discovery.
    void stop();

    /// Snapshot of currently known peers.
    std::vector<ServiceInfo> peers() const;

private:
    void run();
    void send_query();
    void handle_packet(const uint8_t* data, size_t len);

    Callback              on_found_;
    int                   sock_{-1};
    std::thread           thread_;
    std::atomic<bool>     running_{false};
    mutable std::mutex    peers_mu_;
    std::vector<ServiceInfo> peers_;
};

} // namespace agentchat::mdns
