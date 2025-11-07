#include "http_server.hpp"
#include "util.hpp"
#include "cpp-httplib/httplib.h"
#include <iostream>
#include <sstream>

// Simple JSON builder for responses
static std::string json_kv(const std::string& k, const std::string& v) {
  return std::string("{\"") + k + "\":\"" + v + "\"}";
}

// Parse minimal {"key":"value"} JSON (for /create)
static std::pair<std::string, std::string> parse_json_kv(const std::string& body) {
  auto kpos = body.find("\"key\"");
  auto vpos = body.find("\"value\"");
  if (kpos == std::string::npos || vpos == std::string::npos)
    return {"", ""};
  auto kstart = body.find('"', kpos + 5);
  auto kend = body.find('"', kstart + 1);
  auto vstart = body.find('"', vpos + 7);
  auto vend = body.find('"', vstart + 1);
  std::string key = body.substr(kstart + 1, kend - kstart - 1);
  std::string val = body.substr(vstart + 1, vend - vstart - 1);
  return {key, val};
}

KVServer::KVServer(const ServerConfig& sc, const DBConfig& dc)
    : sc_(sc) {
  cache_ = std::make_unique<LRUCache>(sc.cache_capacity);
  if (!db_.connect(dc))
    throw std::runtime_error("Failed to connect to database");
}

bool KVServer::start() {
  httplib::Server srv;
  srv.new_task_queue = [] {
        static thread_local DB db;
        db.connect(DBConfig{});
        return new httplib::ThreadPool(1);  // each handler thread gets one queue
    };

  // POST /create
  srv.Post("/create", [&](const httplib::Request& req, httplib::Response& res) {
    auto [key, value] = parse_json_kv(req.body);
    if (key.empty() || value.empty()) {
      util::bad(res, "Invalid JSON body");
      return;
    }
    if (!db_.upsert(key, value)) {
      util::server_err(res);
      return;
    }
    cache_->put(key, value);
    util::ok(res, json_kv("status", "ok"));
  });

  // GET /read?key=somekey
  srv.Get("/read", [&](const httplib::Request& req, httplib::Response& res) {
    if (!req.has_param("key")) {
      util::bad(res, "Missing key parameter");
      return;
    }
    auto key = req.get_param_value("key");
    if (auto v = cache_->get(key)) {
      hits_++;
      util::ok(res, json_kv("value", *v));
      return;
    }
    misses_++;
    if (auto vdb = db_.get(key)) {
      cache_->put(key, *vdb);
      util::ok(res, json_kv("value", *vdb));
      return;
    }
    util::not_found(res);
  });

  // DELETE /delete?key=somekey
  srv.Delete("/delete", [&](const httplib::Request& req, httplib::Response& res) {
    if (!req.has_param("key")) {
      util::bad(res, "Missing key parameter");
      return;
    }
    auto key = req.get_param_value("key");
    if (!db_.erase(key)) {
      util::server_err(res);
      return;
    }
    cache_->erase(key);
    util::ok(res, json_kv("status", "deleted"));
  });

  // GET /metrics
  srv.Get("/metrics", [&](const httplib::Request&, httplib::Response& res) {
    std::ostringstream ss;
    ss << "{"
       << "\"cache_size\":" << cache_->size() << ","
       << "\"cache_hits\":" << hits_.load() << ","
       << "\"cache_misses\":" << misses_.load() << "}";
    util::ok(res, ss.str());
  });

  std::cout << "KV Server running at http://" << sc_.host << ":" << sc_.port
            << " with " << sc_.threads << " threads\n";
  return srv.listen(sc_.host.c_str(), sc_.port);
}
