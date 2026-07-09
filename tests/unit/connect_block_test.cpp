// Chain::connectBlock structural + link validation at D=0 (Architecture
// Chain/State §4). Covers V1–V6, the V7 commit machinery, atomicity on failure,
// and the D=0 PoW no-op. Label: chain.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "txchain/chain/chain.hpp"
#include "txchain/chain/genesis.hpp"
#include "txchain/chain/params.hpp"
#include "txchain/chain/reason.hpp"
#include "txchain/chain/types.hpp"
#include "txchain/crypto/address.hpp"
#include "txchain/crypto/ed25519.hpp"
#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/crypto/sha256.hpp"
#include "txchain/serialize/canonical.hpp"

namespace {

using txchain::chain::Address;
using txchain::chain::Block;
using txchain::chain::Chain;
using txchain::chain::GENESIS_ALLOC;
using txchain::chain::GENESIS_TIMESTAMP;
using txchain::chain::MAX_CLOCK_SKEW_S;
using txchain::chain::Reason;
using txchain::chain::Txn;
using txchain::chain::Work;

constexpr std::uint64_t NOW = 2000000000;  // year 2033, well past genesis

// A distinct zero-amount txn (unique `from`). Only used where the V6 size cap
// rejects the block BEFORE the V7 gate runs (so the txn need not be signed).
Txn zero_txn(std::uint8_t tag) {
  Txn t;
  t.ver = 1;
  t.from[0] = tag;
  t.to[0] = 0xFF;
  t.amount = 0;
  t.nonce = 0;
  return t;
}

// A validly-signed txn from a genesis identity (seed = SHA-256("txchain-genesis-"||name)).
txchain::crypto::Seed32 genesis_seed(const std::string& name) {
  const std::string s = "txchain-genesis-" + name;
  return txchain::crypto::sha256(txchain::crypto::ByteView(
      reinterpret_cast<const txchain::crypto::Byte*>(s.data()), s.size()));
}

Txn signed_txn(const txchain::crypto::Seed32& seed, const Address& to, std::uint64_t amount,
                std::uint64_t nonce) {
  Txn t;
  t.ver = 1;
  t.pubkey = txchain::crypto::derive_pubkey(seed);
  t.from = txchain::crypto::address(t.pubkey);
  t.to = to;
  t.amount = amount;
  t.nonce = nonce;
  const auto p = txchain::serialize::signed_payload(t);
  t.sig = txchain::crypto::sign(seed, txchain::crypto::ByteView(p.data(), p.size()));
  return t;
}

// Build a well-formed child of the chain tip (valid index/prevHash/txnsHash).
Block child(const Chain& c, std::vector<Txn> txns, std::uint64_t ts) {
  Block b;
  b.header.index = c.height() + 1;
  b.header.timestamp = ts;
  b.header.prevHash = c.tipHash();
  b.txns = std::move(txns);
  b.header.txnsHash = b.computeTxnsHash();
  b.header.nonce = 0;
  return b;
}

}  // namespace

TEST(ConnectBlock, WellFormedConnects) {
  Chain c;
  const Block b = child(c, {}, GENESIS_TIMESTAMP + 1);
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::OK);
  EXPECT_EQ(c.height(), 1u);
  EXPECT_EQ(c.tipHash(), b.header.hash());
  EXPECT_TRUE(c.cumWork() == static_cast<Work>(1));  // 2^0
}

TEST(ConnectBlock, V1BadIndex) {
  Chain c;
  Block b = child(c, {}, GENESIS_TIMESTAMP + 1);
  b.header.index = 5;  // != height()+1
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::BAD_LINK);
}

TEST(ConnectBlock, V2BadPrevHash) {
  Chain c;
  Block b = child(c, {}, GENESIS_TIMESTAMP + 1);
  b.header.prevHash[0] ^= 0x01;  // one byte off the tip hash
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::BAD_LINK);
}

TEST(ConnectBlock, V3TimestampNotMonotonic) {
  Chain c;
  const Block b = child(c, {}, GENESIS_TIMESTAMP);  // == tip timestamp
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::BAD_LINK);
}

