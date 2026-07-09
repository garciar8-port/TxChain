#pragma once
// Length-prefixed frame codec (Architecture P2P §1) — the Phase-0 byte-boundary
// layer P2P (Pillar 4) structurally depends on. TCP is a byte stream, so a
// single recv() may coalesce two messages or split one; this codec restores
// discrete messages.
//
// 9-byte fixed header, then `length` payload bytes:
//   offset 0        4      5                    9
//          | magic  | type |   length (u32 BE)  |  payload[length]  |
//          | u8[4]  | u8   |                    |                   |
//
// The envelope is deliberately OUTSIDE every hashed/signed object (anchor R5):
// its bytes never enter txid, txnsHash, or blockHash. It frames opaque payload
// bytes and never parses them.

#include <array>
#include <cstdint>
#include <vector>

namespace txchain::net {

// Frozen magic "TXC1" and the 2 MiB payload cap.
inline constexpr std::array<std::uint8_t, 4> FRAME_MAGIC = {0x54, 0x58, 0x43, 0x31};
inline constexpr std::uint32_t MAX_FRAME_LEN = 2u * 1024u * 1024u;  // 2097152
inline constexpr std::size_t FRAME_HEADER_LEN = 9;

// Message-type discriminator. A stub catalog is sufficient for M0; the payload
// layouts for these are Pillar 4 (P2P §3).
enum class MsgType : std::uint8_t {
  VERSION = 1,
  VERACK = 2,
  INV = 3,
  GETDATA = 4,
  TX = 5,
  BLOCK = 6,
  GETHEADERS = 7,
  HEADERS = 8,
  GETADDR = 9,
  ADDR = 10,
};

enum class FrameError : std::uint8_t {
  BAD_MAGIC,    // header magic != "TXC1"
  OVERSIZE,     // declared length > MAX_FRAME_LEN (detected before allocating)
  SHORT_READ,   // peer closed mid-frame (recv returned 0)
  IO_ERROR,     // recv/send failed
};

struct Frame {
  MsgType type{};
  std::vector<std::uint8_t> payload;
};

inline bool operator==(const Frame& a, const Frame& b) noexcept {
  return a.type == b.type && a.payload == b.payload;
}
inline bool operator!=(const Frame& a, const Frame& b) noexcept { return !(a == b); }

// Minimal Result<T>: either a value or a FrameError. `ok` selects which member
// is valid. (A project-wide Result type may generalize this later; the codec
// only needs value-or-error.)
template <typename T>
struct Result {
  bool ok = false;
  FrameError error = FrameError::IO_ERROR;
  T value{};

  static Result success(T v) { return Result{true, FrameError::IO_ERROR, std::move(v)}; }
  static Result failure(FrameError e) { return Result{false, e, T{}}; }
  explicit operator bool() const noexcept { return ok; }
};

template <>
struct Result<void> {
  bool ok = false;
  FrameError error = FrameError::IO_ERROR;

  static Result success() { return Result{true, FrameError::IO_ERROR}; }
  static Result failure(FrameError e) { return Result{false, e}; }
  explicit operator bool() const noexcept { return ok; }
};

// Read exactly one frame from `fd`. Validates magic and length BEFORE allocating
// the body; reassembles a header/body that arrives across multiple recv() calls.
Result<Frame> read_frame(int fd);

// Write one frame (header + payload) to `fd`, looping over partial send().
Result<void> write_frame(int fd, MsgType type, const std::vector<std::uint8_t>& payload);

}  // namespace txchain::net
