#include "agent_client.h"
#include "protocol.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdexcept>
#include <iostream>
#include <cstring>

// ── Constructor / Destructor ──────────────────────────────────────────────────

AgentClient::AgentClient(const std::string& server_host, int port)
    : host_(server_host), port_(port), sock_fd_(-1), connected_(false) {}

AgentClient::~AgentClient() {
    disconnect();
}

// ── Connection ────────────────────────────────────────────────────────────────

bool AgentClient::connect() {
    sock_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   =