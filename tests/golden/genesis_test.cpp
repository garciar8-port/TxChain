// Genesis allocation table cross-check (Architecture Chain/State §3). Re-derives
// each address in C++ from the frozen seed rule and asserts the table
// byte-for-byte, plus the genesis-block-0 shape. Label: chain.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "txchain/chain/genesis.hpp"
#include "txchain/chain/types.hpp"
#include "txchain/crypto/address.hpp"
#include "txchain/crypto/ed25519.hpp"
#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/crypto/sha256.hpp"

namespace {

using txchain::chain::Address;
using txchain::chain::Block;
using txchain::chain::GENESIS_ALLOC;
using txchain::chain::GENESIS_COUNT;
using txchain::chain::GENESIS_SUPPLY;
using txchain::chain::genesisBlock;
using txchain::crypto::Byte;
using txchain::crypto::ByteView;
using txchain::crypto::to_hex;

constexpr const char* kEmptyHash =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

// seed = SHA-256("txchain-genesis-"||name); addr = SHA-256(pubkey)[:20].
// (Digest32 and Seed32 are both std::array<uint8_t,32>, so the digest binds
// directly as the seed.)
Address derive_addr(const std::string& name) {
  const std::string s = "txchain-genesis-" + name;
  const auto seed =
      txchain::crypto::sha256(ByteView(reinterpret_cast<const Byte*>(s.data()), s.size()));
  const auto kp = txchain::crypto::keygen_from_seed(seed);
  return txchain::crypto::address(kp.pubkey);
}

}  // namespace

TEST(Genesis, TableRederivesByteForByte) {
  const char* names[GENESIS_COUNT] = {"racheal", "oliver", "matthew", "sophia", "daniel"};
  ASSERT_EQ(GENESIS_COUNT, 5u);
  for (std::size_t i = 0; i < GENESIS_COUNT; ++i) {
    EXPECT_EQ(to_hex(derive_addr(names[i])), to_hex(GENESIS_ALLOC[i].addr))
        << "identity " << names[i];
    EXPECT_EQ(GENESIS_ALLOC[i].amount, 1000u);
  }
}

TEST(Genesis, SupplyIs5000) {
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < GENESIS_COUNT; ++i) sum += GENESIS_ALLOC[i].amount;
  EXPECT_EQ(sum, 5000u);
  EXPECT_EQ(GENESIS_SUPPLY, 5000u);
}

TEST(Genesis, GenesisBlockShape) {
  const Block g = genesisBlock();
  EXPECT_EQ(g.header.index, 0u);
  EXPECT_EQ(g.header.nonce, 0u);
  EXPECT_TRUE(g.txns.empty());
  for (Byte b : g.header.prevHash) EXPECT_EQ(b, 0);  // prevHash all zero
  EXPECT_EQ(to_hex(g.header.txnsHash), kEmptyHash);
  EXPECT_EQ(to_hex(g.computeTxnsHash()), kEmptyHash);  // empty-block txnsHash
}

TEST(Genesis, AddressRuleVectorBUnchanged) {
  txchain::crypto::PubKey32 pub;
  pub.fill(0x02);
  EXPECT_EQ(to_hex(txchain::crypto::address(pub)),
            "75877bb41d393b5fb8455ce60ecd8dda001d0631");
}
