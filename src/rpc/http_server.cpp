#include "txchain/rpc/http_server.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace txchain::rpc {

namespace {

constexpr std::size_t kMaxRequestBytes = 1 << 20;  // 1 MiB cap on a request

const char* reason_phrase(int status) {
  switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 409: return "Conflict";
    case 503: return "Service Unavailable";
    default: return "Error";
  }
}

// Read the whole request (headers + Content-Length body) into `req`. Returns
// false on EOF-before-headers, an oversize request, or a socket error.
bool read_request(int fd, std::string& req) {
  char buf[4096];
  std::size_t header_end = std::string::npos;
  while (header_end == std::string::npos) {
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    if (n <= 0) return false;
    req.append(buf, static_cast<std::size_t>(n));
    if (req.size() > kMaxRequestBytes) return false;
    header_end = req.find("\r\n\r\n");
  }

  // Content-Length (case-insensitive) → how many body bytes to still read.
  std::size_t content_length = 0;
  std::string lower = req.substr(0, header_end);
  for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const auto cl = lower.find("content-length:");
  if (cl != std::string::npos) {
    std::size_t p = cl + std::strlen("content-length:");
    while (p < lower.size() && (lower[p] == ' ' || lower[p] == '\t')) ++p;
    while (p < lower.size() && std::isdigit(static_cast<unsigned char>(lower[p]))) {
      content_length = content_length * 10 + static_cast<std::size_t>(lower[p] - '0');
      ++p;
    }
  }

  const std::size_t body_have = req.size() - (header_end + 4);
  std::size_t need = content_length > body_have ? content_length - body_have : 0;
  while (need > 0) {
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    if (n <= 0) break;  // short body — hand off what we have
    req.append(buf, static_cast<std::size_t>(n));
    if (req.size() > kMaxRequestBytes) return false;
    need -= std::min(need, static_cast<std::size_t>(n));
  }
  return true;
}

void write_all(int fd, const std::string& data) {
  const char* p = data.data();
  std::size_t remaining = data.size();
  while (remaining > 0) {
    const ssize_t w = ::write(fd, p, remaining);
    if (w < 0) {
      if (errno == EINTR) continue;
      return;
    }
    p += w;
    remaining -= static_cast<std::size_t>(w);
  }
}

}  // namespace

HttpServer::HttpServer(int port, Handler handler)
    : port_(port), handler_(std::move(handler)) {}

HttpServer::~HttpServer() {
  if (listen_fd_ >= 0) ::close(listen_fd_);
}

bool HttpServer::start() {
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) return false;

  int one = 1;
  ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1 — never off-host
  addr.sin_port = htons(static_cast<std::uint16_t>(port_));
  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  if (::listen(listen_fd_, 16) != 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  // Read back the actual port when an ephemeral (0) port was requested.
  sockaddr_in bound{};
  socklen_t len = sizeof(bound);
  if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound), &len) == 0)
    port_ = ntohs(bound.sin_port);
  return true;
}

void HttpServer::run() {
  while (!stop_) {
    pollfd pfd{};
    pfd.fd = listen_fd_;
    pfd.events = POLLIN;
    const int pr = ::poll(&pfd, 1, 200);  // 200ms tick so stop() is observed
    if (pr <= 0) continue;
    const int fd = ::accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) continue;
    serve_connection(fd);
    ::close(fd);
  }
}

void HttpServer::stop() {
  stop_ = true;
  if (listen_fd_ >= 0) ::shutdown(listen_fd_, SHUT_RDWR);
}

void HttpServer::serve_connection(int fd) {
  std::string req;
  if (!read_request(fd, req)) return;

  // Request line: METHOD SP TARGET SP HTTP/x.y
  const auto line_end = req.find("\r\n");
  const std::string line = req.substr(0, line_end == std::string::npos ? req.size() : line_end);
  const auto sp1 = line.find(' ');
  const auto sp2 = sp1 == std::string::npos ? std::string::npos : line.find(' ', sp1 + 1);

  RpcResponse resp{400, "{\"error\":\"BAD_REQUEST\",\"detail\":\"malformed request line\"}"};
  if (sp1 != std::string::npos && sp2 != std::string::npos) {
    const std::string method = line.substr(0, sp1);
    const std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const auto hdr_end = req.find("\r\n\r\n");
    const std::string body = hdr_end == std::string::npos ? std::string() : req.substr(hdr_end + 4);
    resp = handler_(method, target, body);
  }

  std::string out = "HTTP/1.1 " + std::to_string(resp.status) + " " + reason_phrase(resp.status) +
                    "\r\nContent-Type: application/json\r\nContent-Length: " +
                    std::to_string(resp.body.size()) + "\r\nConnection: close\r\n\r\n" + resp.body;
  write_all(fd, out);
}

}  // namespace txchain::rpc
