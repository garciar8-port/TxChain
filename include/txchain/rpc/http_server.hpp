#pragma once
// A minimal, single-threaded-per-request HTTP/1.1 server (Architecture Node/CLI
// §4): loopback, JSON-only, no auth — demo-grade. It owns only the socket layer;
// request routing is the pure rpc::handle_request. Bound to 127.0.0.1 so the
// node's RPC is never exposed off-host.

#include <cstdint>
#include <functional>
#include <string>

#include "txchain/rpc/rpc.hpp"

namespace txchain::rpc {

class HttpServer {
 public:
  // A route handler over (method, target, body) → response. In the node this is
  // a lock-guarded call into rpc::handle_request; in tests it can be any closure.
  using Handler = std::function<RpcResponse(const std::string& method, const std::string& target,
                                            const std::string& body)>;

  HttpServer(int port, Handler handler);
  ~HttpServer();

  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  // Bind 127.0.0.1:port and listen. port == 0 binds an ephemeral port (read it
  // back with port()). Returns false on any socket error.
  bool start();

  // The actual bound port (valid after a successful start()).
  int port() const { return port_; }

  // Accept + serve connections until stop() is called (blocks the caller — run it
  // on its own thread in the node).
  void run();

  // Ask run() to return at the next poll tick and close the listening socket.
  void stop();

 private:
  void serve_connection(int fd);

  int port_;
  Handler handler_;
  int listen_fd_ = -1;
  volatile bool stop_ = false;
};

}  // namespace txchain::rpc
