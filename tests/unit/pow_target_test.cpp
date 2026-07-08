// PoW target + cumulative-work tests (Architecture Cryptography §7). Label: crypto.

#include <gtest/gtest.h>

#include <cstddef>

#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/crypto/hashutil.hpp"

using txchain::crypto::block_work;
using txchain::crypto::Byte;
using txchain::crypto::Digest32;
using txchain::crypto::leading_zero_bits;
using txchain::crypto::meets_difficulty;
using txchain::crypto::Work;

namespace {
// A 32-byte digest with EXACTLY `z` leading zero bits: bit index z (from the
// MSB) is 1, all earlier bits are 0. Requires 0 <= z < 256.
Digest32 with_leading_zeros(int z) {
  Digest32 h{};  // all zero
  const std::size_t byte = static_cast<std::size_t>(z) / 8;
  const int bit = 7 - (z % 8);
  h[byte] = static_cast<Byte>(1 << bit);
  return h;
}
}  // namespace

TEST(Pow, LeadingZeroBitsBoundaries) {
  EXPECT_EQ(leading_zero_bits(with_leading_zeros(0)), 0);    // MSB set
  EXPECT_EQ(leading_zero_bits(with_leading_zeros(7)), 7);    // sub-byte boundary
  EXPECT_EQ(leading_zero_bits(with_leading_zeros(8)), 8);    // byte boundary
  EXPECT_EQ(leading_zero_bits(with_leading_zeros(16)), 16);  // default difficulty
  EXPECT_EQ(leading_zero_bits(with_leading_zeros(255)), 255);
  const Digest32 all_zero{};
  EXPECT_EQ(leading_zero_bits(all_zero), 256);
}

// meets_difficulty(h, D) must equal (leading_zero_bits(h) >= D) for all cases.
TEST(Pow, MeetsDifficultyAgreesWithCount) {
  const int zs[] = {0, 7, 8, 16, 255};
  const unsigned Ds[] = {0u, 7u, 8u, 16u, 255u, 256u};
  for (int z : zs) {
    const Digest32 h = with_leading_zeros(z);
    for (unsigned D : Ds) {
      const bool expected = static_cast<unsigned>(leading_zero_bits(h)) >= D;
      EXPECT_EQ(meets_difficulty(h, D), expected) << "z=" << z << " D=" << D;
    }
  }
  // all-zero digest meets every difficulty up to 256.
  const Digest32 all_zero{};
  EXPECT_TRUE(meets_difficulty(all_zero, 256u));
}

// Default D=16 ⇒ first two bytes must be 00 00.
TEST(Pow, DefaultDifficulty16) {
  Digest32 h{};
  h[2] = 0xFF;  // 16 leading zero bits exactly
  EXPECT_TRUE(meets_difficulty(h, 16));
  h[1] = 0x01;  // now only 15 leading zero bits
  EXPECT_FALSE(meets_difficulty(h, 16));
}

// block_work(D) == 2^D for every D whose result fits in 128 bits.
TEST(Pow, BlockWorkIsPowerOfTwo) {
  const unsigned Ds[] = {0u, 7u, 8u, 16u, 64u, 127u};
  for (unsigned D : Ds) {
    const Work expected = static_cast<Work>(1) << D;
    EXPECT_TRUE(block_work(D) == expected) << "D=" << D;
  }
  // Cumulative work sums without overflow at demo scale: 100 blocks at D=16.
  Work cum = 0;
  for (int i = 0; i < 100; ++i) cum += block_work(16);
  EXPECT_TRUE(cum == static_cast<Work>(100) * block_work(16));
}
