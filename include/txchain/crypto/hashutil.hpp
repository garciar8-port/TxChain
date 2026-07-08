#pragma once
// Hex codec + PoW/cumulative-work helpers (Architecture Cryptography §7, §8).
//
// - Hex codec: the DISPLAY-ONLY binary<->chain.jsonl boundary. Fixed field
//   widths are enforced by type; a wrong-length string is a parse failure (no
//   throw). NEVER used in a hashing path (encoding rule R6).
// - PoW: leading-zero-bit counting + 2^D work, placed here so the identical
//   logic serves the miner (Pillar 3) and the validator (BAD_POW). A reported
//   leading-zero count can never disagree with the accept/reject decision.

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "txchain/crypto/fixedbytes.hpp"

namespace txchain::crypto {

// ---- Hex codec (display boundary only) ----

// Lowercase hex, no 0x prefix, no separators. Length == 2 * b.size().
std::string to_hex(ByteView b);

template <std::size_t N>
std::string to_hex(const std::array<Byte, N>& b) {
  return to_hex(ByteView(b.data(), b.size()));
}

// Decode fixed-width hex into `out`. A wrong length (!= 2*N) or any non-hex
// character returns false and leaves `out` unspecified — no throw on the hot
// path (anchor §4.3 step 2). Accepts upper- or lower-case hex digits.
template <std::size_t N>
[[nodiscard]] bool from_hex(std::string_view hex, std::array<Byte, N>& out) noexcept {
  if (hex.size() != 2 * N) return false;
  const auto nib = [](char c, int& v) noexcept -> bool {
    if (c >= '0' && c <= '9') { v = c - '0'; return true; }
    if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; return true; }
    if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; return true; }
    return false;
  };
  for (std::size_t i = 0; i < N; ++i) {
    int hi = 0;
    int lo = 0;
    if (!nib(hex[2 * i], hi) || !nib(hex[2 * i + 1], lo)) return false;
    out[i] = static_cast<Byte>((hi << 4) | lo);
  }
  return true;
}

// ---- PoW target + cumulative work ----

// Number of leading zero BITS of a 32-byte digest read as a big-endian 256-bit
// integer (MSB = hash[0]'s high bit). Range 0..256.
int leading_zero_bits(const Digest32& hash) noexcept;

// Does `hash` meet difficulty D? Equivalent to leading_zero_bits(hash) >= D:
// the first D/8 bytes are 0x00 and the next byte's top (D%8) bits are zero.
// Default D=16 ⇒ first two bytes 00 00.
[[nodiscard]] bool meets_difficulty(const Digest32& hash, unsigned D) noexcept;

// Per-block work for fork choice = 2^D, as a 128-bit accumulator-friendly value
// so cumulative work over a chain cannot overflow at demo heights/difficulty.
// Defined for D <= 127 (far beyond any real difficulty); saturates above that.
using Work = unsigned __int128;
Work block_work(unsigned D) noexcept;

}  // namespace txchain::crypto
