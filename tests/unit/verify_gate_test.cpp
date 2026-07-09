// Pillar-2 signature/identity verify gate (Data Model §2.3, Chain/State §4/§5).
// The gate is ONE predicate (applyTxn) wired into both connectBlock and
// replayFromGenesis; these tests assert the frozen order (BAD_ADDR → BAD_SIG →
// STALE_NONCE → INSUFFICIENT_FUNDS), the coinbase-exempt branch stays dormant at
// M2, connect and replay agree, and connectBlock is atomic. Signed txns are
// built with a funded genesis identity (seed = SHA-256("txchain-genesis-"||name)).
// Golden sig is a python cryptography (Ed25519 RFC 8032) oracle. Label: chain.

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "txchain/chain/chain.hpp"
#include "txchain/chain/gate.hpp"
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

using txchain::chain::AccountState;
using txchain::chain::Address;
using txchain::chain::applyGenesis;
using txchain::chain::applyTxn;
using txchain::chain::Chain;
using txchain::chain::genesisBlock;
using txchain::chain::Hash256;
using txchain::chain::monitor_verify_line;
using txchain::chain::Reason;
using txchain::chain::replayFromGenesis;
using txchain::serialize::Block;
using txchain::serialize::Txn;
using txchain::crypto::to_hex;

// A now_s well past GENESIS_TIMESTAMP (1704067200) and inside the skew window.
constexpr std::uint64_t NOW = 2000000000;

// Golden vector (python cryptography oracle): racheal→oliver, amount=100, nonce=0.
constexpr const char* kRachealAddr = "625c36c34a656bf25a538f8c23a13186985af669";
constexpr const char* kGoldenSig =
    "67a9a5fe04e8de23754e0fb313efde6931b3af845395a7730961739dda560c6d"
    "d4efa5d9445b62f6a429bc547052fb20951bae27ca002ff0858b84c073302306";
// Address(0x02×32) — Data Model Vector B (Cryptography §6).
constexpr const char* kGoldenVectorBAddr = "75877bb41d393b5fb8455ce60ecd8dda001d0631";

txchain::crypto::Seed32 genesis_seed(const std::string& name) {
  const std::string s = "txchain-genesis-" + name;
  const txchain::crypto::Digest32 d = txchain::crypto::sha256(
      txchain::crypto::ByteView(reinterpret_cast<const txchain::crypto::Byte*>(s.data()), s.size()));
  return d;  // Digest32 and Seed32 are the same 32-byte type
}

Address addr_of(const std::string& name) {
  return txchain::crypto::address(txchain::crypto::derive_pubkey(genesis_seed(name)));
}

Txn make_signed(const txchain::crypto::Seed32& seed, const Address& to, std::uint64_t amount,
                std::uint64_t nonce) {
  Txn t;
  t.ver = txchain::serialize::kTxnVersion;
  t.pubkey = txchain::crypto::derive_pubkey(seed);
  t.from = txchain::crypto::address(t.pubkey);
  t.to = to;
  t.amount = amount;
  t.nonce = nonce;
  const auto payload = txchain::serialize::signed_payload(t);
  t.sig = txchain::crypto::sign(seed, txchain::crypto::ByteView(payload.data(), payload.size()));
  return t;
}

Block build_block(std::uint64_t index, const Hash256& prev, std::uint64_t ts,
                  std::vector<Txn> txns) {
  Block b;
  b.header.index = index;
  b.header.timestamp = ts;
  b.header.prevHash = prev;
  b.txns = std::move(txns);
  b.header.txnsHash = b.computeTxnsHash();
  b.header.nonce = 0;
  return b;
}

std::map<Address, AccountState> funded_state() {
  std::map<Address, AccountState> st;
  applyGenesis(st);  // racheal..daniel each 1000
  return st;
}

}  // namespace

