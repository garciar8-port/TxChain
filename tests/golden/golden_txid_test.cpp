// Golden vector A (Architecture Data Model §5.1): the frozen signed payload and
// txid for the canonical transaction. Constants in serialize_vectors.hpp were
// computed by an independent Python SHA-256. Label: serialize.

#include <gtest/gtest.h>

#include <string>

#include "serialize_vectors.hpp"
#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/serialize/canonical.hpp"

namespace {

using txchain::crypto::from_hex;
using txchain::crypto::to_hex;
using txchain::serialize::signed_payload;
using txchain::serialize::Txn;
using txchain::serialize::txid;
namespace vec = txchain::serialize::test;

Txn vector_a() {
  Txn t;
  t.ver = 1;
  EXPECT_TRUE(from_hex(vec::kVecA_from, t.from));
  EXPECT_TRUE(from_hex(vec::kVecA_to, t.to));
  t.amount = vec::kVecA_amount;
  t.nonce = vec::kVecA_nonce;
  EXPECT_TRUE(from_hex(vec::kVecA_pubkey, t.pubkey));
  EXPECT_TRUE(from_hex(vec::kVecA_sig, t.sig));
  return t;
}

}  // namespace

TEST(GoldenTxid, SerializedSizeIs153) {
  EXPECT_EQ(txchain::serialize::serialize(vector_a()).size(), 153u);
}

TEST(GoldenTxid, SignedPayloadMatchesFrozen) {
  const auto sp = signed_payload(vector_a());
  EXPECT_EQ(sp.size(), 89u);
  EXPECT_EQ(to_hex(sp), std::string(vec::kVecA_signed_payload));
}

TEST(GoldenTxid, TxidMatchesFrozen) {
  EXPECT_EQ(to_hex(txid(vector_a())), std::string(vec::kVecA_txid));
}
