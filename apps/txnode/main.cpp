// txnode — the TxChain v2 node process (thin driver).
//
// Arg parsing, signal wiring, and the real-time sleep loop only; all ledger
// logic (boot self-validation, the seal decision, block construction + commit)
// lives in txchain_core's node module (txchain::node). This binary just:
//   resolve_config → Node::boot (refuse to start on a bad chain) → sleep loop
//   that calls should_seal_now / seal_next_block on the cadence timer.
// The deterministic behaviour is unit-tested against txchain::node directly, so
// nothing here needs to sleep to be verified.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <thread>

#include "txchain/chain/reason.hpp"
#include "txchain/chain/validate.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/node/node.hpp"

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int /*sig*/) { g_stop.store(true); }

std::uint64_t now_seconds() { return static_cast<std::uint64_t>(std::time(nullptr)); }

std::uint64_t now_millis() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace

int main(int argc, char** argv) {
  using namespace txchain;
  const node::NodeConfig cfg = node::resolve_config(argc, argv);

  // One JSON line with the fully-resolved effective config (Node §1.3).
  std::printf(
      "{\"event\":\"config_resolved\",\"datadir\":\"%s\",\"mine\":%s,"
      "\"seal_cadence_ms\":%llu,\"seal_empty\":%s,\"max_txns_per_block\":%llu,"
      "\"difficulty\":%u,\"no_rpc\":%s,\"no_p2p\":%s}\n",
      cfg.datadir.c_str(), cfg.mine ? "true" : "false",
      static_cast<unsigned long long>(cfg.seal_cadence_ms), cfg.seal_empty ? "true" : "false",
      static_cast<unsigned long long>(cfg.max_txns_per_block), cfg.difficulty,
      cfg.no_rpc ? "true" : "false", cfg.no_p2p ? "true" : "false");

  // Boot self-validation gate: replay-validate our own chain.jsonl BEFORE binding
  // any listener. A failing replay refuses to start with the monitor-verify reason.
  node::BootResult br;
  node::Node nd = node::Node::boot(cfg, now_seconds(), br);
  if (!br.ok) {
    std::printf(
        "{\"event\":\"node_boot_failed\",\"block\":%llu,\"reason\":\"%s\",\"detail\":\"%s\"}\n",
        static_cast<unsigned long long>(br.status.failIndex), chain::reasonName(br.status.reason),
        br.status.detail.c_str());
    return 1;  // refuse to start — no listener was bound
  }

  std::printf("{\"event\":\"node_up\",\"height\":%llu,\"tip\":\"%s\"}\n",
              static_cast<unsigned long long>(nd.height()),
              crypto::to_hex(nd.tipHash()).c_str());
  std::fflush(stdout);

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  // Event loop: poll on a short tick; seal when the cadence timer (or a full
  // mempool — none yet at M1) says so. sleep_for lives ONLY here, never in the
  // tested seal step.
  std::uint64_t last_seal_ms = now_millis();
  while (!g_stop.load()) {
    const std::size_t mempool_size = 0;  // M1: no mempool (txns arrive in M2)
    if (node::should_seal_now(cfg, now_millis() - last_seal_ms, mempool_size)) {
      const chain::Reason r = nd.seal_next_block(now_seconds());
      last_seal_ms = now_millis();
      if (r == chain::Reason::OK) {
        std::printf("{\"event\":\"block_sealed\",\"height\":%llu,\"tip\":\"%s\"}\n",
                    static_cast<unsigned long long>(nd.height()),
                    crypto::to_hex(nd.tipHash()).c_str());
        std::fflush(stdout);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Graceful shutdown: the last committed block is already fsync'd, so a restart
  // replay-validates and resumes with no data loss (Node §1.3 shutdown).
  std::printf("{\"event\":\"node_down\",\"height\":%llu}\n",
              static_cast<unsigned long long>(nd.height()));
  return 0;
}
