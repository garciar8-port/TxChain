// txnode lifecycle tests (Architecture Node §1.3, §10): boot self-validation,
// the M1 cadence sealer, and config precedence. The sealer is driven with
// explicit timestamps so N iterations run deterministically with NO real
// sleeping — the sleep loop lives only in apps/txnode/main.cpp. Label: node.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "txchain/chain/reason.hpp"
#include "txchain/chain/store.hpp"
#include "txchain/chain/validate.hpp"
#include "txchain/node/node.hpp"

namespace {

namespace fs = std::filesystem;

using txchain::chain::ChainStore;
using txchain::chain::monitor_verify_line;
using txchain::chain::Reason;
using txchain::chain::replayFromGenesis;
using txchain::node::BootResult;
using txchain::node::Node;
using txchain::node::NodeConfig;
using txchain::node::resolve_config;
using txchain::node::should_seal_now;

// A now_s comfortably past GENESIS_TIMESTAMP so every sealed block's timestamp is
// driven by now_s (not clamped up to tip+1) and stays inside the skew window.
constexpr std::uint64_t NOW = 2000000000;

std::string fresh_datadir() {
  static int n = 0;
  const auto p = fs::temp_directory_path() / ("txchain_node_test_" + std::to_string(n++));
  std::error_code ec;
  fs::remove_all(p, ec);
  return p.string();
}

std::vector<std::string> read_lines(const std::string& path) {
  std::ifstream in(path);
  std::vector<std::string> lines;
  std::string l;
  while (std::getline(in, l))
    if (!l.empty()) lines.push_back(l);
  return lines;
}

NodeConfig resolve_args(std::vector<std::string> args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
  return resolve_config(static_cast<int>(argv.size()), argv.data());
}

// Seal `k` empty blocks with strictly increasing timestamps; ASSERT each commits.
void seal_k(Node& node, std::size_t k) {
  for (std::size_t i = 1; i <= k; ++i)
    ASSERT_EQ(node.seal_next_block(NOW + i), Reason::OK) << "seal " << i;
}

}  // namespace

// Boot on an empty datadir writes exactly the genesis block; height 0.
TEST(NodeBoot, FreshDatadirInitialisesGenesis) {
  const auto dir = fresh_datadir();
  NodeConfig cfg;
  cfg.datadir = dir;

  BootResult br;
  Node node = Node::boot(cfg, NOW, br);
  ASSERT_TRUE(br.ok);
  EXPECT_EQ(node.height(), 0u);

  const auto lines = read_lines(ChainStore::forDatadir(dir).path());
  EXPECT_EQ(lines.size(), 1u);  // genesis only
}

// The core acceptance: an idle node advances its chain. Sealing K empty blocks
// gives height K, K+1 chain.jsonl lines, and `monitor verify` OK over the file.
TEST(NodeSealer, IdleChainAdvances) {
  const auto dir = fresh_datadir();
  NodeConfig cfg;
  cfg.datadir = dir;
  cfg.mine = true;

  BootResult br;
  Node node = Node::boot(cfg, NOW, br);
  ASSERT_TRUE(br.ok);

  constexpr std::size_t K = 7;  // ~ the "35 s @ 5 s cadence" demo height
  seal_k(node, K);
  EXPECT_EQ(node.height(), K);

  const auto store = ChainStore::forDatadir(dir);
  const auto lines = read_lines(store.path());
  EXPECT_EQ(lines.size(), K + 1u);

  const auto loaded = store.load();
  ASSERT_TRUE(loaded.status.ok);
  const auto vr = replayFromGenesis(loaded.blocks, NOW + K + 1);
  EXPECT_TRUE(vr.ok);
  EXPECT_EQ(monitor_verify_line(vr, loaded.blocks.size()), "OK " + std::to_string(K + 1) + " blocks");
}

// Clean restart is a no-op: re-booting the same datadir replay-validates the
// existing chain and comes up at the sealed height (no re-mine, no data loss).
TEST(NodeBoot, RestartPreservesHeightAndResumes) {
  const auto dir = fresh_datadir();
  NodeConfig cfg;
  cfg.datadir = dir;
  cfg.mine = true;

  {
    BootResult br;
    Node node = Node::boot(cfg, NOW, br);
    ASSERT_TRUE(br.ok);
    seal_k(node, 3);
    EXPECT_EQ(node.height(), 3u);
  }

  // Fresh process would call boot() again against the same datadir.
  BootResult br2;
  Node restarted = Node::boot(cfg, NOW + 100, br2);
  ASSERT_TRUE(br2.ok);
  EXPECT_EQ(restarted.height(), 3u);

  // ...and resumes sealing from the loaded tip.
  ASSERT_EQ(restarted.seal_next_block(NOW + 101), Reason::OK);
  EXPECT_EQ(restarted.height(), 4u);
  EXPECT_EQ(read_lines(ChainStore::forDatadir(dir).path()).size(), 5u);
}

