#pragma once
// pow module — public umbrella header (txchain_core).
//
// Candidate block assembly (candidate.hpp) and, in later tickets, the nonce-search
// miner + cumulative-work fork choice. Depends on chain (coinbase shape, params)
// and mempool (admission-seq selection), bottom-up. The leading-zero PoW predicate
// itself lives in crypto (meets_difficulty / block_work).

#include "txchain/pow/candidate.hpp"
#include "txchain/pow/forkchoice.hpp"
#include "txchain/pow/miner.hpp"

namespace txchain::pow {

// Returns the module's name. Exists solely so the object is non-empty and the
// symbol can be linked from tests, proving the library boundary is wired.
const char* module_name() noexcept;

}  // namespace txchain::pow
