// M3 Proof-of-Work + coinbase validation (Mempool/PoW §4.4–4.5, Chain/State §4/§6,
// Data Model §3.3). connectBlock (V4) and replayFromGenesis enforce the SAME
// crypto::meets_difficulty(hash, D) and the SAME coinbase rules via applyBlockTxns,
// so mining and monitor verify can never disagree. Difficulty/reward are
// configurable (0 = the M1–M2 no-op default); these tests pass D/REWARD explicitly.
// Label: pow.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "txchain/chain/chain.hpp"
#include "txchain/chain/genesis.hpp"
#include "txchain/chain/params.hpp"
#include "txchain/chain/types.hpp"
#include "txchain/chain/validate.hpp"
#include "txchain/crypto/address.hpp"
#include "txchain/crypto/ed25519.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/crypto/sha256.hpp"
#include "txchain/serialize/canonical.hpp"

namespace {

using txchain::chain::Address;
using txchain::chain::Chain;
using txchain::chain::COINBASE_REWARD;
using txchain::chain::genesisBlock;
using txchain::chain::monitor_verify_line;
using txchain::chain::Reason;
using txchain::chain::replayFromGenesis;
using txchain::chain::Work;
using txchain::serialize::Block;
using txchain::serialize::Txn;

constexpr std::uint64_t NOW = 2000000000;

Address miner_addr(std::uint8_t tag = 0x11) {
  Address a{};
  a.fill(tag);
  return a;
}

txchain::crypto::Seed32 genesis_seed(const std::string& name) {
  const std::string s = "txchain-genesis-" + name;
  return txchain::crypto::sha256(txchain::crypto::ByteView(
      reinterpret_cast<const txchain::crypto::Byte*>(s.data()), s.size()));
}
Address addr_of(const std::string& name) {
  return txchain::crypto::address(txchain::crypto::derive_pubkey(genesis_seed(name)));
}
Txn signed_txn(const std::string& from, const Address& to, std::uint64_t amount,
               std::uint64_t nonce) {
  const auto seed = genesis_seed(from);
  Txn t;
  t.ver = txchain::serialize::kTxnVersion;
  t.pubkey = txchain::crypto::derive_pubkey(seed);
  t.from = txchain::crypto::address(t.pubkey);
  t.to = to;
  t.amount = amount;
  t.nonce = nonce;
  const auto p = txchain::serialize::signed_payload(t);
  t.sig = txchain::crypto::sign(seed, txchain::crypto::ByteView(p.data(), p.size()));
  return t;
}

// A well-formed coinbase: from = 0x00×20, pubkey/sig zero, nonce == block height.
Txn coinbase(const Address& to, std::uint64_t height, std::uint64_t amount = COINBASE_REWARD) {
  Txn t;
  t.ver = txchain::serialize::kTxnVersion;
  t.to = to;
  t.amount = amount;
  t.nonce = height;  // BIP-34-style height → unique txid per block
  return t;
}

// Build a child block; if mineD > 0, search nonce until the hash meets difficulty.
Block make_block(const Chain& c, std::vector<Txn> txns, std::uint64_t ts, unsigned mineD = 0) {
  Block b;
  b.header.index = c.height() + 1;
  b.header.timestamp = ts;
  b.header.prevHash = c.tipHash();
  b.txns = std::move(txns);
  b.header.txnsHash = b.computeTxnsHash();
  b.header.nonce = 0;
  if (mineD > 0)
    while (!txchain::crypto::meets_difficulty(b.header.hash(), mineD)) ++b.header.nonce;
  return b;
}

}  // namespace

// Genesis (index 0) is PoW-exempt even under D=16.
TEST(Pow, GenesisExempt) {
  Chain c(16, 0);  // constructs fine — genesis is never PoW-checked
  EXPECT_EQ(c.height(), 0u);
  const std::vector<Block> chain = {genesisBlock()};
  EXPECT_TRUE(replayFromGenesis(chain, NOW, 16, 0).ok);
}

// connectBlock rejects an under-target block, atomically.
TEST(Pow, ConnectRejectsUnderTarget) {
  Chain c(16, 0);
  Block b = make_block(c, {}, NOW);            // nonce 0, unmined
  while (txchain::crypto::meets_difficulty(b.header.hash(), 16)) ++b.header.nonce;  // ensure under-target
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::BAD_POW);
  EXPECT_EQ(c.height(), 0u);                   // no partial commit
  EXPECT_TRUE(c.cumWork() == static_cast<Work>(0));
}

// A mined block (hash meets D) connects; cumWork advances by 2^D.
TEST(Pow, MinedBlockConnects) {
  Chain c(16, 0);
  const Block b = make_block(c, {}, NOW, /*mineD=*/16);
  ASSERT_TRUE(txchain::crypto::meets_difficulty(b.header.hash(), 16));
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::OK);
  EXPECT_EQ(c.height(), 1u);
  EXPECT_TRUE(c.cumWork() == txchain::crypto::block_work(16));  // 2^16
}

