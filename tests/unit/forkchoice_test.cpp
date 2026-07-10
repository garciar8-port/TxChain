// Cumulative-work fork choice + the T2 import evaluation (Architecture Mempool/PoW
// §5). candidateWins is strict (ties keep the incumbent); evaluateImport validates
// a competing chain from genesis FIRST (work never buys validity) then compares
// cumulative work. Chains are mined at a low D for speed. Label: pow.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "txchain/chain/chain.hpp"
#include "txchain/chain/genesis.hpp"
#include "txchain/chain/params.hpp"
#include "txchain/chain/store.hpp"
#include "txchain/chain/types.hpp"
#include "txchain/chain/validate.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/mempool/mempool.hpp"
#include "txchain/pow/candidate.hpp"
#include "txchain/pow/forkchoice.hpp"
#include "txchain/pow/miner.hpp"

namespace {

namespace fs = std::filesystem;

using txchain::chain::AccountState;
using txchain::chain::Address;
using txchain::chain::Chain;
using txchain::chain::ChainStore;
using txchain::chain::COINBASE_REWARD;
using txchain::chain::genesisBlock;
using txchain::chain::Reason;
using txchain::chain::Work;
using txchain::mempool::Mempool;
using txchain::pow::buildCandidate;
using txchain::pow::candidateWins;
using txchain::pow::cumulativeWork;
using txchain::pow::evaluateImport;
using txchain::serialize::Block;

constexpr std::uint64_t NOW = 2000000000;
constexpr unsigned D = 8;  // fast to mine

Address miner() {
  Address a{};
  a.fill(0x55);
  return a;
}
Mempool::AccountLookup empty_lookup(std::map<Address, AccountState>& st) {
  return [&st](const Address& a) -> AccountState {
    const auto it = st.find(a);
    return it == st.end() ? AccountState{} : it->second;
  };
}

// A valid mined chain of `n` coinbase-only blocks at difficulty D (genesis + n).
std::vector<Block> mine_chain(int n) {
  Chain c(D, COINBASE_REWARD);
  std::map<Address, AccountState> st;
  Mempool mp(empty_lookup(st));
  std::atomic<bool> stop{false};
  std::vector<Block> blocks = {genesisBlock()};
  for (int h = 1; h <= n; ++h) {
    const auto& tip = c.blockAt(c.height());
    const auto cand =
        buildCandidate(miner(), c.height(), c.tipHash(), tip.header.timestamp, NOW + h, mp,
                       empty_lookup(st));
    const auto mined = txchain::pow::mine(cand, D, stop);
    c.connectBlock(*mined, NOW + h);
    blocks.push_back(*mined);
  }
  return blocks;
}

}  // namespace

TEST(ForkChoice, CandidateWinsIsStrict) {
  const Work w = txchain::pow::blockWork(16);  // 2^16
  EXPECT_TRUE(candidateWins(2 * w, w));
  EXPECT_FALSE(candidateWins(w, 2 * w));
  EXPECT_FALSE(candidateWins(w, w));      // tie keeps incumbent
  EXPECT_TRUE(candidateWins(w + 1, w));   // strict, boundary
  EXPECT_FALSE(candidateWins(w, w + 1));
}

TEST(ForkChoice, CumulativeWorkFixedD) {
  const auto chain = mine_chain(3);  // genesis + 3
  EXPECT_TRUE(cumulativeWork(chain, 16) == static_cast<Work>(3) * txchain::pow::blockWork(16));
  // Genesis alone is zero work.
  EXPECT_TRUE(cumulativeWork({genesisBlock()}, 16) == static_cast<Work>(0));
}

TEST(ForkChoice, CumulativeWorkNoOverflow) {
  // 2^120 fits in the 128-bit accumulator (no wrap); demo sums are tiny by comparison.
  EXPECT_TRUE(txchain::pow::blockWork(120) > static_cast<Work>(0));
  EXPECT_TRUE(cumulativeWork(mine_chain(2), 100) == static_cast<Work>(2) * txchain::pow::blockWork(100));
}

TEST(ForkChoice, HeavierValidImportWins) {
  const auto active = mine_chain(2);
  const auto candidate = mine_chain(3);  // strictly heavier + valid
  const auto r = evaluateImport(active, candidate, NOW + 100, D, COINBASE_REWARD);
  EXPECT_TRUE(r.valid);
  EXPECT_TRUE(r.won);
  EXPECT_TRUE(r.candidate_work > r.active_work);
}

TEST(ForkChoice, LighterImportLoses) {
  const auto active = mine_chain(3);
  const auto candidate = mine_chain(2);  // valid but lighter
  const auto r = evaluateImport(active, candidate, NOW + 100, D, COINBASE_REWARD);
  EXPECT_TRUE(r.valid);
  EXPECT_FALSE(r.won);
}

TEST(ForkChoice, TieKeepsIncumbent) {
  const auto active = mine_chain(2);
  const auto candidate = mine_chain(2);  // equal cumulative work
  const auto r = evaluateImport(active, candidate, NOW + 100, D, COINBASE_REWARD);
  EXPECT_TRUE(r.valid);
  EXPECT_FALSE(r.won);  // strict > — a tie does not swap
}

TEST(ForkChoice, InvalidHeavierImportRejected) {
  const auto active = mine_chain(2);
  auto candidate = mine_chain(3);  // heavier...
  // ...but break block 2's PoW (work never buys validity).
  while (txchain::crypto::meets_difficulty(candidate[2].header.hash(), D)) ++candidate[2].header.nonce;
  const auto r = evaluateImport(active, candidate, NOW + 100, D, COINBASE_REWARD);
  EXPECT_FALSE(r.valid);
  EXPECT_FALSE(r.won);  // rejected regardless of claimed work
  EXPECT_EQ(r.status.reason, Reason::BAD_POW);
  EXPECT_EQ(r.status.failIndex, 2u);
}

TEST(ForkChoice, StoreRewriteRoundTrip) {
  static int n = 0;
  const auto path = (fs::temp_directory_path() / ("txchain_fc_rewrite_" + std::to_string(n++) + ".jsonl")).string();
  const ChainStore store(path);
  const auto chain = mine_chain(2);
  ASSERT_TRUE(store.rewrite(chain));
  const auto loaded = store.load();
  ASSERT_TRUE(loaded.status.ok);
  ASSERT_EQ(loaded.blocks.size(), chain.size());
  for (std::size_t i = 0; i < chain.size(); ++i) EXPECT_TRUE(loaded.blocks[i] == chain[i]);
  fs::remove(path);
}
