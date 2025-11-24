#include "http_server.hpp"
#include "util.hpp"
#include "cpp-httplib/httplib.h"

#include <iostream>
#include <sstream>
#include <chrono>
#include <cstdlib>
#include <thread>

// ---- CPU burn helper ----
static void cpu_burn(int micros) {
  if (micros <= 0) return;
  auto start = std::chrono::high_resolution_clock::now();
  while (true) {
    auto now = std::chrono::high_resolution_clock::now();
    if (std::chrono::duration_cast<std::chrono::microseconds>(now - start).count() >= micros)
      break;
    asm volatile("");
  }
}

static int get_cpu_burn() {
  const char* v = std::getenv("CPU_BURN_US");
  return v ? std::atoi(v) : 0;
}

// Simple JSON builder
static std::string json_kv(const std::string& k, const std::string& v) {
  return std::string("{\"") + k + "\":\"" + v + "\"}";
}

// Minimal JSON parser for {"key":"..","value":".."}
static std::pair<std::string, std::string> parse_json_kv(const std::string& body) {
  auto kpos = body.find("\"key\"");
  auto vpos = body.find("\"value\"");
  if (kpos == std::string::npos || vpos == std::string::npos)
    return {"", ""};
  auto kstart = body.find('"', kpos + 5);
  auto kend   = body.find('"', kstart + 1);
  auto vstart = body.find('"', vpos + 7);
  auto vend   = body.find('"', vstart + 1);
  std::string key = body.substr(kstart + 1, kend - kstart - 1);
  std::string val = body.substr(vstart + 1, vend - vstart - 1);
  return {key, val};
}

// ---- Per-thread DB connection ----
// Each OS thread gets its own DB object and connection.
// This avoids sharing PGconn* across threads, which was causing protocol errors.
namespace {
  thread_local DB tdb;
  thread_local bool tdb_connected = false;
}

// We keep dc_ in the KVServer object by reusing db_ in ctor to "test" DB,
// but the actual per-thread connections are established lazily below.

KVServer::KVServer(const ServerConfig& sc, const DBConfig& dc)
    : sc_(sc) {
  int burn = get_cpu_burn();
  std::cout << "CPU_BURN_US = " << burn << "\n";

  cache_ = std::make_unique<LRUCache>(sc.cache_capacity);

  // One-time startup check: ensure DB is reachable and table exists.
  // This uses the member db_ once, then all request handling uses thread-local DB.
  if (!db_.connect(dc)) {
    throw std::runtime_error("Failed to connect to database");
  }
  // db_ will be destroyed when KVServer is destroyed; we don't use it in handlers.
}

// Helper to get a per-thread DB connection using the same config
// We cannot access dc directly (not stored), but db_.connect(...) already
// created the table. For actual connections, we just call connect() with
// default DBConfig (db.cpp ignores cfg fields and uses constant DSN).
static DB* get_thread_db() {
  if (!tdb_connected) {
    DBConfig cfg; // values ignored in db.cpp anyway
    if (!tdb.connect(cfg)) {
      std::cerr << "Thread " << std::this_thread::get_id()
                << ": DB connect failed\n";
      return nullptr;
    }
    tdb_connected = true;
  }
  return &tdb;
}

bool KVServer::start() {
  httplib::Server srv;

  int cpu_burn_us = get_cpu_burn();
  std::cout << "Using CPU burn: " << cpu_burn_us << " microseconds\n";

  // POST /create
  srv.Post("/create", [&](const httplib::Request& req, httplib::Response& res) {
    cpu_burn(cpu_burn_us);

    auto [key, value] = parse_json_kv(req.body);
    if (key.empty() || value.empty()) {
      util::bad(res, "Invalid JSON body");
      return;
    }

    DB* db = get_thread_db();
    if (!db) {
      util::server_err(res);
      return;
    }

    if (!db->upsert(key, value)) {
      util::server_err(res);
      return;
    }

    cache_->put(key, value);
    util::ok(res, json_kv("status", "ok"));
  });

  // GET /read?key=...
  srv.Get("/read", [&](const httplib::Request& req, httplib::Response& res) {
    cpu_burn(cpu_burn_us);

    if (!req.has_param("key")) {
      util::bad(res, "Missing key parameter");
      return;
    }
    auto key = req.get_param_value("key");

    // First hit the in-memory cache
    if (auto v = cache_->get(key)) {
      hits_++;
      util::ok(res, json_kv("value", *v));
      return;
    }

    misses_++;

    DB* db = get_thread_db();
    if (!db) {
      util::server_err(res);
      return;
    }

    if (auto vdb = db->get(key)) {
      cache_->put(key, *vdb);
      util::ok(res, json_kv("value", *vdb));
      return;
    }

    util::not_found(res);
  });

  // DELETE /delete?key=...
  srv.Delete("/delete", [&](const httplib::Request& req, httplib::Response& res) {
    cpu_burn(cpu_burn_us);

    if (!req.has_param("key")) {
      util::bad(res, "Missing key parameter");
      return;
    }
    auto key = req.get_param_value("key");

    DB* db = get_thread_db();
    if (!db) {
      util::server_err(res);
      return;
    }

    if (!db->erase(key)) {
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
       << "\"cache_misses\":" << misses_.load()
       << "}";
    util::ok(res, ss.str());
  });

  std::cout << "=========================================\n";
  std::cout << "KV Server running at http://" << sc_.host << ":" << sc_.port
            << " with " << sc_.threads << " threads (configured)\n";
  std::cout << "Cache capacity: " << sc_.cache_capacity << "\n";
  std::cout << "=========================================\n";

  return srv.listen(sc_.host.c_str(), sc_.port);
}
