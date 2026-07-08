// Address derivation golden test (Architecture Cryptography §6; anchor Vector B).
// address(0x02 × 32) must equal the frozen 75877bb4…0631. Label: crypto.

#include <gtest/gtest.h>

#include "txchain/crypto/address.hpp"
#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/crypto/hashutil.hpp"

using txchain::crypto::Address20;
using txchain::crypto::address;
using txchain::crypto::address_matches;
using txchain::crypto::PubKey32;
using txchain::crypto::to_hex;

TEST(Address, GoldenVectorB) {
  PubKey32 pubkey;
  pubkey.fill(0x02);  // the anchor's placeholder pubkey
  const Address20 a = address(pubkey);
  EXPECT_EQ(to_hex(a), "75877bb41d393b5fb8455ce60ecd8dda001d0631");
}

TEST(Address, MatchesGateBehavior) {
  PubKey32 pubkey;
  pubkey.fill(0x02);
  const Address20 from = address(pubkey);

  // Correct (from, pubkey) pair -> match.
  EXPECT_TRUE(address_matches(from, pubkey));

  // A tampered `from` -> no match (the BAD_ADDR case).
  Address20 wrong = from;
  wrong[0] ^= 0x01;
  EXPECT_FALSE(address_matches(wrong, pubkey));

  // A different pubkey derives a different address -> no match.
  PubKey32 other;
  other.fill(0x03);
  EXPECT_FALSE(address_matches(from, other));
}
