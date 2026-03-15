/**
 * @file src/client/main.cpp
 * @brief AgentChat CLI client — interactive test harness for agents.
 */

#include <agentchat/types.h>
#include <agentchat/crypto.h>
#include "../agent/agent_client.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [options]\n"
        << "  --host HOST    Server host (default: 127.0.0.1)\n"
        << "  --port PORT    Server port (default: 8765)\n"
        << "  --name NAME    Agent display name\n"
        << "  --help         Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    uint16_t    port = 7777;
    std::string name = "cli-agent";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--host") && i + 1 < argc)  host = argv[++i];
        else if ((arg == "--port") && i + 1 < argc) port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if ((arg == "--name") && i + 1 < argc) name = argv[++i];
        else if (arg == "--help") { print_usage(argv[0]); return 0; }
    }

    std::cout << "[client] AgentChat CLI\n";
    std::cout << "[client] Generating keypairs...\n";

    agentchat::crypto::KeyPair id_kp, ex_kp;
    auto client = agentchat::make_agent_client(host, port,
                                               agentchat::AgentId{0},
                                               &id_kp, &ex_kp);
    if (!client) {
        std::cerr << "[client] Failed to create client (keypair generation error)\n";
        return 1;
    }

    std::cout << "[client] Identity: "
              << agentchat::crypto::to_hex(
                     std::span<const uint8_t>{id_kp.pub.data(), id_kp.pub.size()})
              << "\n";

    // Register callbacks
    client->on_message([](const agentchat::Message& msg) {
        std::string text(msg.payload.begin(), msg.payload.end());
        std::cout << "\n[MSG from " << static_cast<uint64_t>(msg.from)
                  << "] " << text << "\n> " << std::flush;
    });
    client->on_error([](const std::string& err) {
        std::cerr << "[error] " << err << "\n";
    });

    std::cout << "[client] Connecting to " << host << ":" << port << "...\n";
    if (!client->connect()) {
        std::cerr << "[client] Connection failed. Is the server running?\n";
        // Don't exit — allow offline key/crypto testing
        std::cout << "[client] Running in offline mode (crypto tests only)\n";
    } else {
        client->register_agent(name, {"text", "cli"});
        std::cout << "[client] Registered as '" << name << "'\n";
    }

    // Interactive REPL
    std::cout << "Commands:\n"
              << "  send <agent_id> <message>          Send direct message\n"
              << "  channel send <channel_id> <msg>    Send to channel\n"
              << "  channel create <name> [group|broadcast]  Create channel\n"
              << "  channel join <channel_id>           Join channel\n"
              << "  channel leave <channel_id>          Leave channel\n"
              << "  react <msg_id> <emoji>              React to message\n"
              << "  list                                List agents\n"
              << "  quit                                Exit\n";

    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "quit" || cmd == "exit") break;

        else if (cmd == "send") {
            uint64_t to_id = 0;
            std::string msg;
            iss >> to_id;
            std::getline(iss, msg);
            if (!msg.empty() && msg[0] == ' ') msg = msg.substr(1);
            auto mid = client->send_message(agentchat::AgentId{to_id}, msg);
            if (mid) std::cout << "[sent msg_id=" << static_cast<uint64_t>(mid) << "]\n";
            else     std::cerr << "[send failed]\n";
        }
        else if (cmd == "channel") {
            std::string sub;
            iss >> sub;

            if (sub == "create") {
                std::string name, type_str;
                iss >> name >> type_str;
                if (name.empty()) { std::cerr << "Usage: channel create <name> [group|broadcast]\n"; continue; }
                auto ctype = agentchat::ChannelType::GROUP;
                if (type_str == "broadcast") ctype = agentchat::ChannelType::BROADCAST;
                auto ch = client->create_channel(name, ctype, {});
                if (ch) std::cout << "[channel created id=" << static_cast<uint64_t>(ch->id) << " name=" << ch->name << "]\n";
                else    std::cerr << "[create failed]\n";
            }
            else if (sub == "join") {
                uint64_t ch_id = 0;
                iss >> ch_id;
                if (!ch_id) { std::cerr << "Usage: channel join <channel_id>\n"; continue; }
                bool ok = client->join_channel(agentchat::ChannelId{ch_id});
                std::cout << (ok ? "[joined]" : "[join failed]") << "\n";
            }
            else if (sub == "leave") {
                uint64_t ch_id = 0;
                iss >> ch_id;
                if (!ch_id) { std::cerr << "Usage: channel leave <channel_id>\n"; continue; }
                bool ok = client->leave_channel(agentchat::ChannelId{ch_id});
                std::cout << (ok ? "[left]" : "[leave failed]") << "\n";
            }
            else if (sub == "send") {
                uint64_t ch_id = 0;
                std::string msg;
                iss >> ch_id;
                std::getline(iss, msg);
                if (!ch_id) { std::cerr << "Usage: channel send <channel_id> <message>\n"; continue; }
                if (!msg.empty() && msg[0] == ' ') msg = msg.substr(1);
                auto mid = client->send_to_channel(agentchat::ChannelId{ch_id}, msg);
                if (mid) std::cout << "[sent to channel msg_id=" << static_cast<uint64_t>(mid) << "]\n";
                else     std::cerr << "[channel send failed]\n";
            }
            else {
                std::cerr << "Unknown channel subcommand: " << sub << "\n";
            }
        }
        else if (cmd == "react") {
            uint64_t msg_id = 0;
            std::string emoji;
            iss >> msg_id >> emoji;
            if (!msg_id || emoji.empty()) { std::cerr << "Usage: react <msg_id> <emoji>\n"; continue; }
            bool ok = client->react_message(agentchat::MessageId{msg_id}, emoji);
            std::cout << (ok ? "[reacted]" : "[react failed]") << "\n";
        }
        else if (cmd == "list") {
            auto agents = client->list_agents();
            if (agents.empty()) std::cout << "[no agents / not connected]\n";
            for (auto& a : agents)
                std::cout << "  " << static_cast<uint64_t>(a.id)
                          << "  " << a.name << "\n";
        }
        else if (cmd == "help") {
            std::cout << "Commands:\n"
                      << "  send <agent_id> <message>\n"
                      << "  channel send <channel_id> <message>\n"
                      << "  channel create <name> [group|broadcast]\n"
                      << "  channel join <channel_id>\n"
                      << "  channel leave <channel_id>\n"
                      << "  react <msg_id> <emoji>\n"
                      << "  list\n"
                      << "  quit\n";
        }
        else {
            std::cerr << "Unknown command: " << cmd << " (type 'help' for commands)\n";
        }
    }

    client->disconnect();
    std::cout << "[client] Bye.\n";
    return 0;
}
