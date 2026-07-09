#pragma once
// txnode lifecycle: boot self-validation + M1 cadence sealer (Architecture Node
// §1.3, §10). This is the TESTABLE core of the node process — everything that
// touches the chain, its persistence, and the seal decision lives here as pure
// or injectable functions (no sleeping, no signals, no wall clock forced on us).
// apps/txnode/main.cpp is the thin driver that wraps `Node` with a real
// sleep/signal event loop; the deterministic behaviour is unit-tested against
// this header directly (drive N seals with explicit timestamps).
//
// M1 scope: seal EMPTY blocks at D=0 (no PoW, no signatures, no P2P). The flag
// surface is wired so the M1→M3 transition (`--seal-cadence 0 --difficulty 16`)
// needs no code change here.

#include <cstddef>
#include <cstdint>
#include <string>

#include "txchain/chain/chain.hpp"
#include "txchain/chain/reason.hpp"
#include "txchain/chain/store.hpp"
#include "txchain/chain/validate.hpp"

namespace txchain::node {

// Fully-resolved effective node configuration (Architecture Node §1.1). Built-in
// defaults are the frozen M1 launcher defaults (`--mine --seal-cadence 5000
// --seal-empty true`, except `mine` which the launcher passes explicitly).
struct NodeConfig {
  std::string datadir = "./txchain-data";
  bool mine = false;                    // only a --mine node seals (the M1 "M" role)
  std::uint64_t seal_cadence_ms = 5000;  // 0 disables the cadence timer (M3 PoW-driven)
  bool seal_empty = true;                // seal an empty block on an idle timer flush
  std::size_t max_txns_per_block = 8;    // MAX_TXNS_PER_BLOCK — early flush threshold
  unsigned difficulty = 0;               // M1 seals at D=0 (flag default 16, inert at M1)
  int node_index = 0;
  int port_base = 29000;
  bool no_rpc = false;
  bool no_p2p = false;
  std::uint64_t max_skew_s = 7200;
  std::string log_format = "json";
};

// Resolve the effective config with precedence: built-in default → <datadir>/node.json
// (or the --config file) → CLI flag (CLI wins). Pure over argv.
NodeConfig resolve_config(int argc, char** argv);

// The seal decision (pure; no state, no clock). Only a --mine node seals. Flush
// immediately once the mempool reaches the cap (MAX_TXNS), else on the cadence
// timer. seal_cadence_ms == 0 disables the timer entirely (the M3 config where
// sealing is PoW-driven rather than time-driven).
bool should_seal_now(const NodeConfig& cfg, std::uint64_t elapsed_ms, std::size_t mempool_size);

// Outcome of the boot self-validation gate. On !ok, `status` carries the same
// (failIndex, reason, detail) the `monitor verify` contract would print — the
// node refuses to start and exits non-zero.
struct BootResult {
  bool ok = false;
  chain::VerifyResult status;
};

// The node's in-memory state: the validated chain + its durable store. Boot runs
// the round-trip loader and replay-validates from genesis BEFORE any listener
// would bind; the sealer advances the tip and appends+fsyncs each block.
class Node {
 public:
  // Boot against cfg.datadir: create the datadir if absent; if chain.jsonl is
  // missing/empty, initialise it with the deterministic genesis block; else load
  // the existing chain and replay-validate from genesis. On a chain that fails
  // replay (or a non-canonical genesis), out.ok = false and the returned Node
  // must not be served. now_s feeds V3's skew check (injectable for tests).
  static Node boot(const NodeConfig& cfg, std::uint64_t now_s, BootResult& out);

  // Seal one empty block (M1): index = tip.index+1, prevHash = tipHash,
  // timestamp = max(now_s, tip.timestamp+1) for strict monotonicity, txnsHash =
  // SHA-256(""), nonce = 0. connectBlock validates + commits; on OK the block is
  // appended+fsync'd and the tip advances. Returns the first failing Reason else.
  chain::Reason seal_next_block(std::uint64_t now_s);

  std::uint64_t height() const { return chain_.height(); }
  chain::Hash256 tipHash() const { return chain_.tipHash(); }
  const NodeConfig& config() const { return cfg_; }
  const chain::ChainStore& store() const { return store_; }

 private:
  Node(NodeConfig cfg, chain::ChainStore store)
      : cfg_(std::move(cfg)), store_(std::move(store)) {}

  NodeConfig cfg_;
  chain::ChainStore store_;
  chain::Chain chain_;  // default-constructs to genesis (height 0)
};

}  // namespace txchain::node
