// Canonical + chain.jsonl round-trip: decode(encode(x)) == x for Txn and
// BlockHeader, and parse(render(block)) == block; plus deserialize rejection of
// wrong length / bad version. Label: serialize.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/serialize/canonical.hpp"
#include "txchain/serialize/json.hpp"
#include "txchain/serialize/types.hpp"

namespace {

using txchain::crypto::Byte;
using txchain::crypto::ByteView;
using txchain::serialize::Block;
using txchain::serialize::BlockHeader;
using txchain::serialize::deserialize_header;
using txchain::serialize::deserialize_txn;
using txchain::serialize::from_json_line;
using txchain::serialize::serialize;
using txchain::serialize::to_json_line;
using txchain::serialize::Txn;
using txchain::serialize::txns_hash;

Txn sample_txn(int seed) {
  Txn t;
  t.ver = 1;
  for (auto& b : t.from) b = static_cast<Byte>((seed * 3 + 1) & 0xFF);
  for (auto& b : t.to) b = static_cast<Byte>((seed * 5 + 2) & 0xFF);
  t.amount = static_cast<std::uint64_t>(seed) * 1000 + 7;
  t.nonce = static_cast<std::uint64_t>(seed);
  for (auto& b : t.pubkey) b = static_cast<Byte>((seed * 7 + 3) & 0xFF);
  for (auto& b : t.sig) b = static_cast<Byte>((seed * 11 + 4) & 0xFF);
  return t;
}

}  // namespace

TEST(SerializeRoundtrip, TxnBinary) {
  for (int s = 0; s < 5; ++s) {
    const Txn t = sample_txn(s);
    const auto bytes = serialize(t);
    EXPECT_EQ(bytes.size(), 153u);
    const auto back = deserialize_txn(ByteView(bytes.data(), bytes.size()));
    ASSERT_TRUE(back.has_value());
    EXPECT_TRUE(*back == t);
  }
}

TEST(SerializeRoundtrip, HeaderBinary) {
  BlockHeader h;
  h.index = 42;
  h.timestamp = 1704067201;
  h.nonce = 99;
  for (auto& b : h.prevHash) b = 0x11;
  for (auto& b : h.txnsHash) b = 0x22;
  const auto bytes = serialize(h);
  EXPECT_EQ(bytes.size(), 88u);
  const auto back = deserialize_header(ByteView(bytes.data(), bytes.size()));
  ASSERT_TRUE(back.has_value());
  EXPECT_TRUE(*back == h);
}

TEST(SerializeRoundtrip, DeserializeRejectsWrongLength) {
  std::array<Byte, 152> short_txn{};
  EXPECT_FALSE(deserialize_txn(ByteView(short_txn.data(), short_txn.size())).has_value());
  std::array<Byte, 154> long_txn{};
  long_txn[0] = 0x01;
  EXPECT_FALSE(deserialize_txn(ByteView(long_txn.data(), long_txn.size())).has_value());
  std::array<Byte, 87> short_hdr{};
  EXPECT_FALSE(deserialize_header(ByteView(short_hdr.data(), short_hdr.size())).has_value());
}

TEST(SerializeRoundtrip, DeserializeRejectsBadVersion) {
  auto bytes = serialize(sample_txn(1));
  bytes[0] = 0x02;  // not the frozen ver 0x01
  EXPECT_FALSE(deserialize_txn(ByteView(bytes.data(), bytes.size())).has_value());
}

TEST(SerializeRoundtrip, ChainJsonlBlock) {
  Block blk;
  blk.header.index = 7;
  blk.header.timestamp = 1704067201;
  blk.header.nonce = 1234;
  for (auto& b : blk.header.prevHash) b = 0xAB;
  blk.txns.push_back(sample_txn(1));
  blk.txns.push_back(sample_txn(2));
  blk.header.txnsHash = txns_hash(blk.txns);

  const std::string line = to_json_line(blk);
  const auto parsed = from_json_line(line);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_TRUE(*parsed == blk);
}

TEST(SerializeRoundtrip, EmptyBlockJsonl) {
  Block blk;
  blk.header.index = 0;
  blk.header.timestamp = 1;
  blk.header.txnsHash = txns_hash(blk.txns);  // SHA-256("")

  const std::string line = to_json_line(blk);
  const auto parsed = from_json_line(line);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_TRUE(*parsed == blk);
}

TEST(SerializeRoundtrip, MalformedJsonRejected) {
  EXPECT_FALSE(from_json_line("not json").has_value());
  EXPECT_FALSE(from_json_line("{}").has_value());
  // header present but a hex field is the wrong width
  EXPECT_FALSE(from_json_line(R"({"header":{"index":0,"timestamp":0,"prevHash":"00","txnsHash":"00","nonce":0},"txns":[],"blockHash":"00"})").has_value());
}
