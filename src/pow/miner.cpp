#include "txchain/pow/miner.hpp"

#include <limits>

#include "txchain/crypto/hashutil.hpp"  // meets_difficulty

namespace txchain::pow {

std::optional<Block> mine(Candidate cand, unsigned difficulty, std::atomic<bool>& stop) {
  std::uint64_t nonce = 0;
  while (true) {
    // Check pre-emption periodically (cheap; the hash dominates the loop cost).
    if ((nonce & 0x3FFFu) == 0 && stop.load(std::memory_order_relaxed)) return std::nullopt;

    cand.hdr.nonce = nonce;
    if (crypto::meets_difficulty(cand.hdr.hash(), difficulty)) {
      Block b;
      b.header = cand.hdr;
      b.txns = cand.txns;
      return b;
    }

    if (nonce == std::numeric_limits<std::uint64_t>::max()) {
      cand.hdr.timestamp += 1;  // nonce space exhausted — bump time and restart
      nonce = 0;
    } else {
      ++nonce;
    }
  }
}

}  // namespace txchain::pow
