#pragma once
/**
 * @file rest_server.h
 * @brief AgentChat REST API — port 8767, OpenAPI 3.0 compatible
 *
 * Endpoints:
 *   GET  /v1/health         — health check
 *   GET  /v1/agents         — list registered agents
 *   GET  /v1/agents/:id     — get agent by ID
 *   GET  /v1/channels       — list channels
 *   POST /v1/channels       — create channel
 *   POST /v1/messages       — send message
 *   GET  /openapi.json      — OpenAPI 3.0 spec
 */

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <optional>
#include <sstream>
#include "../../include/third_party/httplib.h"

namespace agentchat {
namespace rest {

// ── Simple JSON helpers ───────────────────────────────────────────────────────

inline std::string json_str(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out + "\"";
}

// ── Data types ────────────────────────────────────────────────────────────────

struct AgentInfo {
    uint64_t    id;
    std::string name;
    std::string capabilities; // JSON array string e.g. "[\"text\",\"code\"]"
};

struct ChannelInfo {
    uint64_t    id;
    std::string name;
    int         type; // 0=dm 1=group 2=broadcast
};

// ── Callbacks wired from server/main.cpp ──────────────────────────────────────

struct RestCallbacks {
    std::function<std::vector<AgentInfo>()>           list_agents;
    std::function<std::optional<AgentInfo>(uint64_t)> get_agent;
    std::function<std::vector<ChannelInfo>()>         list_channels;
    std::function<uint64_t(const std::string&, int)>  create_channel;
    std::function<uint64_t(uint64_t from, uint64_t to_agent,
                           uint64_t to_ch, const std::string& text)> send_message;
    // PATCH /v1/agents/:id/trust
    std::function<bool(uint64_t agent_id, const std::string& level)> set_trust;
};

// ── REST Server ───────────────────────────────────────────────────────────────

class RestServer {
public:
    RestServer(uint16_t port, RestCallbacks cb)
        : port_(port), cb_(std::move(cb)), running_(false) {}

    ~RestServer() { stop(); }

    void start() {
        running_ = true;
        thread_ = std::thread([this] { run(); });
    }

    void stop() {
        if (running_.exchange(false)) {
            svr_.stop();
            if (thread_.joinable()) thread_.join();
        }
    }

private:
    uint16_t         port_;
    RestCallbacks    cb_;
    httplib::Server  svr_;
    std::thread      thread_;
    std::atomic<bool> running_;

    void run() {
        setup_routes();
        svr_.listen("0.0.0.0", port_);
    }

    void setup_routes() {
        // CORS
        svr_.set_pre_routing_handler([](const httplib::Request&, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");
            return httplib::Server::HandlerResponse::Unhandled;
        });
        svr_.Options(".*", [](const httplib::Request&, httplib::Response& res) {
            res.status = 204;
        });

        // GET /v1/health
        svr_.Get("/v1/health", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(R"({"status":"ok","service":"agentchat","version":"0.1.0"})",
                            "application/json");
        });

        // GET /v1/agents
        svr_.Get("/v1/agents", [this](const httplib::Request&, httplib::Response& res) {
            auto agents = cb_.list_agents ? cb_.list_agents() : std::vector<AgentInfo>{};
            std::ostringstream o;
            o << "{\"agents\":[";
            for (size_t i = 0; i < agents.size(); ++i) {
                if (i) o << ",";
                o << agent_json(agents[i]);
            }
            o << "],\"count\":" << agents.size() << "}";
            res.set_content(o.str(), "application/json");
        });

        // GET /v1/agents/:id
        svr_.Get("/v1/agents/(\\d+)", [this](const httplib::Request& req, httplib::Response& res) {
            uint64_t id = std::stoull(req.matches[1]);
            auto a = cb_.get_agent ? cb_.get_agent(id) : std::nullopt;
            if (!a) { res.status = 404; res.set_content(R"({"error":"not found"})", "application/json"); return; }
            res.set_content(agent_json(*a), "application/json");
        });

        // GET /v1/channels
        svr_.Get("/v1/channels", [this](const httplib::Request&, httplib::Response& res) {
            auto chs = cb_.list_channels ? cb_.list_channels() : std::vector<ChannelInfo>{};
            std::ostringstream o;
            o << "{\"channels\":[";
            for (size_t i = 0; i < chs.size(); ++i) {
                if (i) o << ",";
                o << channel_json(chs[i]);
            }
            o << "],\"count\":" << chs.size() << "}";
            res.set_content(o.str(), "application/json");
        });

        // POST /v1/channels
        svr_.Post("/v1/channels", [this](const httplib::Request& req, httplib::Response& res) {
            auto name = parse_str(req.body, "name");
            if (!name) { res.status = 400; res.set_content(R"({"error":"missing: name"})", "application/json"); return; }
            int type = 1;
            auto t = parse_str(req.body, "type");
            if (t) { if (*t == "dm") type = 0; else if (*t == "broadcast") type = 2; }
            uint64_t id = cb_.create_channel ? cb_.create_channel(*name, type) : 0;
            std::ostringstream o;
            o << "{\"id\":" << id << ",\"name\":" << json_str(*name) << ",\"type\":" << type << "}";
            res.status = 201;
            res.set_content(o.str(), "application/json");
        });

