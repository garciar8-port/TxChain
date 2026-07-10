#include "txchain/chain/validate.hpp"

#include <cstddef>
#include <ctime>
#include <map>
#include <string>
#include <utility>

#include "txchain/chain/gate.hpp"       // applyTxn — the shared per-txn verify gate
#include "txchain/chain/genesis.hpp"
#include "txchain/chain/params.hpp"
#include "txchain/crypto/hashutil.hpp"  // meets_difficulty

namespace txchain::chain {

const char* reasonName(Reason r) noexcept {
  switch (r) {
    case Reason::OK: return "OK";
    case Reason::BAD_LINK: return "BAD_LINK";
    case Reason::BAD_TXNS_HASH: return "BAD_TXNS_HASH";
    case Reason::BAD_POW: return "BAD_POW";
    case Reason::BAD_SIG: return "BAD_SIG";
    case Reason::BAD_ADDR: return "BAD_ADDR";
    case Reason::STALE_NONCE: return "STALE_NONCE";
    case Reason::INSUFFICIENT_FUNDS: return "INSUFFICIENT_FUNDS";
    case Reason::BAD_SUPPLY: return "BAD_SUPPLY";
  }
  return "UNKNOWN";  // unreachable; keeps -Wreturn-type happy
}

namespace {

std::uint64_t wall_clock_now_s() { return static_cast<std::uint64_t>(std::time(nullptr)); }

VerifyResult fail(std::uint64_t idx, Reason r, std::string detail) {
  return VerifyResult{false, idx, r, std::move(detail)};
}

}  // namespace

VerifyResult replayFromGenesis(const std::vector<Block>& blocks) {
  return replayFromGenesis(blocks, wall_clock_now_s(), 0, 0);
}

VerifyResult replayFromGenesis(const std::vector<Block>& blocks, std::uint64_t now_s) {
  return replayFromGenesis(blocks, now_s, 0, 0);
}

// Ground-truth from-genesis replay with the V4 PoW target `D` (required leading
// zero bits; 0 ⇒ V4 is a no-op) and the per-block coinbase `reward` (0 ⇒ no
// coinbase; COINBASE_REWARD ⇒ M3 requires exactly one coinbase per non-genesis
// block). Genesis (index 0) is PoW-exempt and carries no coinbase.
VerifyResult replayFromGenesis(const std::vector<Block>& blocks, std::uint64_t now_s,
                               unsigned D, std::uint64_t reward) {
  if (blocks.empty()) return fail(0, Reason::BAD_LINK, "empty chain");

  std::map<Address, AccountState> state;
  applyGenesis(state);  // the ONLY source of initial supply

  // ---- Genesis (index 0) ----
  const Block& b0 = blocks[0];
  if (b0.header.index != 0) return fail(0, Reason::BAD_LINK, "index mismatch");
  const Hash256 zero{};
  if (b0.header.prevHash != zero) return fail(0, Reason::BAD_LINK, "prevHash mismatch");
  if (b0.computeTxnsHash() != b0.header.txnsHash)
    return fail(0, Reason::BAD_TXNS_HASH, "txns commitment mismatch");
  // Genesis is PoW-exempt and carries no signed txns.
  Hash256 prevHash = b0.header.hash();
  std::uint64_t prevTime = b0.header.timestamp;

  // ---- Blocks 1..height ----
  for (std::size_t i = 1; i < blocks.size(); ++i) {
    const std::uint64_t idx = static_cast<std::uint64_t>(i);
    const Block& b = blocks[i];

    if (b.header.index != idx) return fail(idx, Reason::BAD_LINK, "index mismatch");
    if (b.header.prevHash != prevHash) return fail(idx, Reason::BAD_LINK, "prevHash mismatch");
    if (!(b.header.timestamp > prevTime && b.header.timestamp <= now_s + MAX_CLOCK_SKEW_S))
      return fail(idx, Reason::BAD_LINK, "timestamp out of range");
    if (!crypto::meets_difficulty(b.header.hash(), D))  // V4 — genesis exempt (loop starts at 1)
      return fail(idx, Reason::BAD_POW, "hash above target");
    if (b.computeTxnsHash() != b.header.txnsHash)
      return fail(idx, Reason::BAD_TXNS_HASH, "txns commitment mismatch");
    if (b.txns.size() > MAX_TXNS_PER_BLOCK)
      return fail(idx, Reason::BAD_TXNS_HASH, "too many txns");

    // Per-txn gate + coinbase via the SAME shared predicate (applyBlockTxns) that
    // connectBlock uses, in block-inclusion order — so the ground-truth replay and
    // the incremental commit can never disagree on a reason (Chain/State §4.5).
    const BlockApplyResult ar = applyBlockTxns(state, b, reward);
    if (ar.reason != Reason::OK) return fail(idx, ar.reason, ar.detail);

    prevHash = b.header.hash();
    prevTime = b.header.timestamp;
  }

  // ---- Global supply invariant (§6, closed form — not an accumulator) ----
  std::uint64_t actual = 0;
  for (const auto& kv : state) actual += kv.second.balance;
  const std::uint64_t height = blocks.size() - 1;
  const std::uint64_t expected = GENESIS_SUPPLY + height * reward;  // + Σ coinbase mints
  if (actual != expected) return fail(height, Reason::BAD_SUPPLY, "supply mismatch");

  return VerifyResult{true, 0, Reason::OK, ""};
}

std::map<Address, AccountState> replayState(const std::vector<Block>& blocks) {
  std::map<Address, AccountState> state;
  applyGenesis(state);
  for (std::size_t i = 1; i < blocks.size(); ++i)
    for (std::size_t ti = 0; ti < blocks[i].txns.size(); ++ti)
      (void)applyTxn(state, blocks[i].txns[ti], blocks[i].header.index, ti);
  return state;
}

std::string monitor_verify_line(const VerifyResult& r, std::size_t block_count) {
  if (r.ok) return "OK " + std::to_string(block_count) + " blocks";
  std::string s = "FAIL block " + std::to_string(r.failIndex) + ": " + reasonName(r.reason);
  if (!r.detail.empty()) s += " (" + r.detail + ")";
  return s;
}

int monitor_verify_exit_code(const VerifyResult& r) noexcept { return r.ok ? 0 : 1; }

}  // namespace txchain::chain
