#include "http_server.hpp"
#include <cstdlib>
#include <iostream>

static std::string env(const char* key, const char* def) {
  const char* val = std::getenv(key);
  return val ? std::string(val) : std::string(def);
}

static int env_int(const char* key, int def) {
  const char* val = std::getenv(key);
  return val ? std::atoi(val) : def;
}

static size_t env_size(const char* key, size_t def) {
  const char* val = std::getenv(key);
  return val ? static_cast<size_t>(std::atoll(val)) : def;
}

int main() {
  try {
    // --- Server Config ---
    ServerConfig sc;
    sc.host = env("SRV_HOST", "0.0.0.0");
    sc.port = env_int("SRV_PORT", 8080);
    sc.cache_capacity = env_size("CACHE_CAP", 1000);
    sc.threads = env_int("SRV_THREADS", std::thread::hardware_concurrency());

    // --- DB Config ---
    DBConfig dc;
    dc.host = env("DB_HOST", "127.0.0.1");
    dc.port = env("DB_PORT", "5432");
    dc.user = env("DB_USER", "postgres");
    dc.password = env("DB_PASS", "postgres123");
    dc.dbname = env("DB_NAME", "kvdb");

    KVServer server(sc, dc);
    return server.start() ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << "\n";
    return 2;
  }
}
