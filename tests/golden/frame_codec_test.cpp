// Frame codec round-trip + partial-read/write + error tests (Architecture P2P
// §1). Uses a socketpair; a writer thread avoids deadlock on large payloads.
// Label: frame.

#include <gtest/gtest.h>

#include <csignal>
#include <cstdint>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

#include "txchain/net/frame.hpp"

namespace {

using txchain::net::Frame;
using txchain::net::FrameError;
using txchain::net::MsgType;
using txchain::net::read_frame;
using txchain::net::write_frame;
using txchain::net::MAX_FRAME_LEN;

// Ignore SIGPIPE process-wide so a write to a closed peer returns an error
// instead of killing the test binary (runs before main via static init).
const int kIgnoreSigpipe = []() {
  std::signal(SIGPIPE, SIG_IGN);
  return 0;
}();

std::vector<std::uint8_t> make_payload(std::size_t n) {
  std::vector<std::uint8_t> p(n);
  for (std::size_t i = 0; i < n; ++i) p[i] = static_cast<std::uint8_t>((i * 131 + 7) & 0xFF);
  return p;
}

// Write one frame from a thread, then close the write end; read it on the other.
txchain::net::Result<Frame> roundtrip(MsgType t, const std::vector<std::uint8_t>& p) {
  int fds[2];
  EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  std::thread writer([&]() {
    write_frame(fds[0], t, p);
    ::shutdown(fds[0], SHUT_WR);
  });
  auto r = read_frame(fds[1]);
  writer.join();
  ::close(fds[0]);
  ::close(fds[1]);
  return r;
}

// Send an arbitrary byte buffer one byte at a time, then close — to force
// header/body reassembly across many recv() calls.
void send_bytes_trickle(int fd, const std::vector<std::uint8_t>& bytes) {
  for (std::uint8_t b : bytes) {
    ASSERT_EQ(::send(fd, &b, 1, 0), 1);
  }
  ::shutdown(fd, SHUT_WR);
}

}  // namespace

TEST(FrameCodec, Constants) {
  EXPECT_EQ(MAX_FRAME_LEN, 2097152u);
  EXPECT_EQ(txchain::net::FRAME_MAGIC[0], 0x54);  // 'T'
  EXPECT_EQ(txchain::net::FRAME_MAGIC[1], 0x58);  // 'X'
  EXPECT_EQ(txchain::net::FRAME_MAGIC[2], 0x43);  // 'C'
  EXPECT_EQ(txchain::net::FRAME_MAGIC[3], 0x31);  // '1'
}

TEST(FrameCodec, RoundTripEmptyPayload) {
  auto r = roundtrip(MsgType::VERACK, {});
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.value.type, MsgType::VERACK);
  EXPECT_TRUE(r.value.payload.empty());
}

TEST(FrameCodec, RoundTripOneByte) {
  const std::vector<std::uint8_t> p{0xAB};
  auto r = roundtrip(MsgType::TX, p);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.value.type, MsgType::TX);
  EXPECT_EQ(r.value.payload, p);
}

TEST(FrameCodec, RoundTripMaxFrameLen) {
  const auto p = make_payload(MAX_FRAME_LEN);  // 2 MiB — spans many recv() chunks
  auto r = roundtrip(MsgType::BLOCK, p);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.value.type, MsgType::BLOCK);
  EXPECT_EQ(r.value.payload.size(), MAX_FRAME_LEN);
  EXPECT_EQ(r.value.payload, p);
}

// Header + body arriving one byte at a time must reassemble correctly.
TEST(FrameCodec, ReassemblesAcrossManyRecvChunks) {
  const std::vector<std::uint8_t> payload = make_payload(1000);
  // Build the exact on-wire frame: TXC1 | type | len(BE) | payload
  std::vector<std::uint8_t> wire{0x54, 0x58, 0x43, 0x31, static_cast<std::uint8_t>(MsgType::INV)};
  const std::uint32_t len = 1000;
  wire.push_back(static_cast<std::uint8_t>((len >> 24) & 0xFF));
  wire.push_back(static_cast<std::uint8_t>((len >> 16) & 0xFF));
  wire.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
  wire.push_back(static_cast<std::uint8_t>(len & 0xFF));
  wire.insert(wire.end(), payload.begin(), payload.end());

  int fds[2];
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  std::thread writer([&]() { send_bytes_trickle(fds[0], wire); });
  auto r = read_frame(fds[1]);
  writer.join();
  ::close(fds[0]);
  ::close(fds[1]);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.value.type, MsgType::INV);
  EXPECT_EQ(r.value.payload, payload);
}

// recv() returning 0 mid-body yields SHORT_READ.
TEST(FrameCodec, ShortReadOnMidFrameClose) {
  int fds[2];
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  std::thread writer([&]() {
    // Header claims length 100, but only 50 payload bytes are sent, then close.
    std::vector<std::uint8_t> hdr{0x54, 0x58, 0x43, 0x31,
                                  static_cast<std::uint8_t>(MsgType::TX), 0, 0, 0, 100};
    for (std::uint8_t b : hdr) (void)::send(fds[0], &b, 1, 0);
    std::vector<std::uint8_t> half(50, 0xEE);
    (void)::send(fds[0], half.data(), half.size(), 0);
    ::shutdown(fds[0], SHUT_WR);
  });
  auto r = read_frame(fds[1]);
  writer.join();
  ::close(fds[0]);
  ::close(fds[1]);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, FrameError::SHORT_READ);
}

// A bad magic errors BAD_MAGIC (before any body allocation).
TEST(FrameCodec, BadMagicRejected) {
  int fds[2];
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  std::thread writer([&]() {
    std::vector<std::uint8_t> hdr{0xDE, 0xAD, 0xBE, 0xEF, 1, 0, 0, 0, 0};  // wrong magic
    (void)::send(fds[0], hdr.data(), hdr.size(), 0);
    ::shutdown(fds[0], SHUT_WR);
  });
  auto r = read_frame(fds[1]);
  writer.join();
  ::close(fds[0]);
  ::close(fds[1]);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, FrameError::BAD_MAGIC);
}

// A declared length over the cap errors OVERSIZE (before allocating the body).
TEST(FrameCodec, OversizeRejectedBeforeAlloc) {
  int fds[2];
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  std::thread writer([&]() {
    const std::uint32_t big = MAX_FRAME_LEN + 1;
    std::vector<std::uint8_t> hdr{0x54, 0x58, 0x43, 0x31,
                                  static_cast<std::uint8_t>(MsgType::BLOCK),
                                  static_cast<std::uint8_t>((big >> 24) & 0xFF),
                                  static_cast<std::uint8_t>((big >> 16) & 0xFF),
                                  static_cast<std::uint8_t>((big >> 8) & 0xFF),
                                  static_cast<std::uint8_t>(big & 0xFF)};
    (void)::send(fds[0], hdr.data(), hdr.size(), 0);
    ::shutdown(fds[0], SHUT_WR);
  });
  auto r = read_frame(fds[1]);
  writer.join();
  ::close(fds[0]);
  ::close(fds[1]);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, FrameError::OVERSIZE);
}

// write_frame refuses an over-cap payload (a local bug, not a peer fault).
TEST(FrameCodec, WriteRefusesOversizePayload) {
  int fds[2];
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  std::vector<std::uint8_t> too_big(static_cast<std::size_t>(MAX_FRAME_LEN) + 1, 0);
  auto w = write_frame(fds[0], MsgType::BLOCK, too_big);
  EXPECT_FALSE(w.ok);
  EXPECT_EQ(w.error, FrameError::OVERSIZE);
  ::close(fds[0]);
  ::close(fds[1]);
}