// --- Golden signed-txn vector (cross-checks the 89-byte payload layout) ---
TEST(VerifyGate, GoldenSignedTxnVector) {
  const Txn t = make_signed(genesis_seed("racheal"), addr_of("oliver"), 100, 0);
  EXPECT_EQ(to_hex(t.from), kRachealAddr);
  EXPECT_EQ(to_hex(t.sig), kGoldenSig);  // deterministic Ed25519 == python oracle

  auto st = funded_state();
  EXPECT_EQ(applyTxn(st, t, 1, 0), Reason::OK);
  EXPECT_EQ(st[t.from].balance, 900u);
  EXPECT_EQ(st[t.from].nonce, 1u);
  EXPECT_EQ(st[addr_of("oliver")].balance, 1100u);
}

// --- Frozen ordering: address before sig before nonce before funds ---
TEST(VerifyGate, OrderingAddrBeforeSig) {
  auto st = funded_state();
  Txn t = make_signed(genesis_seed("racheal"), addr_of("oliver"), 100, 0);
  t.from = addr_of("oliver");  // now from != address(pubkey) AND sig invalid
  EXPECT_EQ(applyTxn(st, t, 1, 0), Reason::BAD_ADDR);  // address decided first
}

TEST(VerifyGate, OrderingSigBeforeNonce) {
  auto st = funded_state();
  Txn t = make_signed(genesis_seed("racheal"), addr_of("oliver"), 100, 5);  // wrong nonce
  t.sig[0] ^= 0x01;  // ...and a broken sig
  EXPECT_EQ(applyTxn(st, t, 1, 0), Reason::BAD_SIG);  // sig decided before nonce
}

TEST(VerifyGate, ValidSigWrongNonce) {
  auto st = funded_state();
  const Txn t = make_signed(genesis_seed("racheal"), addr_of("oliver"), 100, 5);  // gap nonce
  EXPECT_EQ(applyTxn(st, t, 1, 0), Reason::STALE_NONCE);
}

TEST(VerifyGate, ReplayedNonceIsStale) {
  auto st = funded_state();
  const Txn t = make_signed(genesis_seed("racheal"), addr_of("oliver"), 100, 0);
  ASSERT_EQ(applyTxn(st, t, 1, 0), Reason::OK);          // nonce 0 consumed → next-expected 1
  EXPECT_EQ(applyTxn(st, t, 2, 0), Reason::STALE_NONCE);  // replay of nonce 0 = below expected
}

TEST(VerifyGate, ValidSigValidNonceOverBalance) {
  auto st = funded_state();
  const Txn t = make_signed(genesis_seed("racheal"), addr_of("oliver"), 5000, 0);  // > 1000
  EXPECT_EQ(applyTxn(st, t, 1, 0), Reason::INSUFFICIENT_FUNDS);
}

// --- BAD_ADDR KAT (Data Model Vector B): pubkey 0x02×32 ---
TEST(VerifyGate, BadAddrKAT) {
  std::map<Address, AccountState> st;
  Txn t;
  t.pubkey.fill(0x02);
  t.from.fill(0xAB);  // anything but the golden Vector-B address
  EXPECT_EQ(applyTxn(st, t, 1, 0), Reason::BAD_ADDR);

  // With from == the golden address, address passes and the (zero) sig fails.
  Txn t2;
  t2.pubkey.fill(0x02);
  ASSERT_TRUE(txchain::crypto::from_hex<20>(kGoldenVectorBAddr, t2.from));
  EXPECT_EQ(applyTxn(st, t2, 1, 0), Reason::BAD_SIG);
}

// --- BAD_SIG: flip a payload byte OR a sig byte ---
TEST(VerifyGate, BadSigOnPayloadTamper) {
  auto st = funded_state();
  Txn t = make_signed(genesis_seed("racheal"), addr_of("oliver"), 100, 0);
  t.amount = 101;  // payload changed; sig is over amount=100; from/pubkey intact
  EXPECT_EQ(applyTxn(st, t, 1, 0), Reason::BAD_SIG);
}

TEST(VerifyGate, BadSigOnSigTamper) {
  auto st = funded_state();
  Txn t = make_signed(genesis_seed("racheal"), addr_of("oliver"), 100, 0);
  t.sig[31] ^= 0xFF;
  EXPECT_EQ(applyTxn(st, t, 1, 0), Reason::BAD_SIG);
}

