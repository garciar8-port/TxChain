#include "txchain/net/frame.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>

#include <sys/socket.h>

namespace txchain::net {

namespace {

// Suppress SIGPIPE on write where the platform supports the send() flag.
#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

// Read exactly `n` bytes into `buf`, looping over partial recv(). A single
// recv() is never assumed to return everything. recv()==0 mid-read means the
// peer closed → SHORT_READ.
bool read_exact(int fd, std::uint8_t* buf, std::size_t n, FrameError& err) {
  std::size_t got = 0;
  while (got < n) {
    const ssize_t r = ::recv(fd, buf + got, n - got, 0);
    if (r == 0) {
      err = FrameError::SHORT_READ;
      return false;
    }
    if (r < 0) {
      if (errno == EINTR) continue;
      err = FrameError::IO_ERROR;
      return false;
    }
    got += static_cast<std::size_t>(r);
  }
  return true;
}

// Write all `n` bytes from `buf`, looping over partial send().
bool write_all(int fd, const std::uint8_t* buf, std::size_t n, FrameError& err) {
  std::size_t sent = 0;
  while (sent < n) {
    const ssize_t w = ::send(fd, buf + sent, n - sent, kSendFlags);
    if (w < 0) {
      if (errno == EINTR) continue;
      err = FrameError::IO_ERROR;
      return false;
    }
    sent += static_cast<std::size_t>(w);
  }
  return true;
}

}  // namespace

Result<Frame> read_frame(int fd) {
  std::uint8_t hdr[FRAME_HEADER_LEN];
  FrameError err = FrameError::IO_ERROR;

  if (!read_exact(fd, hdr, FRAME_HEADER_LEN, err)) return Result<Frame>::failure(err);

  if (hdr[0] != FRAME_MAGIC[0] || hdr[1] != FRAME_MAGIC[1] ||
      hdr[2] != FRAME_MAGIC[2] || hdr[3] != FRAME_MAGIC[3]) {
    return Result<Frame>::failure(FrameError::BAD_MAGIC);
  }

  const auto type = static_cast<MsgType>(hdr[4]);
  const std::uint32_t length = (static_cast<std::uint32_t>(hdr[5]) << 24) |
                               (static_cast<std::uint32_t>(hdr[6]) << 16) |
                               (static_cast<std::uint32_t>(hdr[7]) << 8) |
                               static_cast<std::uint32_t>(hdr[8]);

  // Validate the declared size BEFORE allocating the body.
  if (length > MAX_FRAME_LEN) return Result<Frame>::failure(FrameError::OVERSIZE);

  Frame frame;
  frame.type = type;
  frame.payload.resize(length);
  if (length > 0 && !read_exact(fd, frame.payload.data(), length, err)) {
    return Result<Frame>::failure(err);
  }
  return Result<Frame>::success(std::move(frame));
}

Result<void> write_frame(int fd, MsgType type, const std::vector<std::uint8_t>& payload) {
  // A payload over the cap is a LOCAL bug (not a peer fault); refuse to send.
  if (payload.size() > MAX_FRAME_LEN) return Result<void>::failure(FrameError::OVERSIZE);

  const std::uint32_t length = static_cast<std::uint32_t>(payload.size());
  std::uint8_t hdr[FRAME_HEADER_LEN];
  hdr[0] = FRAME_MAGIC[0];
  hdr[1] = FRAME_MAGIC[1];
  hdr[2] = FRAME_MAGIC[2];
  hdr[3] = FRAME_MAGIC[3];
  hdr[4] = static_cast<std::uint8_t>(type);
  hdr[5] = static_cast<std::uint8_t>((length >> 24) & 0xFF);  // big-endian
  hdr[6] = static_cast<std::uint8_t>((length >> 16) & 0xFF);
  hdr[7] = static_cast<std::uint8_t>((length >> 8) & 0xFF);
  hdr[8] = static_cast<std::uint8_t>(length & 0xFF);

  FrameError err = FrameError::IO_ERROR;
  if (!write_all(fd, hdr, FRAME_HEADER_LEN, err)) return Result<void>::failure(err);
  if (!payload.empty() && !write_all(fd, payload.data(), payload.size(), err)) {
    return Result<void>::failure(err);
  }
  return Result<void>::success();
}

}  // namespace txchain::net
