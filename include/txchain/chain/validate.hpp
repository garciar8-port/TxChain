#pragma once
// From-genesis replay validator + monitor verify (Architecture Chain/State §5–§7).
//
// replayFromGenesis is the GROUND-TRUTH arbiter of chain validity and the brief's
// mandated prerequisite for reorg: it reconstructs the entire account state from
// nothing (the genesis table only) and replays every block, depending on no
// cached state_. It is a pure function of the block list (V3's now() is the only
// non-pure input, injectable for deterministic tests). monitor verify is the
// user-facing driver that names the exact tampered block.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "txchain/chain/reason.hpp"
#include "txchain/chain/types.hpp"

namespace txchain::chain {

struct VerifyResult {
  bool ok = true;
  std::uint64_t failIndex = 0;  // valid iff !ok
  Reason reason = Reason::OK;
  std::string detail;
};

inline bool operator==(const VerifyResult& a, const VerifyResult& b) noexcept {
  return a.ok == b.ok && a.failIndex == b.failIndex && a.reason == b.reason &&
         a.detail == b.detail;
}

// Replay the whole chain from genesis and return pass or the first-failing
// (failIndex, reason, detail). The no-`now` form uses the wall clock (V3 skew).
VerifyResult replayFromGenesis(const std::vector<Block>& blocks);
VerifyResult replayFromGenesis(const std::vector<Block>& blocks, std::uint64_t now_s);

// The FROZEN monitor-verify output contract (§7.3):
//   OK   →  "OK <N> blocks"                          (N = block_count, genesis counted)
//   FAIL →  "FAIL block <idx>: <REASON> (<detail>)"
std::string monitor_verify_line(const VerifyResult& r, std::size_t block_count);

// Exit code: 0 on OK, non-zero on any FAIL — so CI / the launcher can gate on it.
int monitor_verify_exit_code(const VerifyResult& r) noexcept;

}  // namespace txchain::chain
