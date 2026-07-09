// From-genesis replay validator + monitor verify (Architecture Chain/State
// §5–§7) — the M1 tamper-evidence demo. Label: chain.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "txchain/chain/chain.hpp"
#include "txchain/chain/genesis.hpp"
#include "txchain/chain/reason.hpp"
#include "txchain/chain/types.hpp"
#include "txchain/chain/validate.hpp"

namespace {

using txchain::chain::Block;
using txchain::chain::Chain;
using txchain::chain::genesisBlock;
using txchain::chain::GENESIS_ALLOC;
using txchain::chain::GENESIS_TIMESTAMP;
using txchain::chain::Hash256;
using txchain::chain::monitor_verify_exit_code;
using txchain::chain::monitor_verify_line;
using txchain::chain::Reason;
using txchain::chain::reasonName;
using txchain::chain::replayFromGenesis;
using txchain::chain::Txn;

constexpr std::uint64_t NOW = 2000000000;

// genesis + (n-1) correctly-linked empty cadence blocks.
std::vector<Block> buildChain(std::size_t n) {
  std::vector<Block> v;
  const Block g = genesisBlock();
  v.push_back(g);
  Hash256 prev = g.header.hash();
  std::uint64_t ts = g.header.timestamp;
  for (std::size_t i = 1; i < n; ++i) {
    Block b;
    b.header.index = i;
    ts += 1;
    b.header.timestamp = ts;
    b.header.prevHash = prev;
    b.header.txnsHash = b.computeTxnsHash();  // empty
    b.header.nonce = 0;
    v.push_back(b);
    prev = b.header.hash();
  }
  return v;
}

}  // namespace

TEST(Replay, CleanChainOk) {
  const auto chain = buildChain(8);
  const auto r = replayFromGenesis(chain, NOW);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(monitor_verify_line(r, chain.size()), "OK 8 blocks");
  EXPECT_EQ(monitor_verify_exit_code(r), 0);
}

TEST(Replay, TamperPrevHashNamesBlock7) {
  auto chain = buildChain(8);
  chain[7].header.prevHash[0] ^= 0x01;  // block 7 no longer links to block 6
  const auto r = replayFromGenesis(chain, NOW);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.failIndex, 7u);
  EXPECT_EQ(r.reason, Reason::BAD_LINK);
  EXPECT_EQ(monitor_verify_line(r, chain.size()), "FAIL block 7: BAD_LINK (prevHash mismatch)");
  EXPECT_NE(monitor_verify_exit_code(r), 0);
}

TEST(Replay, TamperTxnsHash) {
  auto chain = buildChain(5);
  chain[3].header.txnsHash[0] ^= 0x01;  // disagrees with computeTxnsHash()
  const auto r = replayFromGenesis(chain, NOW);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.failIndex, 3u);
  EXPECT_EQ(r.reason, Reason::BAD_TXNS_HASH);
  EXPECT_EQ(monitor_verify_line(r, chain.size()),
            "FAIL block 3: BAD_TXNS_HASH (txns commitment mismatch)");
}

// A self-transfer mints under the M1 placeholder apply; the supply invariant is
// the backstop that catches it (Σ balance != 5000).
TEST(Replay, SupplyInvariantCatchesMint) {
  auto chain = buildChain(1);  // genesis only
  Block b1;
  b1.header.index = 1;
  b1.header.timestamp = GENESIS_TIMESTAMP + 1;
  b1.header.prevHash = chain[0].header.hash();
  Txn t;
  t.ver = 1;
  t.from = GENESIS_ALLOC[0].addr;
  t.to = GENESIS_ALLOC[0].addr;  // self-transfer
  t.amount = 100;
  b1.txns.push_back(t);
  b1.header.txnsHash = b1.computeTxnsHash();
  b1.header.nonce = 0;
  chain.push_back(b1);

  const auto r = replayFromGenesis(chain, NOW);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.reason, Reason::BAD_SUPPLY);
  EXPECT_EQ(r.failIndex, 1u);  // failIndex = height
  EXPECT_EQ(monitor_verify_line(r, chain.size()), "FAIL block 1: BAD_SUPPLY (supply mismatch)");
}

TEST(Replay, CleanChainSupplyIs5000) {
  const auto chain = buildChain(4);
  EXPECT_TRUE(replayFromGenesis(chain, NOW).ok);  // Σ balance == 5000, no mint
}

TEST(Replay, GenesisBadPrevHash) {
  auto chain = buildChain(1);
  chain[0].header.prevHash[0] = 0xFF;  // genesis prevHash must be all-zero
  const auto r = replayFromGenesis(chain, NOW);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.failIndex, 0u);
  EXPECT_EQ(r.reason, Reason::BAD_LINK);
}

TEST(Replay, GenesisBadTxnsHash) {
  auto chain = buildChain(1);
  chain[0].header.txnsHash[0] ^= 0x01;  // != SHA-256("")
  const auto r = replayFromGenesis(chain, NOW);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.failIndex, 0u);
  EXPECT_EQ(r.reason, Reason::BAD_TXNS_HASH);
}

TEST(Replay, TimestampOutOfRange) {
  auto chain = buildChain(2);
  chain[1].header.timestamp = chain[0].header.timestamp;  // not strictly increasing
  chain[1].header.txnsHash = chain[1].computeTxnsHash();
  const auto r = replayFromGenesis(chain, NOW);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.reason, Reason::BAD_LINK);
  EXPECT_EQ(r.detail, "timestamp out of range");
}

TEST(Replay, ReasonNamesExactSpelling) {
  EXPECT_STREQ(reasonName(Reason::OK), "OK");
  EXPECT_STREQ(reasonName(Reason::BAD_LINK), "BAD_LINK");
  EXPECT_STREQ(reasonName(Reason::BAD_TXNS_HASH), "BAD_TXNS_HASH");
  EXPECT_STREQ(reasonName(Reason::BAD_POW), "BAD_POW");
  EXPECT_STREQ(reasonName(Reason::BAD_SIG), "BAD_SIG");
  EXPECT_STREQ(reasonName(Reason::BAD_ADDR), "BAD_ADDR");
  EXPECT_STREQ(reasonName(Reason::STALE_NONCE), "STALE_NONCE");
  EXPECT_STREQ(reasonName(Reason::INSUFFICIENT_FUNDS), "INSUFFICIENT_FUNDS");
  EXPECT_STREQ(reasonName(Reason::BAD_SUPPLY), "BAD_SUPPLY");
}

// validateChain() (uses cached-state-free replay) agrees with replayFromGenesis.
TEST(Replay, ValidateChainMatchesReplay) {
  Chain c;
  const Block b1 = [&] {
    Block b;
    b.header.index = 1;
    b.header.timestamp = GENESIS_TIMESTAMP + 1;
    b.header.prevHash = c.tipHash();
    b.header.txnsHash = b.computeTxnsHash();
    b.header.nonce = 0;
    return b;
  }();
  ASSERT_EQ(c.connectBlock(b1, NOW), Reason::OK);

  std::vector<Block> v;
  for (std::uint64_t i = 0; i < c.numBlocks(); ++i) v.push_back(c.blockAt(i));

  EXPECT_EQ(c.validateChain(), Reason::OK);
  EXPECT_EQ(replayFromGenesis(v, NOW).reason, Reason::OK);
  EXPECT_EQ(c.validateChain(), replayFromGenesis(v, NOW).reason);
}
