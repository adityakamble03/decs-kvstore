#pragma once
#include <string>
#include "cpp-httplib/httplib.h"

namespace util {

inline void ok(httplib::Response& res, const std::string& body) {
  res.status = 200;
  res.set_header("Content-Type", "application/json");
  res.set_content(body, "application/json");
}

inline void bad(httplib::Response& res, const std::string& msg) {
  res.status = 400;
  res.set_header("Content-Type", "application/json");
  res.set_content("{\"error\":\"" + msg + "\"}", "application/json");
}

inline void not_found(httplib::Response& res) {
  res.status = 404;
  res.set_header("Content-Type", "application/json");
  res.set_content("{\"error\":\"not found\"}", "application/json");
}

inline void server_err(httplib::Response& res) {
  res.status = 500;
  res.set_header("Content-Type", "application/json");
  res.set_content("{\"error\":\"server error\"}", "application/json");
}

} // namespace util
