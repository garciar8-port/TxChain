// Golden vector C (Architecture Data Model §5.3): txnsHash over Vector A, the
// empty-block txnsHash constant, and the block header hash. Label: serialize.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "serialize_vectors.hpp"
#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/serialize/canonical.hpp"

namespace {

using txchain::crypto::from_hex;
using txchain::crypto::to_hex;
using txchain::serialize::block_hash;
using txchain::serialize::BlockHeader;
using txchain::serialize::txns_hash;
using txchain::serialize::Txn;
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

TEST(GoldenBlockHash, TxnsHashOverSingleTxn) {
  const std::vector<Txn> txns{vector_a()};
  EXPECT_EQ(to_hex(txns_hash(txns)), std::string(vec::kVecC_txnsHash));
}

TEST(GoldenBlockHash, EmptyBlockTxnsHashIsShaOfEmpty) {
  const std::vector<Txn> empty;
  EXPECT_EQ(to_hex(txns_hash(empty)), std::string(vec::kEmptyTxnsHash));
}

TEST(GoldenBlockHash, BlockHashMatchesFrozen) {
  BlockHeader h;
  h.index = vec::kVecC_index;
  h.timestamp = vec::kVecC_timestamp;
  ASSERT_TRUE(from_hex(vec::kVecC_prevHash, h.prevHash));
  ASSERT_TRUE(from_hex(vec::kVecC_txnsHash, h.txnsHash));
  h.nonce = vec::kVecC_nonce;
  EXPECT_EQ(to_hex(block_hash(h)), std::string(vec::kVecC_blockHash));
}
