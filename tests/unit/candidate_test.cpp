// Candidate block assembly (Architecture Mempool/PoW §4.2): buildCandidate is
// coinbase-first, ≤ MAX_TXNS_PER_BLOCK, ascending admission order, funds
// re-checked against a lazily-materialized shadow of the live tip. Label: pow.

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "txchain/chain/chain.hpp"
#include "txchain/chain/genesis.hpp"
#include "txchain/chain/params.hpp"
#include "txchain/chain/types.hpp"
#include "txchain/crypto/address.hpp"
#include "txchain/crypto/ed25519.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/crypto/sha256.hpp"
#include "txchain/mempool/mempool.hpp"
#include "txchain/pow/candidate.hpp"
#include "txchain/serialize/canonical.hpp"

namespace {

using txchain::chain::AccountState;
using txchain::chain::Address;
using txchain::chain::Chain;
using txchain::chain::COINBASE_REWARD;
using txchain::chain::genesisBlock;
using txchain::chain::MAX_TXNS_PER_BLOCK;
using txchain::mempool::AdmitResult;
using txchain::mempool::Mempool;
using txchain::pow::buildCandidate;
using txchain::pow::Candidate;
using txchain::serialize::Txn;

constexpr std::uint64_t NOW = 2000000000;
constexpr std::uint64_t GEN_TS = txchain::chain::GENESIS_TIMESTAMP;

txchain::crypto::Seed32 seed_for(const std::string& tag) {
  const std::string s = "cand-" + tag;
  return txchain::crypto::sha256(txchain::crypto::ByteView(
      reinterpret_cast<const txchain::crypto::Byte*>(s.data()), s.size()));
}
Address addr_of(const txchain::crypto::Seed32& seed) {
  return txchain::crypto::address(txchain::crypto::derive_pubkey(seed));
}
Txn sign_from(const txchain::crypto::Seed32& seed, const Address& to, std::uint64_t amount,
              std::uint64_t nonce) {
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

Address miner() {
  Address a{};
  a.fill(0x77);
  return a;
}

// Genesis identity seeds use the frozen genesis rule (funded 1000 by applyGenesis).
txchain::crypto::Seed32 genesis_seed(const std::string& name) {
  const std::string s = "txchain-genesis-" + name;
  return txchain::crypto::sha256(txchain::crypto::ByteView(
      reinterpret_cast<const txchain::crypto::Byte*>(s.data()), s.size()));
}

Mempool::AccountLookup lookup_of(std::map<Address, AccountState>& st) {
  return [&st](const Address& a) -> AccountState {
    const auto it = st.find(a);
    return it == st.end() ? AccountState{} : it->second;
  };
}

}  // namespace

// Coinbase is always tx0 with the frozen shape.
TEST(Candidate, CoinbaseIsTx0) {
  std::map<Address, AccountState> st;
  Mempool mp(lookup_of(st));  // empty mempool
  const auto g = genesisBlock();
  const Candidate c = buildCandidate(miner(), 0, g.header.hash(), GEN_TS, NOW, mp, lookup_of(st));

  ASSERT_EQ(c.txns.size(), 1u);  // coinbase only
  const Txn& cb = c.txns[0];
  EXPECT_EQ(cb.from, Address{});      // 0x00×20
  EXPECT_EQ(cb.to, miner());
  EXPECT_EQ(cb.amount, COINBASE_REWARD);
  EXPECT_EQ(cb.nonce, 1u);            // height = tip.index + 1
  EXPECT_EQ(c.hdr.index, 1u);
  EXPECT_EQ(c.hdr.txnsHash, txchain::serialize::txns_hash(c.txns));
}

// At most MAX_TXNS_PER_BLOCK txns (coinbase + 7 lowest-seq transfers).
TEST(Candidate, AtMostMaxTxnsAndAscendingOrder) {
  std::map<Address, AccountState> st;
  const Address recipient = addr_of(seed_for("recipient"));
  std::vector<Txn> admitted;
  Mempool mp(lookup_of(st));
  for (int i = 0; i < 9; ++i) {  // 9 distinct funded senders
    const auto seed = seed_for("s" + std::to_string(i));
    st[addr_of(seed)] = AccountState{1000, 0};
    const Txn t = sign_from(seed, recipient, 10, 0);
    ASSERT_EQ(mp.admit(t), AdmitResult::OK);
    admitted.push_back(t);
  }
  const auto g = genesisBlock();
  const Candidate c = buildCandidate(miner(), 0, g.header.hash(), GEN_TS, NOW, mp, lookup_of(st));

  EXPECT_EQ(c.txns.size(), MAX_TXNS_PER_BLOCK);  // coinbase + 7
  // selected[1..] are the lowest-feeless_seq (first-admitted) transfers, in order.
  for (std::size_t k = 1; k < c.txns.size(); ++k)
    EXPECT_EQ(txchain::serialize::txid(c.txns[k]), txchain::serialize::txid(admitted[k - 1]));
}

// A txn admitted OK is dropped if the tip advanced and the sender is now underfunded.
TEST(Candidate, FundsRecheckDropsUnderfunded) {
  std::map<Address, AccountState> st;
  const auto seed = seed_for("rich");
  const Address from = addr_of(seed);
  st[from] = AccountState{1000, 0};
  Mempool mp(lookup_of(st));
  ASSERT_EQ(mp.admit(sign_from(seed, addr_of(seed_for("dst")), 500, 0)), AdmitResult::OK);

  st[from].balance = 100;  // the tip advanced; sender can no longer afford 500
  const auto g = genesisBlock();
  const Candidate c = buildCandidate(miner(), 0, g.header.hash(), GEN_TS, NOW, mp, lookup_of(st));
  EXPECT_EQ(c.txns.size(), 1u);  // coinbase only — the transfer was re-checked out
}

// The shadow reads only the touched accounts (miner + each examined from/to).
TEST(Candidate, ShadowMaterializedLazily) {
  std::map<Address, AccountState> st;
  // Genesis-funded senders so admit succeeds against genesis state.
  txchain::chain::applyGenesis(st);
  Mempool mp(lookup_of(st));  // admit uses the plain lookup
  ASSERT_EQ(mp.admit(sign_from(genesis_seed("racheal"),
                               txchain::crypto::address(
                                   txchain::crypto::derive_pubkey(genesis_seed("oliver"))),
                               10, 0)),
            AdmitResult::OK);

  std::set<Address> queried;
  const auto spy = [&](const Address& a) -> AccountState {
    queried.insert(a);
    const auto it = st.find(a);
    return it == st.end() ? AccountState{} : it->second;
  };
  const auto g = genesisBlock();
  (void)buildCandidate(miner(), 0, g.header.hash(), GEN_TS, NOW, mp, spy);

  const Address racheal =
      txchain::crypto::address(txchain::crypto::derive_pubkey(genesis_seed("racheal")));
  const Address oliver =
      txchain::crypto::address(txchain::crypto::derive_pubkey(genesis_seed("oliver")));
  const Address matthew =
      txchain::crypto::address(txchain::crypto::derive_pubkey(genesis_seed("matthew")));
  EXPECT_TRUE(queried.count(miner()));
  EXPECT_TRUE(queried.count(racheal));
  EXPECT_TRUE(queried.count(oliver));
  EXPECT_FALSE(queried.count(matthew));  // untouched genesis account never read
}

// The candidate's txnsHash + txns pass V5/V7 in connectBlock (after mining, D=0).
TEST(Candidate, ValidForConnectBlock) {
  std::map<Address, AccountState> st;
  txchain::chain::applyGenesis(st);
  const Address oliver =
      txchain::crypto::address(txchain::crypto::derive_pubkey(genesis_seed("oliver")));
  Mempool mp(lookup_of(st));
  ASSERT_EQ(mp.admit(sign_from(genesis_seed("racheal"), oliver, 100, 0)), AdmitResult::OK);

  const auto g = genesisBlock();
  const Candidate c = buildCandidate(miner(), 0, g.header.hash(), GEN_TS, NOW, mp, lookup_of(st));

  txchain::serialize::Block b;
  b.header = c.hdr;  // nonce 0; D=0 chain accepts without mining
  b.txns = c.txns;
  Chain chain(0, COINBASE_REWARD);
  EXPECT_EQ(chain.connectBlock(b, NOW), txchain::chain::Reason::OK);
  EXPECT_EQ(chain.account(miner()).balance, COINBASE_REWARD);
  const Address racheal =
      txchain::crypto::address(txchain::crypto::derive_pubkey(genesis_seed("racheal")));
  EXPECT_EQ(chain.account(racheal).balance, 900u);
  EXPECT_EQ(chain.account(oliver).balance, 1100u);
}

// Timestamp strictly increases even when now() <= tip.timestamp.
TEST(Candidate, TimestampStrictlyIncreases) {
  std::map<Address, AccountState> st;
  Mempool mp(lookup_of(st));
  const auto g = genesisBlock();
  // now_s below the tip timestamp — max(now, tip+1) must still exceed the tip.
  const Candidate c = buildCandidate(miner(), 0, g.header.hash(), GEN_TS, /*now_s=*/GEN_TS - 5, mp,
                                     lookup_of(st));
  EXPECT_GT(c.hdr.timestamp, GEN_TS);
  EXPECT_EQ(c.hdr.timestamp, GEN_TS + 1);
}
