// Hex codec round-trip + rejection tests (Architecture Cryptography §8). Label: crypto.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>

#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/crypto/hashutil.hpp"

using txchain::crypto::Byte;
using txchain::crypto::from_hex;
using txchain::crypto::to_hex;

namespace {
template <std::size_t N>
void roundtrip() {
  std::array<Byte, N> x{};
  for (std::size_t i = 0; i < N; ++i) x[i] = static_cast<Byte>((i * 7 + 1) & 0xFF);

  const std::string h = to_hex(x);
  EXPECT_EQ(h.size(), 2 * N);
  for (char c : h) {
    const bool lc_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    EXPECT_TRUE(lc_hex) << "non-lowercase-hex char in output: " << c;
  }

  std::array<Byte, N> y{};
  EXPECT_TRUE(from_hex(h, y));
  EXPECT_EQ(x, y);
}
}  // namespace

// All the frozen fixed widths: Address20 (40), Digest32/PubKey32 (64), Sig64 (128).
TEST(Hex, RoundTripAllWidths) {
  roundtrip<20>();
  roundtrip<32>();
  roundtrip<64>();
}

TEST(Hex, NoPrefixNoSeparators) {
  std::array<Byte, 3> x{0xab, 0xcd, 0xef};
  EXPECT_EQ(to_hex(x), "abcdef");  // no 0x, no separators, lowercase
}

TEST(Hex, WrongLengthRejected) {
  std::array<Byte, 32> out{};
  EXPECT_FALSE(from_hex("00", out));                    // far too short
  EXPECT_FALSE(from_hex(std::string(63, '0'), out));    // one short + odd
  EXPECT_FALSE(from_hex(std::string(66, '0'), out));    // too long
  EXPECT_TRUE(from_hex(std::string(64, '0'), out));     // exactly right
}

TEST(Hex, NonHexRejected) {
  std::array<Byte, 4> out{};
  EXPECT_FALSE(from_hex("zzzzzzzz", out));  // right length (8), non-hex
  EXPECT_FALSE(from_hex("0g0g0g0g", out));
}

TEST(Hex, AcceptsUpperCase) {
  std::array<Byte, 2> out{};
  ASSERT_TRUE(from_hex("ABCD", out));
  EXPECT_EQ(out[0], 0xAB);
  EXPECT_EQ(out[1], 0xCD);
}
