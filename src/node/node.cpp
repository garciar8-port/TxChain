#include "txchain/node/node.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

#include <picojson.h>  // vendored (SYSTEM include on txchain_core), INT64 mode

#include "txchain/chain/genesis.hpp"

namespace txchain::node {

namespace fs = std::filesystem;

using chain::Block;
using chain::BlockHeader;
using chain::ChainStore;
using chain::Txn;
using chain::genesisBlock;
using chain::LoadResult;
using chain::Reason;
using chain::replayFromGenesis;
using chain::VerifyResult;

int rpc_port_for(const NodeConfig& cfg) {
  if (cfg.rpc_port != 0) return cfg.rpc_port;  // --rpc-port override
  return cfg.port_base + kRpcPortOffset + cfg.node_index;
}

bool should_seal_now(const NodeConfig& cfg, std::uint64_t elapsed_ms, std::size_t mempool_size) {
  if (!cfg.mine) return false;                             // only the "M" role seals
  if (mempool_size >= cfg.max_txns_per_block) return true;  // early flush at MAX_TXNS
  if (cfg.seal_cadence_ms == 0) return false;               // cadence timer disabled (M3)
  return elapsed_ms >= cfg.seal_cadence_ms;                 // timer elapsed
}

namespace {

// Overlay any recognised keys from a node.json / --config object onto cfg. Absent
// or wrong-typed keys are left at their current value (the built-in default).
void apply_config_file(const std::string& path, NodeConfig& cfg) {
  std::ifstream in(path);
  if (!in) return;
  const std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
  picojson::value v;
  const std::string err = picojson::parse(v, content);
  if (!err.empty() || !v.is<picojson::object>()) return;
  const auto& o = v.get<picojson::object>();

  auto get_bool = [&](const char* k, bool& out) {
    const auto it = o.find(k);
    if (it != o.end() && it->second.is<bool>()) out = it->second.get<bool>();
  };
  auto get_u64 = [&](const char* k, std::uint64_t& out) {
    const auto it = o.find(k);
    if (it == o.end()) return;
    if (it->second.is<std::int64_t>() && it->second.get<std::int64_t>() >= 0)
      out = static_cast<std::uint64_t>(it->second.get<std::int64_t>());
    else if (it->second.is<double>() && it->second.get<double>() >= 0)
      out = static_cast<std::uint64_t>(it->second.get<double>());
  };
  auto get_str = [&](const char* k, std::string& out) {
    const auto it = o.find(k);
    if (it != o.end() && it->second.is<std::string>()) out = it->second.get<std::string>();
  };

  get_str("datadir", cfg.datadir);
  get_bool("mine", cfg.mine);
  get_bool("seal_empty", cfg.seal_empty);
  get_bool("no_rpc", cfg.no_rpc);
  get_bool("no_p2p", cfg.no_p2p);
  get_u64("seal_cadence_ms", cfg.seal_cadence_ms);
  get_u64("max_skew_s", cfg.max_skew_s);
  std::uint64_t tmp = cfg.max_txns_per_block;
  get_u64("max_txns_per_block", tmp);
  cfg.max_txns_per_block = static_cast<std::size_t>(tmp);
  tmp = cfg.difficulty;
  get_u64("difficulty", tmp);
  cfg.difficulty = static_cast<unsigned>(tmp);
  tmp = static_cast<std::uint64_t>(cfg.node_index);
  get_u64("node_index", tmp);
  cfg.node_index = static_cast<int>(tmp);
  tmp = static_cast<std::uint64_t>(cfg.port_base);
  get_u64("port_base", tmp);
  cfg.port_base = static_cast<int>(tmp);
  get_str("log_format", cfg.log_format);
}

bool truthy(const char* v) {
  return std::strcmp(v, "true") == 0 || std::strcmp(v, "1") == 0;
}

}  // namespace

NodeConfig resolve_config(int argc, char** argv) {
  NodeConfig cfg;

  // Phase 1: pre-scan CLI for --datadir / --config so the config file is read
  // from the right place before CLI flags are applied.
  std::string config_file;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--datadir") == 0) cfg.datadir = argv[i + 1];
    else if (std::strcmp(argv[i], "--config") == 0) config_file = argv[i + 1];
  }

  // Phase 2: config file overlays the built-in defaults.
  apply_config_file(config_file.empty() ? (cfg.datadir + "/node.json") : config_file, cfg);