// replayFromGenesis reports BAD_POW naming the under-target block; connect agrees.
TEST(Pow, ReplayReportsBadPowAndAgreesWithConnect) {
  Chain c(16, 0);
  Block b = make_block(c, {}, NOW);
  while (txchain::crypto::meets_difficulty(b.header.hash(), 16)) ++b.header.nonce;

  const Reason connect_reason = c.connectBlock(b, NOW);
  const std::vector<Block> chain = {genesisBlock(), b};
  const auto vr = replayFromGenesis(chain, NOW, 16, 0);
  EXPECT_EQ(connect_reason, vr.reason);  // one predicate, both sites
  EXPECT_FALSE(vr.ok);
  EXPECT_EQ(vr.failIndex, 1u);
  EXPECT_EQ(vr.reason, Reason::BAD_POW);
  EXPECT_EQ(vr.detail, "hash above target");
  EXPECT_EQ(monitor_verify_line(vr, chain.size()), "FAIL block 1: BAD_POW (hash above target)");
}

// Coinbase happy path (D=0 to skip mining): +REWARD credit, no nonce bump.
TEST(Coinbase, HappyPath) {
  Chain c(0, COINBASE_REWARD);
  const Address m = miner_addr();
  const Block b = make_block(c, {coinbase(m, 1)}, NOW);
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::OK);
  EXPECT_EQ(c.account(m).balance, COINBASE_REWARD);
  EXPECT_EQ(c.account(m).nonce, 0u);  // coinbase does not bump the recipient nonce
}

TEST(Coinbase, WrongAmountBadSupply) {
  Chain c(0, COINBASE_REWARD);
  const Block b = make_block(c, {coinbase(miner_addr(), 1, 51)}, NOW);  // != 50
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::BAD_SUPPLY);
  const std::vector<Block> chain = {genesisBlock(), b};
  const auto vr = replayFromGenesis(chain, NOW, 0, COINBASE_REWARD);
  EXPECT_EQ(vr.reason, Reason::BAD_SUPPLY);
  EXPECT_EQ(vr.detail, "bad coinbase reward");
}

TEST(Coinbase, MisplacedNotTx0) {
  Chain c(0, COINBASE_REWARD);
  // tx0 is a normal transfer, coinbase wrongly at tx1.
  const Block b = make_block(c, {signed_txn("racheal", addr_of("oliver"), 10, 0), coinbase(miner_addr(), 1)}, NOW);
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::BAD_TXNS_HASH);
  const auto vr = replayFromGenesis({genesisBlock(), b}, NOW, 0, COINBASE_REWARD);
  EXPECT_EQ(vr.detail, "coinbase not at tx0");
}

TEST(Coinbase, DuplicateBadTxnsHash) {
  Chain c(0, COINBASE_REWARD);
  const Block b = make_block(c, {coinbase(miner_addr(0x11), 1), coinbase(miner_addr(0x22), 1)}, NOW);
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::BAD_TXNS_HASH);
  const auto vr = replayFromGenesis({genesisBlock(), b}, NOW, 0, COINBASE_REWARD);
  EXPECT_EQ(vr.detail, "duplicate coinbase");
}

TEST(Coinbase, MalformedNonceBadTxnsHash) {
  Chain c(0, COINBASE_REWARD);
  const Block b = make_block(c, {coinbase(miner_addr(), 999)}, NOW);  // nonce != index (1)
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::BAD_TXNS_HASH);
  const auto vr = replayFromGenesis({genesisBlock(), b}, NOW, 0, COINBASE_REWARD);
  EXPECT_EQ(vr.detail, "malformed coinbase");
}

TEST(Coinbase, MissingBadSupply) {
  Chain c(0, COINBASE_REWARD);  // reward > 0 ⇒ a coinbase is mandatory
  const Block b = make_block(c, {signed_txn("racheal", addr_of("oliver"), 10, 0)}, NOW);
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::BAD_SUPPLY);
  const auto vr = replayFromGenesis({genesisBlock(), b}, NOW, 0, COINBASE_REWARD);
  EXPECT_EQ(vr.detail, "missing coinbase");
}

// Supply invariant with the height×REWARD term; tampering one coinbase breaks it.
TEST(Coinbase, SupplyInvariantWithRewardTerm) {
  Chain c(0, COINBASE_REWARD);
  const Address m = miner_addr();
  std::vector<Block> chain = {genesisBlock()};
  for (std::uint64_t h = 1; h <= 3; ++h) {
    const Block b = make_block(c, {coinbase(m, h)}, NOW + h);
    ASSERT_EQ(c.connectBlock(b, NOW + h), Reason::OK);
    chain.push_back(b);
  }
  EXPECT_EQ(c.account(m).balance, 3u * COINBASE_REWARD);  // 150
  const auto vr = replayFromGenesis(chain, NOW + 10, 0, COINBASE_REWARD);
  EXPECT_TRUE(vr.ok) << monitor_verify_line(vr, chain.size());  // Σ == 5000 + 3×50

  // Tamper one block's coinbase amount ⇒ BAD_SUPPLY (bad coinbase reward fires first).
  chain[2].txns[0].amount = 49;
  chain[2].header.txnsHash = chain[2].computeTxnsHash();  // keep V5 consistent
  EXPECT_FALSE(replayFromGenesis(chain, NOW + 10, 0, COINBASE_REWARD).ok);
}

// Coinbase txid uniqueness: same miner, consecutive heights ⇒ distinct 153-byte txns.
TEST(Coinbase, TxidUniquePerHeight) {
  const Address m = miner_addr();
  const Txn cb_h = coinbase(m, 7);
  const Txn cb_h1 = coinbase(m, 8);
  EXPECT_NE(txchain::crypto::to_hex(txchain::serialize::txid(cb_h)),
            txchain::crypto::to_hex(txchain::serialize::txid(cb_h1)));
}
