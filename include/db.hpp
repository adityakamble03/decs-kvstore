#pragma once
#include <string>
#include <optional>
#include <libpq-fe.h> 

struct DBConfig {
  std::string host = "127.0.0.1";
  std::string port = "5432";
  std::string user = "postgres";
  std::string password = "postgres123";
  std::string dbname = "kvdb";
};

class DB {
public:
  DB() = default;
  ~DB();

  bool connect(const DBConfig& cfg);
  bool upsert(const std::string& key, const std::string& value);
  std::optional<std::string> get(const std::string& key);
  bool erase(const std::string& key);

private:
  PGconn* conn_ = nullptr;
};
