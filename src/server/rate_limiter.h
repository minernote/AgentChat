#pragma once
/**
 * @file rate_limiter.h
 * @brief Per-agent sliding-window rate limiter.
 *
 * Default: 60 messages per 60-second window.
 * Returns true if the message is allowed, false (429) if rate exceeded.
 */
#include <agentchat/types.h>

#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace agentchat::server {

class RateLimiter {
public:
    /**
     * @param max_messages  Max messages allowed per window.
     * @param window_sec    Window duration in seconds.
     */
    explicit RateLimiter(int max_messages = 60, int window_sec = 60)
        : max_messages_(max_messages)
        , window_(std::chrono::seconds(window_sec))
    {}

    /**
     * @brief Check and record a message attempt.
     * @return true  if allowed, false if rate limit exceeded (429).
     */
    bool allow(AgentId agent) {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lk(mtx_);
        auto& dq = windows_[static_cast<uint64_t>(agent)];
        // evict timestamps older than the window
        while (!dq.empty() && now - dq.front() > window_)
            dq.pop_front();
        if (static_cast<int>(dq.size()) >= max_messages_)
            return false;
        dq.push_back(now);
        return true;
    }

    /** Current message count in window for agent (for diagnostics). */
    int count(AgentId agent) {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = windows_.find(static_cast<uint64_t>(agent));
        if (it == windows_.end()) return 0;
        auto& dq = it->second;
        while (!dq.empty() && now - dq.front() > window_)
            dq.pop_front();
        return static_cast<int>(dq.size());
    }

private:
    int max_messages_;
    std::chrono::seconds window_;
    std::mutex mtx_;
    std::unordered_map<uint64_t,
        std::deque<std::chrono::steady_clock::time_point>> windows_;
};

} // namespace agentchat::server