TEST(ConnectBlock, V3TimestampBeyondSkew) {
  Chain c;
  const Block b = child(c, {}, NOW + MAX_CLOCK_SKEW_S + 1);  // too far in the future
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::BAD_LINK);
}

TEST(ConnectBlock, V3TimestampMonotonicWithinSkewOk) {
  Chain c;
  const Block b = child(c, {}, GENESIS_TIMESTAMP + 1);  // tip+1, within skew
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::OK);
}

TEST(ConnectBlock, V5BadTxnsHash) {
  Chain c;
  Block b = child(c, {}, GENESIS_TIMESTAMP + 1);
  b.header.txnsHash[0] ^= 0x01;  // disagrees with computeTxnsHash()
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::BAD_TXNS_HASH);
}

TEST(ConnectBlock, V6TooManyTxns) {
  Chain c;
  std::vector<Txn> nine;
  for (int i = 0; i < 9; ++i) nine.push_back(zero_txn(static_cast<std::uint8_t>(i + 1)));
  const Block b = child(c, nine, GENESIS_TIMESTAMP + 1);  // valid txnsHash for 9 txns
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::BAD_TXNS_HASH);
}

TEST(ConnectBlock, V6EightTxnsOk) {
  Chain c;
  // Eight validly-signed txns from racheal (balance 1000), nonces 0..7 — the
  // block-size cap boundary now also has to pass the V7 verify gate.
  const auto seed = genesis_seed("racheal");
  const Address oliver =
      txchain::crypto::address(txchain::crypto::derive_pubkey(genesis_seed("oliver")));
  std::vector<Txn> eight;
  for (std::uint64_t i = 0; i < 8; ++i) eight.push_back(signed_txn(seed, oliver, 1, i));
  const Block b = child(c, eight, GENESIS_TIMESTAMP + 1);
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::OK);
  EXPECT_EQ(c.height(), 1u);
}

TEST(ConnectBlock, AtomicityOnFailure) {
  Chain c;
  const auto h0 = c.height();
  const auto tip0 = c.tipHash();
  const auto cw0 = c.cumWork();
  const auto bal0 = c.account(GENESIS_ALLOC[0].addr).balance;

  Block bad = child(c, {}, GENESIS_TIMESTAMP + 1);
  bad.header.prevHash[0] ^= 0x01;  // will fail V2
  EXPECT_EQ(c.connectBlock(bad, NOW), Reason::BAD_LINK);

  EXPECT_EQ(c.height(), h0);
  EXPECT_EQ(c.tipHash(), tip0);
  EXPECT_TRUE(c.cumWork() == cw0);
  EXPECT_EQ(c.account(GENESIS_ALLOC[0].addr).balance, bal0);
}

TEST(ConnectBlock, D0PowNoop) {
  // D=0 accepts any blockHash, so an arbitrary header.nonce still connects.
  Chain c;
  Block b = child(c, {}, GENESIS_TIMESTAMP + 1);
  b.header.nonce = 123456;  // changes header.hash(), not txnsHash
  EXPECT_EQ(c.connectBlock(b, NOW), Reason::OK);
}

TEST(ConnectBlock, MeetsDifficultyZeroAlwaysTrue) {
  txchain::crypto::Digest32 h;
  h.fill(0xFF);
  EXPECT_TRUE(txchain::crypto::meets_difficulty(h, 0));
}

TEST(ConnectBlock, SequentialBlocksAdvanceCumWork) {
  Chain c;
  const Block b1 = child(c, {}, GENESIS_TIMESTAMP + 1);
  ASSERT_EQ(c.connectBlock(b1, NOW), Reason::OK);
  const Block b2 = child(c, {}, GENESIS_TIMESTAMP + 2);
  ASSERT_EQ(c.connectBlock(b2, NOW), Reason::OK);
  EXPECT_EQ(c.height(), 2u);
  EXPECT_TRUE(c.cumWork() == static_cast<Work>(2));
}
