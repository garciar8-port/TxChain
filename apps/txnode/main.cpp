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
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "txchain/chain/params.hpp"
#include "txchain/chain/reason.hpp"
#include "txchain/chain/types.hpp"
#include "txchain/chain/validate.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/mempool/mempool.hpp"
#include "txchain/node/node.hpp"
#include "txchain/pow.hpp"
#include "txchain/rpc.hpp"
#include "txchain/wallet/wallet.hpp"

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

  // Load (or create) the node's operating key. A malformed / bad-length wallet.key
  // refuses to start, same as a bad chain (Cryptography §10); a world-readable key
  // warns but loads (learning-artifact affordance).
  const wallet::LoadResult wr = wallet::load_or_create_wallet(cfg.datadir);
  if (!wr.ok) {
    std::printf("{\"event\":\"wallet_error\",\"detail\":\"%s\"}\n", wr.error.c_str());
    return 1;
  }
  if (wr.world_readable)
    std::printf("{\"event\":\"wallet_perms_warning\",\"detail\":\"wallet.key is group/other-accessible\"}\n");
  std::printf("{\"event\":\"wallet_ready\",\"address\":\"%s\"}\n",
              crypto::to_hex(wr.wallet.address).c_str());

  std::printf("{\"event\":\"node_up\",\"height\":%llu,\"tip\":\"%s\"}\n",
              static_cast<unsigned long long>(nd.height()),
              crypto::to_hex(nd.tipHash()).c_str());
  std::fflush(stdout);

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  // Shared-state lock — the M2 stand-in for the Pillar-5 shared_mutex. The RPC
  // thread and the seal loop serialize ALL access to the chain + mempool through
  // it (reads and the single mutation alike, until the shared_mutex lands).
  std::mutex node_mtx;
  mempool::Mempool mp([&nd](const chain::Address& a) { return nd.chain().account(a); });

  // HTTP RPC surface (POST /tx, GET /account/{addr}, GET /tip), bound to loopback.
  rpc::RpcContext rpc_ctx{
      [&nd](const chain::Address& a) { return nd.chain().account(a); },
      [&nd]() {
        const chain::Chain& c = nd.chain();
        const auto& blk = c.blockAt(c.height());
        rpc::TipInfo t;
        t.index = c.height();
        t.blockHash = blk.header.hash();
        t.prevHash = blk.header.prevHash;
        t.cumWork = c.cumWork();
        t.difficulty = c.difficulty();
        t.timestamp = blk.header.timestamp;
        return t;
      },
      mp};
  rpc::HttpServer rpc_server(
      node::rpc_port_for(cfg),
      [&](const std::string& m, const std::string& target, const std::string& body) {
        std::lock_guard<std::mutex> lk(node_mtx);  // consistent snapshot under the lock
        return rpc::handle_request(m, target, body, rpc_ctx);
      });
  std::thread rpc_thread;
  if (!cfg.no_rpc) {
    if (rpc_server.start()) {
      std::printf("{\"event\":\"rpc_up\",\"port\":%d}\n", rpc_server.port());
      std::fflush(stdout);
      rpc_thread = std::thread([&rpc_server]() { rpc_server.run(); });
    } else {
      std::printf("{\"event\":\"rpc_error\",\"detail\":\"cannot bind rpc port\"}\n");
    }
  }

  // Event loop. Two mining modes on the SAME binary, chosen by flags:
  //   PoW (M3):  --mine --difficulty D>0 --seal-cadence 0  → grind a nonce until
  //              the hash meets D; block production costs ~2^D hashes.
  //   cadence (M1/M2): --mine --seal-cadence T>0 (D=0)     → timer-driven sealing.
  const chain::Address miner_addr = wr.wallet.address;  // coinbase pays the node's wallet
  const bool pow_mode = cfg.mine && cfg.difficulty > 0;

  std::uint64_t last_seal_ms = now_millis();
  while (!g_stop.load()) {
    if (pow_mode) {
      // Snapshot the tip + mempool under the lock, grind LOCK-FREE, then commit
      // under the lock only if the tip has not moved ("tip moved under me").
      pow::Candidate cand;
      {
        std::lock_guard<std::mutex> lk(node_mtx);
        const auto& tip_blk = nd.chain().blockAt(nd.chain().height());
        cand = pow::buildCandidate(
            miner_addr, nd.height(), nd.tipHash(), tip_blk.header.timestamp, now_seconds(), mp,
            [&nd](const chain::Address& a) { return nd.chain().account(a); });
      }
      const auto mined = pow::mine(cand, cfg.difficulty, g_stop);
      if (!mined) continue;  // pre-empted (shutdown / tip advanced)
      {
        std::lock_guard<std::mutex> lk(node_mtx);
        if (nd.height() + 1 == mined->header.index && nd.tipHash() == mined->header.prevHash &&
            nd.commitBlock(*mined, now_seconds()) == chain::Reason::OK) {
          if (mined->txns.size() > 1)  // evict the included transfers (tx0 is the coinbase)
            mp.evictIncluded(
                std::vector<chain::Txn>(mined->txns.begin() + 1, mined->txns.end()));
          std::printf("{\"event\":\"block_mined\",\"height\":%llu,\"tip\":\"%s\",\"txns\":%zu}\n",
                      static_cast<unsigned long long>(nd.height()),
                      crypto::to_hex(nd.tipHash()).c_str(), mined->txns.size());
          std::fflush(stdout);
        }
      }
      continue;
    }

    if (node::should_seal_now(cfg, now_millis() - last_seal_ms, /*mempool_size=*/0)) {
      chain::Reason r;
      std::vector<chain::Txn> batch;
      {
        std::lock_guard<std::mutex> lk(node_mtx);
        // Drain up to a block's worth of admitted txns (ascending admission order).
        for (const auto* e : mp.sortedBySeq()) {
          batch.push_back(e->txn);
          if (batch.size() >= chain::MAX_TXNS_PER_BLOCK) break;
        }
        r = nd.seal_next_block(now_seconds(), batch);
        if (r == chain::Reason::OK && !batch.empty()) mp.evictIncluded(batch);
      }
      last_seal_ms = now_millis();
      if (r == chain::Reason::OK) {
        std::printf("{\"event\":\"block_sealed\",\"height\":%llu,\"tip\":\"%s\",\"txns\":%zu}\n",
                    static_cast<unsigned long long>(nd.height()),
                    crypto::to_hex(nd.tipHash()).c_str(), batch.size());
        std::fflush(stdout);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  rpc_server.stop();
  if (rpc_thread.joinable()) rpc_thread.join();

  // Graceful shutdown: the last committed block is already fsync'd, so a restart
  // replay-validates and resumes with no data loss (Node §1.3 shutdown).
  std::printf("{\"event\":\"node_down\",\"height\":%llu}\n",
              static_cast<unsigned long long>(nd.height()));
  return 0;
}
