// chain.jsonl persistence round-trip + tamper tests (Architecture Data Model §4).
// Uses temp files. Label: chain.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "txchain/chain/genesis.hpp"
#include "txchain/chain/store.hpp"
#include "txchain/chain/types.hpp"
#include "txchain/chain/validate.hpp"
#include "txchain/crypto/hashutil.hpp"

namespace {

namespace fs = std::filesystem;

using txchain::chain::Block;
using txchain::chain::ChainStore;
using txchain::chain::genesisBlock;
using txchain::chain::Hash256;
using txchain::chain::monitor_verify_line;
using txchain::chain::Reason;
using txchain::chain::replayFromGenesis;
using txchain::crypto::to_hex;

constexpr std::uint64_t NOW = 2000000000;
constexpr const char* kEmptyHash =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

std::string tmp_path() {
  static int n = 0;
  const auto p = fs::temp_directory_path() / ("txchain_jsonl_test_" + std::to_string(n++) + ".jsonl");
  fs::remove(p);
  return p.string();
}

std::vector<Block> buildChain(std::size_t count) {
  std::vector<Block> v;
  const Block g = genesisBlock();
  v.push_back(g);
  Hash256 prev = g.header.hash();
  std::uint64_t ts = g.header.timestamp;
  for (std::size_t i = 1; i < count; ++i) {
    Block b;
    b.header.index = i;
    ts += 1;
    b.header.timestamp = ts;
    b.header.prevHash = prev;
    b.header.txnsHash = b.computeTxnsHash();
    b.header.nonce = 0;
    v.push_back(b);
    prev = b.header.hash();
  }
  return v;
}

std::vector<std::string> readLines(const std::string& path) {
  std::ifstream in(path);
  std::vector<std::string> lines;
  std::string l;
  while (std::getline(in, l))
    if (!l.empty()) lines.push_back(l);
  return lines;
}

void writeLines(const std::string& path, const std::vector<std::string>& lines) {
  std::ofstream out(path, std::ios::trunc);
  for (const auto& l : lines) out << l << "\n";
}

}  // namespace

TEST(ChainJsonl, RoundTrip) {
  const auto path = tmp_path();
  const ChainStore store(path);
  const auto chain = buildChain(5);
  for (const auto& b : chain) ASSERT_TRUE(store.appendBlock(b));

  const auto loaded = store.load();
  ASSERT_TRUE(loaded.status.ok);
  ASSERT_EQ(loaded.blocks.size(), chain.size());
  for (std::size_t i = 0; i < chain.size(); ++i) {
    EXPECT_TRUE(loaded.blocks[i] == chain[i]) << "block " << i;
    // Recomputed blockHash on load equals the block written (never trusted from disk).
    EXPECT_EQ(to_hex(loaded.blocks[i].header.hash()), to_hex(chain[i].header.hash()));
  }
  fs::remove(path);
}

TEST(ChainJsonl, GenesisLineShape) {
  const auto path = tmp_path();
  const ChainStore store(path);
  ASSERT_TRUE(store.appendBlock(genesisBlock()));

  const auto lines = readLines(path);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_NE(lines[0].find("\"txns\":[]"), std::string::npos);  // empty genesis txns
  EXPECT_NE(lines[0].find("\"prevHash\":\"" + std::string(64, '0') + "\""), std::string::npos);
  EXPECT_NE(lines[0].find(std::string("\"txnsHash\":\"") + kEmptyHash + "\""), std::string::npos);
  fs::remove(path);
}

TEST(ChainJsonl, LoadThenReplayOk) {
  const auto path = tmp_path();
  const ChainStore store(path);
  for (const auto& b : buildChain(8)) ASSERT_TRUE(store.appendBlock(b));

  const auto loaded = store.load();
  ASSERT_TRUE(loaded.status.ok);
  const auto vr = replayFromGenesis(loaded.blocks, NOW);
  EXPECT_TRUE(vr.ok);
  EXPECT_EQ(monitor_verify_line(vr, loaded.blocks.size()), "OK 8 blocks");
  fs::remove(path);
}

// The canonical demo: flip one hex digit of block 3's prevHash in the file.
TEST(ChainJsonl, SingleHexDigitTamperPrevHash) {
  const auto path = tmp_path();
  const ChainStore store(path);
  for (const auto& b : buildChain(5)) ASSERT_TRUE(store.appendBlock(b));

  auto lines = readLines(path);
  ASSERT_EQ(lines.size(), 5u);
  const std::string key = "\"prevHash\":\"";
  const auto pos = lines[3].find(key);
  ASSERT_NE(pos, std::string::npos);
  const auto hp = pos + key.size();
  lines[3][hp] = (lines[3][hp] == '0') ? '1' : '0';  // a different valid hex digit
  writeLines(path, lines);

  const auto loaded = store.load();
  ASSERT_TRUE(loaded.status.ok);  // still valid hex / right length → parses
  const auto vr = replayFromGenesis(loaded.blocks, NOW);
  EXPECT_FALSE(vr.ok);
  EXPECT_EQ(vr.failIndex, 3u);
  EXPECT_EQ(vr.reason, Reason::BAD_LINK);
  EXPECT_EQ(monitor_verify_line(vr, loaded.blocks.size()),
            "FAIL block 3: BAD_LINK (prevHash mismatch)");
  fs::remove(path);
}

// A wrong-length hex field (63 chars) is a parse failure, not silently accepted.
TEST(ChainJsonl, WrongLengthHexParseFailure) {
  const auto path = tmp_path();
  const ChainStore store(path);
  for (const auto& b : buildChain(3)) ASSERT_TRUE(store.appendBlock(b));

  auto lines = readLines(path);
  const std::string key = "\"prevHash\":\"";
  const auto pos = lines[2].find(key);
  ASSERT_NE(pos, std::string::npos);
  lines[2].erase(pos + key.size(), 1);  // 64 → 63 hex chars
  writeLines(path, lines);

  const auto loaded = store.load();
  EXPECT_FALSE(loaded.status.ok);
  EXPECT_EQ(loaded.status.failIndex, 2u);
  fs::remove(path);
}

// Rendering fidelity: frozen hex lengths, lowercase, no 0x prefix.
TEST(ChainJsonl, RenderingFidelity) {
  const auto path = tmp_path();
  const ChainStore store(path);
  ASSERT_TRUE(store.appendBlock(genesisBlock()));

  const auto lines = readLines(path);
  ASSERT_EQ(lines.size(), 1u);
  const std::string& l = lines[0];

  auto field = [&](const std::string& name) -> std::string {
    const std::string key = "\"" + name + "\":\"";
    const auto p = l.find(key);
    EXPECT_NE(p, std::string::npos);
    const auto start = p + key.size();
    const auto end = l.find('"', start);
    return l.substr(start, end - start);
  };

  const auto ph = field("prevHash");
  EXPECT_EQ(ph.size(), 64u);
  for (char c : ph) EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
  EXPECT_EQ(field("txnsHash").size(), 64u);
  EXPECT_EQ(field("blockHash").size(), 64u);
  EXPECT_EQ(l.find("0x"), std::string::npos);  // no 0x prefix anywhere
  fs::remove(path);
}
