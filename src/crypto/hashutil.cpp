#include "txchain/crypto/hashutil.hpp"

#include <cstddef>

namespace txchain::crypto {

std::string to_hex(ByteView b) {
  static const char* kHex = "0123456789abcdef";
  std::string s;
  s.reserve(b.size() * 2);
  for (std::size_t i = 0; i < b.size(); ++i) {
    const Byte byte = b.data()[i];
    s.push_back(kHex[byte >> 4]);
    s.push_back(kHex[byte & 0x0F]);
  }
  return s;
}

int leading_zero_bits(const Digest32& hash) noexcept {
  int count = 0;
  for (std::size_t i = 0; i < hash.size(); ++i) {
    const Byte b = hash[i];
    if (b == 0) {
      count += 8;
      continue;
    }
    // Count leading zero bits within this (non-zero) byte, then stop.
    for (int bit = 7; bit >= 0; --bit) {
      if ((b >> bit) & 0x01) return count;
      ++count;
    }
  }
  return count;  // all bytes zero -> 256
}

bool meets_difficulty(const Digest32& hash, unsigned D) noexcept {
  // Single source of truth: the accept/reject decision is exactly the reported
  // leading-zero count vs the target, so they can never disagree.
  return static_cast<unsigned>(leading_zero_bits(hash)) >= D;
}

Work block_work(unsigned D) noexcept {
  // 2^D. Real difficulty is ~16; Work is 128-bit, so guard the shift and
  // saturate for the (non-physical) D >= 128 case rather than invoke UB.
  if (D >= 128) return ~static_cast<Work>(0);
  return static_cast<Work>(1) << D;
}

}  // namespace txchain::crypto
