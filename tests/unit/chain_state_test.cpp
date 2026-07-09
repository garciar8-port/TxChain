// Chain / account-state tests (Architecture Chain/State §1-§3). applyGenesis
// funds exactly the 5 identities; a fresh Chain starts at genesis height 0 and
// returns {0,0} for unknown addresses. Label: chain.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>

#include "txchain/chain/chain.hpp"
#include "txchain/chain/genesis.hpp"
#include "txchain/chain/types.hpp"
#include "txchain/serialize/canonical.hpp"

namespace {

using txchain::chain::AccountState;
using txchain::chain::Address;
using txchain::chain::applyGenesis;
using txchain::chain::BlockHeader;
using txchain::chain::Chain;
using txchain::chain::GENESIS_ALLOC;
using txchain::chain::GENESIS_COUNT;
using txchain::chain::genesisBlock;
using txchain::chain::Txn;
using txchain::chain::Work;

}  // namespace

TEST(ChainState, ApplyGenesisPopulatesFive) {
  std::map<Address, AccountState> st;
  applyGenesis(st);
  EXPECT_EQ(st.size(), 5u);
  std::uint64_t total = 0;
  for (std::size_t i = 0; i < GENESIS_COUNT; ++i) {
    const auto it = st.find(GENESIS_ALLOC[i].addr);
    ASSERT_NE(it, st.end());
    EXPECT_EQ(it->second.balance, 1000u);
    EXPECT_EQ(it->second.nonce, 0u);
    total += it->second.balance;
  }
  EXPECT_EQ(total, 5000u);
}

TEST(ChainState, ApplyGenesisClearsFirst) {
  std::map<Address, AccountState> st;
  Address junk{};
  junk[0] = 0xFF;
  st[junk] = AccountState{999, 7};
  applyGenesis(st);  // must clear the pre-existing entry
  EXPECT_EQ(st.size(), 5u);
  EXPECT_EQ(st.find(junk), st.end());
}

TEST(ChainState, ChainStartsAtGenesis) {
  Chain c;
  EXPECT_EQ(c.height(), 0u);
  EXPECT_EQ(c.numBlocks(), 1u);
  EXPECT_TRUE(c.cumWork() == static_cast<Work>(0));  // __int128: avoid gtest streaming

  // genesis identities are funded
  EXPECT_EQ(c.account(GENESIS_ALLOC[0].addr).balance, 1000u);
  EXPECT_EQ(c.account(GENESIS_ALLOC[4].addr).balance, 1000u);

  // an unknown (non-genesis) address returns {0,0}
  Address zero{};
  EXPECT_EQ(c.account(zero).balance, 0u);
  EXPECT_EQ(c.account(zero).nonce, 0u);

  // tip is the genesis block
  EXPECT_EQ(c.tipHash(), genesisBlock().header.hash());
  EXPECT_EQ(c.blockAt(0).header.index, 0u);
}

TEST(ChainState, CanonicalSizesUnchanged) {
  Txn t{};
  t.ver = 1;
  EXPECT_EQ(txchain::serialize::serialize(t).size(), 153u);
  BlockHeader h{};
  EXPECT_EQ(txchain::serialize::serialize(h).size(), 88u);
}
