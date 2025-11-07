#include <libpq-fe.h>
#include "db.hpp"
#include <iostream>

DB::~DB() {
  if (conn_) PQfinish(conn_);
}

bool DB::connect(const DBConfig& cfg) {
  (void)cfg;
  std::string conninfo =
    "host=127.0.0.1 "
    "port=5432 "
    "user=postgres "
    "password=postgres123 "
    "dbname=kvdb "
    "sslmode=disable "          
    "connect_timeout=10"; 

  conn_ = PQconnectdb(conninfo.c_str());
  if (PQstatus(conn_) != CONNECTION_OK) {
    std::cerr << "Connection failed: " << PQerrorMessage(conn_);
    return false;
  }

  const char* ddl =
    "CREATE TABLE IF NOT EXISTS kv_store ("
    " key TEXT PRIMARY KEY,"
    " value TEXT NOT NULL);";

  PGresult* res = PQexec(conn_, ddl);
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    std::cerr << "Table creation failed: " << PQerrorMessage(conn_);
    PQclear(res);
    return false;
  }
  PQclear(res);
  return true;
}

bool DB::upsert(const std::string& key, const std::string& value) {
  const char* sql =
    "INSERT INTO kv_store (key,value) VALUES ($1,$2) "
    "ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value;";
  const char* params[2] = { key.c_str(), value.c_str() };
  PGresult* res = PQexecParams(conn_, sql, 2, nullptr, params, nullptr, nullptr, 0);
  bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
  if (!ok) std::cerr << "Upsert failed: " << PQerrorMessage(conn_);
  PQclear(res);
  return ok;
}

std::optional<std::string> DB::get(const std::string& key) {
  const char* sql = "SELECT value FROM kv_store WHERE key=$1;";
  const char* params[1] = { key.c_str() };
  PGresult* res = PQexecParams(conn_, sql, 1, nullptr, params, nullptr, nullptr, 0);
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    PQclear(res);
    return std::nullopt;
  }
  if (PQntuples(res) == 0) {
    PQclear(res);
    return std::nullopt;
  }
  std::string val = PQgetvalue(res, 0, 0);
  PQclear(res);
  return val;
}

bool DB::erase(const std::string& key) {
  const char* sql = "DELETE FROM kv_store WHERE key=$1;";
  const char* params[1] = { key.c_str() };
  PGresult* res = PQexecParams(conn_, sql, 1, nullptr, params, nullptr, nullptr, 0);
  bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
  if (!ok) std::cerr << "Delete failed: " << PQerrorMessage(conn_);
  PQclear(res);
  return ok;
}
