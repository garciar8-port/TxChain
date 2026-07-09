#pragma once
// A minimal blocking HTTP/1.1 client (Architecture Node/CLI §6) — the counterpart
// to HttpServer, used by `txchain send` to GET /account and POST /tx. JSON bodies,
// Connection: close, no keep-alive. Kept in txchain_core so the CLI shim carries
// no socket code.

#include <cstdint>
#include <string>

namespace txchain::rpc {

struct HttpResult {
  bool ok = false;     // false on connect/transport failure (status is meaningless)
  int status = 0;      // HTTP status code on ok
  std::string body;    // response body on ok
};

// Issue one request to host:port and read the whole response. `path` includes any
// query. On a connect/DNS/socket error returns {ok=false}.
HttpResult http_request(const std::string& method, const std::string& host, int port,
                        const std::string& path, const std::string& body);

inline HttpResult http_get(const std::string& host, int port, const std::string& path) {
  return http_request("GET", host, port, path, "");
}
inline HttpResult http_post(const std::string& host, int port, const std::string& path,
                            const std::string& body) {
  return http_request("POST", host, port, path, body);
}

}  // namespace txchain::rpc
