// The miner (Architecture Mempool/PoW §4.3, §4.6): mine() grinds the nonce until
// the hash meets difficulty, Node::commitBlock atomically applies + persists a
// mined block. Tested at a low D for speed (the D=16 live mine is an integration
// smoke). Label: pow.

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
#include "txchain/crypto/address.hpp"
#include "txchain/crypto/ed25519.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/crypto/sha256.hpp"
#include "txchain/mempool/mempool.hpp"
#include "txchain/node/node.hpp"
#include "txchain/pow/candidate.hpp"
#include "txchain/pow/miner.hpp"
#include "txchain/serialize/canonical.hpp"

namespace {

namespace fs = std::filesystem;

using txchain::chain::AccountState;
using txchain::chain::Address;
using txchain::chain::ChainStore;
using txchain::chain::COINBASE_REWARD;
using txchain::chain::genesisBlock;
using txchain::chain::Hash256;
using txchain::chain::monitor_verify_line;
using txchain::chain::Reason;
using txchain::chain::replayFromGenesis;
using txchain::chain::Work;
using txchain::mempool::AdmitResult;
using txchain::mempool::Mempool;
using txchain::pow::buildCandidate;
using txchain::pow::Candidate;
using txchain::serialize::Txn;

constexpr std::uint64_t NOW = 2000000000;
constexpr std::uint64_t GEN_TS = txchain::chain::GENESIS_TIMESTAMP;
constexpr unsigned D_TEST = 8;  // fast to mine (~256 attempts) yet a real target

Address miner() {
  Address a{};
  a.fill(0x77);
  return a;
}
txchain::crypto::Seed32 genesis_seed(const std::string& name) {
  const std::string s = "txchain-genesis-" + name;
  return txchain::crypto::sha256(txchain::crypto::ByteView(
      reinterpret_cast<const txchain::crypto::Byte*>(s.data()), s.size()));
}
Address addr_g(const std::string& name) {
  return txchain::crypto::address(txchain::crypto::derive_pubkey(genesis_seed(name)));
}
Txn signed_g(const std::string& from, const Address& to, std::uint64_t amount, std::uint64_t nonce) {
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
std::string fresh_datadir() {
  static int n = 0;
  const auto p = fs::temp_directory_path() / ("txchain_miner_test_" + std::to_string(n++));
  std::error_code ec;
  fs::remove_all(p, ec);
  return p.string();
}
Mempool::AccountLookup lookup_of(std::map<Address, AccountState>& st) {
  return [&st](const Address& a) -> AccountState {
    const auto it = st.find(a);
    return it == st.end() ? AccountState{} : it->second;
  };
}

}  // namespace

// mine() finds a nonce whose hash meets difficulty; only the nonce differs.
TEST(Miner, MineReturnsHashMeetingDifficulty) {
  std::map<Address, AccountState> st;
  Mempool mp(lookup_of(st));
  const auto g = genesisBlock();
  const Candidate cand = buildCandidate(miner(), 0, g.header.hash(), GEN_TS, NOW, mp, lookup_of(st));

  std::atomic<bool> stop{false};
  const auto mined = txchain::pow::mine(cand, D_TEST, stop);
  ASSERT_TRUE(mined.has_value());
  EXPECT_TRUE(txchain::crypto::meets_difficulty(mined->header.hash(), D_TEST));
  EXPECT_EQ(mined->header.index, cand.hdr.index);
  EXPECT_EQ(mined->header.prevHash, cand.hdr.prevHash);
  EXPECT_EQ(mined->header.txnsHash, cand.hdr.txnsHash);
  EXPECT_EQ(mined->txns, cand.txns);  // txns unchanged, only the nonce searched
}

// A set stop flag pre-empts the search immediately (nullopt, no solution found).
TEST(Miner, PreemptOnStop) {
  std::map<Address, AccountState> st;
  Mempool mp(lookup_of(st));
  const auto g = genesisBlock();
  const Candidate cand = buildCandidate(miner(), 0, g.header.hash(), GEN_TS, NOW, mp, lookup_of(st));
  std::atomic<bool> stop{true};
  EXPECT_FALSE(txchain::pow::mine(cand, 240, stop).has_value());  // would never finish; stop wins
}

// commitBlock applies a mined coinbase+transfer block, advances cumWork by 2^D,
// and persists it; monitor verify (at the same D/reward) passes.
TEST(Miner, CommitBlockAtomicAndPersists) {
  const auto dir = fresh_datadir();
  txchain::node::NodeConfig cfg;
  cfg.datadir = dir;
  cfg.difficulty = D_TEST;
  txchain::node::BootResult br;
  txchain::node::Node nd = txchain::node::Node::boot(cfg, NOW, br);
  ASSERT_TRUE(br.ok);

  Mempool mp([&nd](const Address& a) { return nd.chain().account(a); });
  const Address oliver = addr_g("oliver");
  ASSERT_EQ(mp.admit(signed_g("racheal", oliver, 100, 0)), AdmitResult::OK);

  const auto& tip = nd.chain().blockAt(nd.chain().height());
  const Candidate cand = buildCandidate(miner(), nd.height(), nd.tipHash(), tip.header.timestamp,
                                        NOW, mp, [&nd](const Address& a) { return nd.chain().account(a); });
  std::atomic<bool> stop{false};
  const auto mined = txchain::pow::mine(cand, D_TEST, stop);
  ASSERT_TRUE(mined.has_value());

  const Work cw0 = nd.chain().cumWork();
  ASSERT_EQ(nd.commitBlock(*mined, NOW), Reason::OK);
  EXPECT_EQ(nd.height(), 1u);
  EXPECT_EQ(nd.chain().account(miner()).balance, COINBASE_REWARD);          // +reward
  EXPECT_EQ(nd.chain().account(addr_g("racheal")).balance, 900u);           // -amount
  EXPECT_EQ(nd.chain().account(addr_g("racheal")).nonce, 1u);               // nonce++
  EXPECT_EQ(nd.chain().account(oliver).balance, 1100u);                     // +amount
  EXPECT_TRUE(nd.chain().cumWork() == cw0 + txchain::crypto::block_work(D_TEST));

  const auto lr = ChainStore::forDatadir(dir).load();
  ASSERT_TRUE(lr.status.ok);
  EXPECT_EQ(lr.blocks.size(), 2u);  // genesis + the mined block, appended + fsynced
  const auto vr = replayFromGenesis(lr.blocks, NOW + 10, D_TEST, COINBASE_REWARD);
  EXPECT_TRUE(vr.ok) << monitor_verify_line(vr, lr.blocks.size());
}

// An idle node still advances by mining coinbase-only blocks; consecutive blocks
// never collide (distinct blockHash + distinct coinbase txid) and supply tracks height.
TEST(Miner, IdleAdvancesDistinctBlocks) {
  const auto dir = fresh_datadir();
  txchain::node::NodeConfig cfg;
  cfg.datadir = dir;
  cfg.difficulty = D_TEST;
  txchain::node::BootResult br;
  txchain::node::Node nd = txchain::node::Node::boot(cfg, NOW, br);
  ASSERT_TRUE(br.ok);
  Mempool mp([&nd](const Address& a) { return nd.chain().account(a); });  // idle
  std::atomic<bool> stop{false};

  std::vector<Hash256> hashes;
  std::vector<txchain::crypto::Digest32> cb_txids;
  for (int h = 1; h <= 2; ++h) {
    const auto& tip = nd.chain().blockAt(nd.chain().height());
    const Candidate cand = buildCandidate(miner(), nd.height(), nd.tipHash(), tip.header.timestamp,
                                          NOW + h, mp,
                                          [&nd](const Address& a) { return nd.chain().account(a); });
    const auto mined = txchain::pow::mine(cand, D_TEST, stop);
    ASSERT_TRUE(mined.has_value());
    EXPECT_EQ(mined->txns.size(), 1u);  // coinbase-only while idle
    ASSERT_EQ(nd.commitBlock(*mined, NOW + h), Reason::OK);
    hashes.push_back(mined->header.hash());
    cb_txids.push_back(txchain::serialize::txid(mined->txns[0]));
  }
  EXPECT_EQ(nd.height(), 2u);
  EXPECT_EQ(nd.chain().account(miner()).balance, 2u * COINBASE_REWARD);  // supply += height×50
  EXPECT_NE(hashes[0], hashes[1]);        // distinct blockHash
  EXPECT_NE(cb_txids[0], cb_txids[1]);    // distinct coinbase txid (nonce == index differs)
}

// A coinbase-only block's txnsHash is SHA-256(canonical153(coinbase)), NOT the
// empty-string digest — tx0 is present.
TEST(Miner, CoinbaseOnlyTxnsHashNotEmpty) {
  std::map<Address, AccountState> st;
  Mempool mp(lookup_of(st));
  const auto g = genesisBlock();
  const Candidate cand = buildCandidate(miner(), 0, g.header.hash(), GEN_TS, NOW, mp, lookup_of(st));
  ASSERT_EQ(cand.txns.size(), 1u);
  const auto empty_digest = txchain::crypto::sha256(txchain::crypto::ByteView(nullptr, 0));
  EXPECT_NE(cand.hdr.txnsHash, empty_digest);
  EXPECT_EQ(cand.hdr.txnsHash, txchain::serialize::txns_hash(cand.txns));
}
