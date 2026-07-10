#pragma once
// The nonce search — the "cost" in Proof-of-Work (Architecture Mempool/PoW §4.3).
// mine() grinds SHA-256 over the 88-byte header, incrementing the nonce, until the
// block hash meets difficulty D. It runs LOCK-FREE on the snapshot from
// buildCandidate (readers are never blocked during the grind, Concurrency §4), and
// is pre-emptible via a stop flag (tip advanced / shutdown) so the caller can
// re-snapshot against a new tip. Uses the same crypto::meets_difficulty predicate
// validateBlock and monitor verify use — one function, three call sites.

#include <atomic>
#include <cstdint>
#include <optional>

#include "txchain/chain/types.hpp"
#include "txchain/pow/candidate.hpp"

namespace txchain::pow {

using chain::Block;

// Search cand.hdr.nonce = 0,1,2,… until meets_difficulty(hash, difficulty). Returns
// the fully-mined Block, or nullopt if `stop` is observed true before a solution
// (checked periodically). On nonce-space exhaustion the timestamp is bumped and the
// search restarts (never reached at D=16, where a solution is ~2^16 attempts).
std::optional<Block> mine(Candidate cand, unsigned difficulty, std::atomic<bool>& stop);

}  // namespace txchain::pow