// Boot self-validation refuses a tampered chain: flipping one hex digit of block
// 3's prevHash makes boot fail with the same (failIndex, reason) monitor verify
// would report — before any listener binds.
TEST(NodeBoot, RefusesTamperedChain) {
  const auto dir = fresh_datadir();
  NodeConfig cfg;
  cfg.datadir = dir;
  cfg.mine = true;

  {
    BootResult br;
    Node node = Node::boot(cfg, NOW, br);
    ASSERT_TRUE(br.ok);
    seal_k(node, 5);  // genesis + 5 → 6 lines
  }

  const auto path = ChainStore::forDatadir(dir).path();
  auto lines = read_lines(path);
  ASSERT_EQ(lines.size(), 6u);
  const std::string key = "\"prevHash\":\"";
  const auto pos = lines[3].find(key);
  ASSERT_NE(pos, std::string::npos);
  const auto hp = pos + key.size();
  lines[3][hp] = (lines[3][hp] == '0') ? '1' : '0';  // flip one hex digit
  {
    std::ofstream out(path, std::ios::trunc);
    for (const auto& l : lines) out << l << "\n";
  }

  BootResult br;
  Node node = Node::boot(cfg, NOW + 10, br);
  EXPECT_FALSE(br.ok);
  EXPECT_EQ(br.status.failIndex, 3u);
  EXPECT_EQ(br.status.reason, Reason::BAD_LINK);
}

// A datadir whose chain.jsonl has a non-canonical genesis is not this node's
// chain — boot refuses at block 0.
TEST(NodeBoot, RefusesNonCanonicalGenesis) {
  const auto dir = fresh_datadir();
  std::error_code ec;
  fs::create_directories(dir, ec);
  {
    std::ofstream out(ChainStore::forDatadir(dir).path(), std::ios::trunc);
    // A structurally-plausible but wrong genesis line (all-zero hashes but a
    // non-genesis index) — parses, but != genesisBlock().
    out << R"({"index":0,"timestamp":1,"prevHash":")" << std::string(64, '0')
        << R"(","txnsHash":")" << std::string(64, '0') << R"(","nonce":0,"txns":[],"blockHash":")"
        << std::string(64, '0') << "\"}\n";
  }

  NodeConfig cfg;
  cfg.datadir = dir;
  BootResult br;
  Node node = Node::boot(cfg, NOW, br);
  EXPECT_FALSE(br.ok);
  EXPECT_EQ(br.status.failIndex, 0u);
}

// The seal decision (pure): mine-gated, MAX_TXNS early flush, cadence timer,
// and --seal-cadence 0 disabling the timer.
TEST(NodeSealDecision, ShouldSealNow) {
  NodeConfig cfg;
  cfg.mine = true;
  cfg.seal_cadence_ms = 5000;
  cfg.max_txns_per_block = 8;

  EXPECT_FALSE(should_seal_now(cfg, 3000, 0));  // timer not elapsed, mempool idle
  EXPECT_TRUE(should_seal_now(cfg, 6000, 0));   // cadence elapsed
  EXPECT_TRUE(should_seal_now(cfg, 0, 8));      // MAX_TXNS early flush
  EXPECT_FALSE(should_seal_now(cfg, 0, 7));     // below cap, timer not elapsed

  NodeConfig off = cfg;
  off.seal_cadence_ms = 0;                      // M3 flag flip: cadence disabled
  EXPECT_FALSE(should_seal_now(off, 999999, 0));  // no timer sealing
  EXPECT_TRUE(should_seal_now(off, 0, 8));        // ...but a full mempool still flushes

  NodeConfig replica = cfg;
  replica.mine = false;                        // a non-mining replica never seals
  EXPECT_FALSE(should_seal_now(replica, 999999, 8));
}

// --seal-cadence 0 with no --mine seals nothing even when driven — the replica
// role. (seal_next_block itself always seals when called; the gate is upstream.)
TEST(NodeSealDecision, NonMiningReplicaNeverSeals) {
  NodeConfig cfg;
  cfg.mine = false;
  cfg.seal_cadence_ms = 0;
  EXPECT_FALSE(should_seal_now(cfg, 10'000'000, 0));
  EXPECT_FALSE(should_seal_now(cfg, 0, 100));
}

// Config precedence: built-in default → node.json → CLI flag (CLI wins).
TEST(NodeConfigResolve, NodeJsonOverriddenByCli) {
  const auto dir = fresh_datadir();
  std::error_code ec;
  fs::create_directories(dir, ec);
  {
    std::ofstream out(dir + "/node.json", std::ios::trunc);
    out << R"({"seal_cadence_ms":9999,"mine":true,"max_txns_per_block":4})" << "\n";
  }

  // node.json alone: its values win over the built-in defaults.
  const auto from_json = resolve_args({"txnode", "--datadir", dir});
  EXPECT_EQ(from_json.seal_cadence_ms, 9999u);
  EXPECT_TRUE(from_json.mine);
  EXPECT_EQ(from_json.max_txns_per_block, 4u);

  // A matching CLI flag overrides node.json; unmentioned keys keep the node.json value.
  const auto overridden = resolve_args({"txnode", "--datadir", dir, "--seal-cadence", "3000"});
  EXPECT_EQ(overridden.seal_cadence_ms, 3000u);  // CLI wins
  EXPECT_TRUE(overridden.mine);                  // still from node.json
  EXPECT_EQ(overridden.max_txns_per_block, 4u);  // still from node.json
}

// Built-in defaults are the frozen M1 launcher values when nothing overrides them.
TEST(NodeConfigResolve, BuiltInDefaults) {
  const auto cfg = resolve_args({"txnode"});
  EXPECT_EQ(cfg.datadir, "./txchain-data");
  EXPECT_FALSE(cfg.mine);
  EXPECT_EQ(cfg.seal_cadence_ms, 5000u);
  EXPECT_TRUE(cfg.seal_empty);
  EXPECT_EQ(cfg.max_txns_per_block, 8u);

  const auto mining = resolve_args({"txnode", "--mine", "--seal-cadence", "0"});
  EXPECT_TRUE(mining.mine);
  EXPECT_EQ(mining.seal_cadence_ms, 0u);
}
