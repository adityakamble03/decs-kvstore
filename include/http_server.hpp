#pragma once
#include "db.hpp"
#include "lru_cache.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

struct ServerConfig {
  std::string host = "0.0.0.0";
  int port = 8080;
  size_t cache_capacity = 10000;
  int threads = std::thread::hardware_concurrency();
};

class KVServer {
public:
  KVServer(const ServerConfig& sc, const DBConfig& dc);
  bool start();  // blocking call to run the HTTP server

private:
  ServerConfig sc_;
  DB db_;
  std::unique_ptr<LRUCache> cache_;
  std::atomic<uint64_t> hits_{0}, misses_{0};
};
