#include "txchain/rpc/http_client.hpp"

#include <cctype>
#include <cstddef>
#include <cstring>
#include <string>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace txchain::rpc {

HttpResult http_request(const std::string& method, const std::string& host, int port,
                        const std::string& path, const std::string& body) {
  HttpResult out;

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  const std::string port_str = std::to_string(port);
  if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || res == nullptr)
    return out;

  const int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) {
    ::freeaddrinfo(res);
    return out;
  }
  if (::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
    ::close(fd);
    ::freeaddrinfo(res);
    return out;
  }
  ::freeaddrinfo(res);

  std::string req = method + " " + path + " HTTP/1.1\r\nHost: " + host +
                    "\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\n\r\n" + body;
  {
    const char* p = req.data();
    std::size_t remaining = req.size();
    while (remaining > 0) {
      const ssize_t w = ::write(fd, p, remaining);
      if (w <= 0) {
        ::close(fd);
        return out;
      }
      p += w;
      remaining -= static_cast<std::size_t>(w);
    }
  }

  std::string resp;
  char buf[4096];
  ssize_t n;
  while ((n = ::read(fd, buf, sizeof(buf))) > 0) resp.append(buf, static_cast<std::size_t>(n));
  ::close(fd);

  // Parse "HTTP/1.1 <status> <reason>" and the body after the blank line.
  const auto sp1 = resp.find(' ');
  if (sp1 == std::string::npos) return out;
  int status = 0;
  std::size_t p = sp1 + 1;
  while (p < resp.size() && std::isdigit(static_cast<unsigned char>(resp[p]))) {
    status = status * 10 + (resp[p] - '0');
    ++p;
  }
  const auto hdr_end = resp.find("\r\n\r\n");
  out.ok = true;
  out.status = status;
  out.body = hdr_end == std::string::npos ? std::string() : resp.substr(hdr_end + 4);
  return out;
}

}  // namespace txchain::rpc
