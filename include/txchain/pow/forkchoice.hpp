#pragma once
// Cumulative-work fork choice (Architecture Mempool/PoW §5). The metric that makes
// two histories comparable: Σ 2^D over the non-genesis blocks. A challenger chain
// replaces the incumbent iff it is STRICTLY heavier (ties keep the incumbent —
// first-seen wins, so tips never flap and the import demo is deterministic).
//
// M3 is single-process with NO in-flight reorg: the only fork-choice triggers are
// T1 (a local mine extends the tip, always heavier by 2^D — the commitBlock fast
// path) and T2 (`txchain import` — validate a competing chain.jsonl from genesis,
// and if heavier, whole-chain swap + full replay). Work never buys validity: an
// imported chain is eligible only after replayFromGenesis passes end-to-end.

#include <cstdint>
#include <vector>

#include "txchain/chain/types.hpp"       // Work, Block
#include "txchain/chain/validate.hpp"    // VerifyResult

namespace txchain::pow {

using chain::Block;
using chain::VerifyResult;
using chain::Work;

// 2^D — the per-block work at difficulty D (delegates to crypto::block_work).
Work blockWork(unsigned difficulty);

// Σ 2^D over the non-genesis blocks of a fixed-difficulty chain (genesis = 0 work).
Work cumulativeWork(const std::vector<Block>& blocks, unsigned difficulty);

// Strict fork choice: replace the incumbent iff the candidate is strictly heavier.
bool candidateWins(Work candidateCumWork, Work activeCumWork);

// The verdict of evaluating a candidate chain against the active one (T2 import).
struct ImportResult {
  bool valid = false;         // candidate passed replayFromGenesis end-to-end
  bool won = false;           // valid AND strictly heavier than active
  VerifyResult status;        // replay verdict (reason + failIndex on !valid)
  Work active_work = 0;
  Work candidate_work = 0;
};

// Validate the candidate from genesis (at D/reward), then compare cumulative work.
// Never mutates anything — the caller performs the swap iff `won`. `active` is the
// current chain's blocks (assumed already valid); its cumWork is computed at D too.
ImportResult evaluateImport(const std::vector<Block>& active, const std::vector<Block>& candidate,
                            std::uint64_t now_s, unsigned difficulty, std::uint64_t reward);

}  // namespace txchain::pow