        // POST /v1/messages
        svr_.Post("/v1/messages", [this](const httplib::Request& req, httplib::Response& res) {
            auto text = parse_str(req.body, "text");
            if (!text) { res.status = 400; res.set_content(R"({"error":"missing: text"})", "application/json"); return; }
            uint64_t from    = parse_u64(req.body, "from_agent").value_or(0);
            uint64_t to_ag   = parse_u64(req.body, "to_agent").value_or(0);
            uint64_t to_ch   = parse_u64(req.body, "to_channel").value_or(0);
            if (!to_ag && !to_ch) { res.status = 400; res.set_content(R"({"error":"need to_agent or to_channel"})", "application/json"); return; }
            uint64_t mid = cb_.send_message ? cb_.send_message(from, to_ag, to_ch, *text) : 0;
            std::ostringstream o;
            o << "{\"message_id\":" << mid << ",\"status\":\"sent\"}";
            res.status = 201;
            res.set_content(o.str(), "application/json");
        });

        // GET /openapi.json
        svr_.Get("/openapi.json", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(openapi_spec(), "application/json");
        });
    }

    static std::string agent_json(const AgentInfo& a) {
        std::ostringstream o;
        o << "{\"id\":" << a.id << ",\"name\":" << json_str(a.name)
          << ",\"capabilities\":" << (a.capabilities.empty() ? "[]" : a.capabilities) << "}";
        return o.str();
    }

    static std::string channel_json(const ChannelInfo& c) {
        static const char* T[] = {"dm","group","broadcast"};
        std::ostringstream o;
        o << "{\"id\":" << c.id << ",\"name\":" << json_str(c.name)
          << ",\"type\":" << json_str(T[c.type < 3 ? c.type : 1]) << "}";
        return o.str();
    }

    static std::optional<std::string> parse_str(const std::string& j, const std::string& k) {
        auto pos = j.find('"' + k + '"');
        if (pos == std::string::npos) return std::nullopt;
        pos += k.size() + 2;
        while (pos < j.size() && (j[pos]==' '||j[pos]==':')) ++pos;
        if (pos >= j.size() || j[pos] != '"') return std::nullopt;
        ++pos;
        std::string r;
        while (pos < j.size() && j[pos] != '"') {
            if (j[pos]=='\\' && pos+1 < j.size()) { ++pos; if(j[pos]=='n') r+='\n'; else r+=j[pos]; }
            else r += j[pos];
            ++pos;
        }
        return r;
    }

    static std::optional<uint64_t> parse_u64(const std::string& j, const std::string& k) {
        auto pos = j.find('"' + k + '"');
        if (pos == std::string::npos) return std::nullopt;
        pos += k.size() + 2;
        while (pos < j.size() && (j[pos]==' '||j[pos]==':')) ++pos;
        if (pos >= j.size() || !std::isdigit((unsigned char)j[pos])) return std::nullopt;
        return std::stoull(j.substr(pos));
    }

    static std::string openapi_spec() {
        return R"({
  "openapi":"3.0.3",
  "info":{"title":"AgentChat API","version":"0.1.0",
    "description":"Encrypted AI-native messaging REST API",
    "license":{"name":"MIT"}},
  "servers":[{"url":"http://localhost:8767","description":"Local"}],
  "paths":{
    "/v1/health":{"get":{"summary":"Health check","operationId":"getHealth",
      "responses":{"200":{"description":"OK"}}}},
    "/v1/agents":{"get":{"summary":"List agents","operationId":"listAgents",
      "responses":{"200":{"description":"Agent list"}}}},
    "/v1/agents/{id}":{"get":{"summary":"Get agent","operationId":"getAgent",
      "parameters":[{"name":"id","in":"path","required":true,"schema":{"type":"integer"}}],
      "responses":{"200":{"description":"Agent"},"404":{"description":"Not found"}}}},
    "/v1/channels":{
      "get":{"summary":"List channels","operationId":"listChannels",
        "responses":{"200":{"description":"Channel list"}}},
      "post":{"summary":"Create channel","operationId":"createChannel",
        "requestBody":{"required":true,"content":{"application/json":{"schema":{
          "type":"object","required":["name"],
          "properties":{"name":{"type":"string"},"type":{"type":"string","enum":["dm","group","broadcast"]}}}}}},
        "responses":{"201":{"description":"Created"}}}},
    "/v1/agents/{id}/trust":{"patch":{"summary":"Set agent trust level","operationId":"setAgentTrust",
      "parameters":[{"name":"id","in":"path","required":true,"schema":{"type":"integer"}}],
      "requestBody":{"required":true,"content":{"application/json":{"schema":{
        "type":"object","required":["trust"],
        "properties":{"trust":{"type":"string","enum":["unknown","trusted","verified","blocked"]}}}}}},
      "responses":{"200":{"description":"Updated"},"404":{"description":"Agent not found"},"400":{"description":"Invalid trust level"}}}},
    "/v1/messages":{"post":{"summary":"Send message","operationId":"sendMessage",
      "requestBody":{"required":true,"content":{"application/json":{"schema":{
        "type":"object","required":["text"],
        "properties":{"from_agent":{"type":"integer"},"to_agent":{"type":"integer"},
          "to_channel":{"type":"integer"},"text":{"type":"string"}}}}}},
      "responses":{"201":{"description":"Sent"}}}}
  }
})";
    }
};

} // namespace rest
} // namespace agentchat
