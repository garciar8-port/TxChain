#include "txchain/pow/forkchoice.hpp"

#include "txchain/crypto/hashutil.hpp"  // block_work

namespace txchain::pow {

Work blockWork(unsigned difficulty) { return crypto::block_work(difficulty); }

Work cumulativeWork(const std::vector<Block>& blocks, unsigned difficulty) {
  if (blocks.empty()) return 0;
  // Genesis (index 0) is PoW-exempt and contributes 0; every other block cost 2^D.
  return static_cast<Work>(blocks.size() - 1) * blockWork(difficulty);
}

bool candidateWins(Work candidateCumWork, Work activeCumWork) {
  return candidateCumWork > activeCumWork;  // strict; a tie keeps the incumbent
}

ImportResult evaluateImport(const std::vector<Block>& active, const std::vector<Block>& candidate,
                            std::uint64_t now_s, unsigned difficulty, std::uint64_t reward) {
  ImportResult r;
  // Validate the challenger from genesis FIRST — work never buys validity.
  r.status = chain::replayFromGenesis(candidate, now_s, difficulty, reward);
  r.valid = r.status.ok;
  r.active_work = cumulativeWork(active, difficulty);
  r.candidate_work = cumulativeWork(candidate, difficulty);
  r.won = r.valid && candidateWins(r.candidate_work, r.active_work);
  return r;
}

}  // namespace txchain::pow