  // Phase 3: CLI flags win over both.
  for (int i = 1; i < argc; ++i) {
    const auto is = [&](const char* f) { return std::strcmp(argv[i], f) == 0; };
    const auto val = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
    if (is("--datadir")) cfg.datadir = val();
    else if (is("--config")) (void)val();  // already consumed in phase 1/2
    else if (is("--mine")) cfg.mine = true;
    else if (is("--no-rpc")) cfg.no_rpc = true;
    else if (is("--no-p2p")) cfg.no_p2p = true;
    else if (is("--seal-cadence")) cfg.seal_cadence_ms = std::strtoull(val(), nullptr, 10);
    else if (is("--seal-empty")) cfg.seal_empty = truthy(val());
    else if (is("--max-txns-per-block"))
      cfg.max_txns_per_block = static_cast<std::size_t>(std::strtoull(val(), nullptr, 10));
    else if (is("--difficulty"))
      cfg.difficulty = static_cast<unsigned>(std::strtoul(val(), nullptr, 10));
    else if (is("--node-index")) cfg.node_index = static_cast<int>(std::strtol(val(), nullptr, 10));
    else if (is("--port-base")) cfg.port_base = static_cast<int>(std::strtol(val(), nullptr, 10));
    else if (is("--rpc-port")) cfg.rpc_port = static_cast<int>(std::strtol(val(), nullptr, 10));
    else if (is("--max-skew")) cfg.max_skew_s = std::strtoull(val(), nullptr, 10);
    else if (is("--log-format")) cfg.log_format = val();
    // Unknown flags are ignored at M1 (M4 wires --peers etc.).
  }
  return cfg;
}

Node Node::boot(const NodeConfig& cfg, std::uint64_t now_s, BootResult& out) {
  std::error_code ec;
  fs::create_directories(cfg.datadir, ec);  // ensure_datadir (no-op if present)

  ChainStore store = ChainStore::forDatadir(cfg.datadir);
  Node node(cfg, store);  // chain_ starts at the canonical genesis (height 0)

  // Fresh datadir (no file, or a zero-byte file): initialise with genesis.
  const bool has_blocks = fs::exists(store.path(), ec) && fs::file_size(store.path(), ec) > 0;
  if (!has_blocks) {
    store.appendBlock(genesisBlock());
    out.ok = true;
    return node;
  }

  // Existing chain: round-trip load, then replay-validate from genesis BEFORE we
  // would bind any listener. A parse failure or a failing replay refuses boot.
  const LoadResult lr = store.load();
  if (!lr.status.ok) {
    out.ok = false;
    out.status = lr.status;
    return node;
  }
  if (lr.blocks.empty()) {  // file existed but held no blocks
    store.appendBlock(genesisBlock());
    out.ok = true;
    return node;
  }
  // The node's genesis is the vendored deterministic block; a different on-disk
  // genesis is not this node's chain.
  if (!(lr.blocks[0] == genesisBlock())) {
    out.ok = false;
    out.status = VerifyResult{false, 0, Reason::BAD_LINK, "genesis mismatch"};
    return node;
  }
  const VerifyResult vr = replayFromGenesis(lr.blocks, now_s);
  if (!vr.ok) {
    out.ok = false;
    out.status = vr;
    return node;
  }
  // Replay passed → rebuild the committed in-memory chain through the single
  // write path so state_/cumWork_/tip reflect the loaded blocks.
  for (std::size_t i = 1; i < lr.blocks.size(); ++i) {
    const Reason r = node.chain_.connectBlock(lr.blocks[i], now_s);
    if (r != Reason::OK) {  // belt-and-suspenders: replay already agreed this is valid
      out.ok = false;
      out.status = VerifyResult{false, static_cast<std::uint64_t>(i), r, "reconnect failed"};
      return node;
    }
  }
  out.ok = true;
  return node;
}

Reason Node::seal_next_block(std::uint64_t now_s, const std::vector<Txn>& txns) {
  const BlockHeader tip = chain_.blockAt(chain_.height()).header;

  Block b;
  b.header.index = chain_.height() + 1;
  b.header.timestamp = std::max<std::uint64_t>(now_s, tip.timestamp + 1);  // strict monotonic
  b.header.prevHash = chain_.tipHash();
  b.txns = txns;  // empty ⇒ SHA-256("") (M1 cadence); non-empty ⇒ M2 mempool drain
  b.header.txnsHash = b.computeTxnsHash();
  b.header.nonce = 0;  // no PoW at M1–M2 (D=0)

  const Reason r = chain_.connectBlock(b, now_s);
  if (r != Reason::OK) return r;

  // Durable append + fsync OUTSIDE the (no-op at M1) lock (Concurrency §6).
  store_.appendBlock(b);
  return Reason::OK;
}

}  // namespace txchain::node
