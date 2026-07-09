// Mempool admission (Architecture Mempool §2–§3): the frozen admit() order, the
// single-pending-nonce rule, dedup/capacity/eviction, and AdmitResult↔Reason
// token parity. admit() reuses the SAME consensus gate as connect/replay, so a
// rejection surfaces the identical token. Signed txns come from genesis
// identities (seed = SHA-256("txchain-genesis-"||name)). Label: mempool.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "txchain/chain/genesis.hpp"
#include "txchain/chain/reason.hpp"
#include "txchain/chain/types.hpp"
#include "txchain/crypto/address.hpp"
#include "txchain/crypto/ed25519.hpp"
#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/crypto/sha256.hpp"
#include "txchain/mempool/mempool.hpp"
#include "txchain/serialize/canonical.hpp"

namespace {

using txchain::chain::AccountState;
using txchain::chain::Address;
using txchain::chain::applyGenesis;
using txchain::chain::Reason;
using txchain::chain::reasonName;
using txchain::chain::Txn;
using txchain::mempool::admit_result_name;
using txchain::mempool::AdmitResult;
using txchain::mempool::Mempool;

txchain::crypto::Seed32 genesis_seed(const std::string& name) {
  const std::string s = "txchain-genesis-" + name;
  return txchain::crypto::sha256(txchain::crypto::ByteView(
      reinterpret_cast<const txchain::crypto::Byte*>(s.data()), s.size()));
}

Address addr_of(const std::string& name) {
  return txchain::crypto::address(txchain::crypto::derive_pubkey(genesis_seed(name)));
}

Txn signed_txn(const std::string& from_name, const Address& to, std::uint64_t amount,
               std::uint64_t nonce) {
  const auto seed = genesis_seed(from_name);
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

// A mutable committed-tip index + its accountAt lookup (absent ⇒ {0,0}).
struct Tip {
  std::map<Address, AccountState> st;
  Tip() { applyGenesis(st); }  // racheal..daniel each {1000, 0}
  Mempool::AccountLookup lookup() {
    return [this](const Address& a) -> AccountState {
      const auto it = st.find(a);
      return it == st.end() ? AccountState{} : it->second;
    };
  }
};

}  // namespace

TEST(Mempool, AdmitValidTxn) {
  Tip tip;
  Mempool mp(tip.lookup());
  const Txn t = signed_txn("racheal", addr_of("oliver"), 100, 0);
  EXPECT_EQ(mp.admit(t), AdmitResult::OK);
  EXPECT_EQ(mp.size(), 1u);
  EXPECT_TRUE(mp.contains(txchain::serialize::txid(t)));
}

TEST(Mempool, DuplicateDoesNotBumpSeq) {
  Tip tip;
  Mempool mp(tip.lookup());
  const Txn a = signed_txn("racheal", addr_of("oliver"), 100, 0);
  ASSERT_EQ(mp.admit(a), AdmitResult::OK);       // seq 0
  EXPECT_EQ(mp.admit(a), AdmitResult::DUPLICATE);  // no second entry, no seq bump
  EXPECT_EQ(mp.size(), 1u);

  const Txn b = signed_txn("oliver", addr_of("racheal"), 100, 0);
  ASSERT_EQ(mp.admit(b), AdmitResult::OK);  // should get seq 1, not 2
  const auto ordered = mp.sortedBySeq();
  ASSERT_EQ(ordered.size(), 2u);
  EXPECT_EQ(ordered[0]->feeless_seq, 0u);
  EXPECT_EQ(ordered[1]->feeless_seq, 1u);
}

TEST(Mempool, BadAddr) {
  Tip tip;
  Mempool mp(tip.lookup());
  Txn t = signed_txn("racheal", addr_of("oliver"), 100, 0);
  t.from = addr_of("oliver");  // from != address(pubkey)
  EXPECT_EQ(mp.admit(t), AdmitResult::BAD_ADDR);
}

TEST(Mempool, BadSig) {
  Tip tip;
  Mempool mp(tip.lookup());
  Txn t = signed_txn("racheal", addr_of("oliver"), 100, 0);
  t.sig[0] ^= 0x01;  // different txid (sig is hashed) — not a duplicate, fails the gate
  EXPECT_EQ(mp.admit(t), AdmitResult::BAD_SIG);
}

TEST(Mempool, StaleNonceWrongNonce) {
  Tip tip;
  Mempool mp(tip.lookup());
  const Txn t = signed_txn("racheal", addr_of("oliver"), 100, 3);  // committed nonce is 0
  EXPECT_EQ(mp.admit(t), AdmitResult::STALE_NONCE);
}

TEST(Mempool, PendingSlotBusySameNonce) {
  Tip tip;
  Mempool mp(tip.lookup());
  ASSERT_EQ(mp.admit(signed_txn("racheal", addr_of("oliver"), 100, 0)), AdmitResult::OK);
  // A second, DIFFERENT txn at the correct (still-committed) nonce 0: the slot is busy.
  EXPECT_EQ(mp.admit(signed_txn("racheal", addr_of("matthew"), 50, 0)),
            AdmitResult::PENDING_SLOT_BUSY);
}

// Normative order (Architecture §3): the nonce check (against the TIP nonce)
// precedes the slot check, so a higher nonce while the prior is unmined is
// STALE_NONCE — the committed nonce has not advanced yet.
TEST(Mempool, HigherNonceWhilePendingIsStale) {
  Tip tip;
  Mempool mp(tip.lookup());
  ASSERT_EQ(mp.admit(signed_txn("racheal", addr_of("oliver"), 100, 0)), AdmitResult::OK);
  EXPECT_EQ(mp.admit(signed_txn("racheal", addr_of("oliver"), 100, 1)), AdmitResult::STALE_NONCE);
}

TEST(Mempool, InsufficientFunds) {
  Tip tip;
  Mempool mp(tip.lookup());
  const Txn t = signed_txn("racheal", addr_of("oliver"), 5000, 0);  // balance is 1000
  EXPECT_EQ(mp.admit(t), AdmitResult::INSUFFICIENT_FUNDS);
}

TEST(Mempool, UnparseableBytes) {
  Tip tip;
  Mempool mp(tip.lookup());
  const Txn t = signed_txn("racheal", addr_of("oliver"), 100, 0);
  const auto bytes = txchain::serialize::serialize(t);  // 153 canonical bytes

  // 152 bytes (truncated) and 154 bytes (padded) both fail to deserialize.
  EXPECT_EQ(mp.admit(txchain::crypto::ByteView(bytes.data(), 152)), AdmitResult::UNPARSEABLE);
  std::vector<txchain::crypto::Byte> padded(bytes.begin(), bytes.end());
  padded.push_back(0x00);  // 154
  EXPECT_EQ(mp.admit(txchain::crypto::ByteView(padded.data(), padded.size())),
            AdmitResult::UNPARSEABLE);

  // Correct length but ver != 0x01.
  auto bad_ver = bytes;
  bad_ver[0] = 0x02;
  EXPECT_EQ(mp.admit(txchain::crypto::ByteView(bad_ver.data(), bad_ver.size())),
            AdmitResult::UNPARSEABLE);
}

TEST(Mempool, MempoolFull) {
  Tip tip;
  Mempool mp(tip.lookup(), /*cap=*/2);
  ASSERT_EQ(mp.admit(signed_txn("racheal", addr_of("daniel"), 10, 0)), AdmitResult::OK);
  ASSERT_EQ(mp.admit(signed_txn("oliver", addr_of("daniel"), 10, 0)), AdmitResult::OK);
  // A third distinct-sender valid txn overflows the cap.
  EXPECT_EQ(mp.admit(signed_txn("matthew", addr_of("daniel"), 10, 0)), AdmitResult::MEMPOOL_FULL);
}

TEST(Mempool, SortedBySeqAscending) {
  Tip tip;
  Mempool mp(tip.lookup());
  const Txn a = signed_txn("racheal", addr_of("daniel"), 10, 0);
  const Txn b = signed_txn("oliver", addr_of("daniel"), 10, 0);
  const Txn c = signed_txn("matthew", addr_of("daniel"), 10, 0);
  ASSERT_EQ(mp.admit(a), AdmitResult::OK);
  ASSERT_EQ(mp.admit(b), AdmitResult::OK);
  ASSERT_EQ(mp.admit(c), AdmitResult::OK);

  const auto ordered = mp.sortedBySeq();
  ASSERT_EQ(ordered.size(), 3u);
  EXPECT_EQ(ordered[0]->feeless_seq, 0u);
  EXPECT_EQ(ordered[1]->feeless_seq, 1u);
  EXPECT_EQ(ordered[2]->feeless_seq, 2u);
  EXPECT_EQ(ordered[0]->txid, txchain::serialize::txid(a));
  EXPECT_EQ(ordered[2]->txid, txchain::serialize::txid(c));
}

TEST(Mempool, EvictionFreesSlotThenNextNonceAdmits) {
  Tip tip;
  Mempool mp(tip.lookup());
  const Txn n0 = signed_txn("racheal", addr_of("oliver"), 100, 0);
  ASSERT_EQ(mp.admit(n0), AdmitResult::OK);
  ASSERT_EQ(mp.size(), 1u);

  // Committing the block that includes n0 evicts it and frees racheal's slot.
  mp.evictIncluded({n0});
  EXPECT_EQ(mp.size(), 0u);
  EXPECT_FALSE(mp.contains(txchain::serialize::txid(n0)));

  // The tip advances (racheal nonce 0→1, balance 1000→900); nonce 1 now admits.
  tip.st[addr_of("racheal")] = AccountState{900, 1};
  EXPECT_EQ(mp.admit(signed_txn("racheal", addr_of("oliver"), 100, 1)), AdmitResult::OK);
}

TEST(Mempool, TokenParityWithConsensusReason) {
  // Numeric parity: the shared members share values with the consensus enum.
  EXPECT_EQ(static_cast<int>(AdmitResult::BAD_ADDR), static_cast<int>(Reason::BAD_ADDR));
  EXPECT_EQ(static_cast<int>(AdmitResult::BAD_SIG), static_cast<int>(Reason::BAD_SIG));
  EXPECT_EQ(static_cast<int>(AdmitResult::STALE_NONCE), static_cast<int>(Reason::STALE_NONCE));
  EXPECT_EQ(static_cast<int>(AdmitResult::INSUFFICIENT_FUNDS),
            static_cast<int>(Reason::INSUFFICIENT_FUNDS));

  // Spelling parity: an RPC surfaces one token for a consensus-shared rejection.
  EXPECT_STREQ(admit_result_name(AdmitResult::BAD_ADDR), reasonName(Reason::BAD_ADDR));
  EXPECT_STREQ(admit_result_name(AdmitResult::BAD_SIG), reasonName(Reason::BAD_SIG));
  EXPECT_STREQ(admit_result_name(AdmitResult::STALE_NONCE), reasonName(Reason::STALE_NONCE));
  EXPECT_STREQ(admit_result_name(AdmitResult::INSUFFICIENT_FUNDS),
               reasonName(Reason::INSUFFICIENT_FUNDS));

  // Local outcomes have their own spelling (never a consensus reason).
  EXPECT_STREQ(admit_result_name(AdmitResult::DUPLICATE), "DUPLICATE");
  EXPECT_STREQ(admit_result_name(AdmitResult::PENDING_SLOT_BUSY), "PENDING_SLOT_BUSY");
  EXPECT_STREQ(admit_result_name(AdmitResult::MEMPOOL_FULL), "MEMPOOL_FULL");
  EXPECT_STREQ(admit_result_name(AdmitResult::UNPARSEABLE), "UNPARSEABLE");
}