// --- connectBlock happy path: balances + nonce update, block committed ---
TEST(VerifyGate, ConnectBlockHappyPath) {
  Chain c;
  const Txn t = make_signed(genesis_seed("racheal"), addr_of("oliver"), 100, 0);
  const Block b = build_block(1, c.tipHash(), NOW, {t});
  ASSERT_EQ(c.connectBlock(b, NOW), Reason::OK);
  EXPECT_EQ(c.height(), 1u);
  EXPECT_EQ(c.account(t.from).balance, 900u);
  EXPECT_EQ(c.account(t.from).nonce, 1u);
  EXPECT_EQ(c.account(addr_of("oliver")).balance, 1100u);
}

// --- connectBlock atomicity: a bad 2nd txn commits nothing ---
TEST(VerifyGate, ConnectBlockAtomicOnSecondTxnFailure) {
  Chain c;
  const auto rseed = genesis_seed("racheal");
  const Txn good = make_signed(rseed, addr_of("oliver"), 100, 0);
  Txn bad = make_signed(rseed, addr_of("oliver"), 50, 1);
  bad.sig[0] ^= 0x01;  // second txn is BAD_SIG
  const Block b = build_block(1, c.tipHash(), NOW, {good, bad});

  EXPECT_EQ(c.connectBlock(b, NOW), Reason::BAD_SIG);
  EXPECT_EQ(c.height(), 0u);                        // block not pushed
  EXPECT_EQ(c.account(good.from).balance, 1000u);   // txn 0 NOT partially committed
  EXPECT_EQ(c.account(good.from).nonce, 0u);
  EXPECT_TRUE(c.cumWork() == 0);
}

// --- Same predicate, both sites: connect and replay agree on every reason ---
TEST(VerifyGate, ConnectAndReplayAgree) {
  const auto rseed = genesis_seed("racheal");
  const Address oliver = addr_of("oliver");

  Txn bad_addr = make_signed(rseed, oliver, 100, 0); bad_addr.from = oliver;
  Txn bad_sig = make_signed(rseed, oliver, 100, 0); bad_sig.sig[5] ^= 0xFF;
  Txn stale = make_signed(rseed, oliver, 100, 7);
  Txn overspend = make_signed(rseed, oliver, 4000, 0);
  Txn ok = make_signed(rseed, oliver, 100, 0);

  for (const Txn& t : {bad_addr, bad_sig, stale, overspend, ok}) {
    Chain c;
    const Block b = build_block(1, c.tipHash(), NOW, {t});
    const Reason connect_reason = c.connectBlock(b, NOW);
    const std::vector<Block> chain = {genesisBlock(), b};
    const Reason replay_reason = replayFromGenesis(chain, NOW).reason;
    EXPECT_EQ(connect_reason, replay_reason) << "connect/replay disagree";
  }
}

// --- monitor verify contract strings (Chain/State §7.3) ---
TEST(VerifyGate, MonitorVerifyBadSigString) {
  const auto rseed = genesis_seed("racheal");
  Txn bad = make_signed(rseed, addr_of("oliver"), 100, 0);
  bad.sig[0] ^= 0x01;
  const Block b = build_block(1, genesisBlock().header.hash(), NOW, {bad});
  const std::vector<Block> chain = {genesisBlock(), b};

  const auto vr = replayFromGenesis(chain, NOW);
  EXPECT_EQ(monitor_verify_line(vr, chain.size()),
            "FAIL block 1: BAD_SIG (ed25519 verify failed for txn 0)");
}

// --- A clean signed chain: OK + supply invariant Σbalance == GENESIS_SUPPLY ---
TEST(VerifyGate, ValidSignedChainOkAndSupplyConserved) {
  const auto rseed = genesis_seed("racheal");
  const Txn t = make_signed(rseed, addr_of("oliver"), 100, 0);
  const Block b = build_block(1, genesisBlock().header.hash(), NOW, {t});
  const std::vector<Block> chain = {genesisBlock(), b};

  const auto vr = replayFromGenesis(chain, NOW);
  EXPECT_TRUE(vr.ok) << monitor_verify_line(vr, chain.size());
  EXPECT_EQ(monitor_verify_line(vr, chain.size()), "OK 2 blocks");
}
