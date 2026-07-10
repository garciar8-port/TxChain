#pragma once
// Frozen block/validation parameters (Architecture Chain/State §4, Data Model
// §3). Single source shared by connectBlock, the mempool, and the replay
// validator — never a second hardcoded copy.

#include <cstddef>
#include <cstdint>

namespace txchain::chain {

// V3 bounded future skew: a block timestamp may be at most this far ahead of
// now() (config max_clock_skew_s). Default 2 hours.
inline constexpr std::uint64_t MAX_CLOCK_SKEW_S = 7200;

// V6 block-size cap: at most this many txns per block (Data Model §3 default).
inline constexpr std::size_t MAX_TXNS_PER_BLOCK = 8;

// Fixed coinbase reward (Architecture §Money supply / constants table): the tx0
// mint amount and the per-block term of the supply invariant
// (Σbalance == GENESIS_SUPPLY + height × COINBASE_REWARD). No halving/maturity/
// fees. Coinbase is *produced* in Pillar 3 (M3); in M1–M2 blocks carry none, but
// the gate's coinbase-exemption branch honors this constant so a future coinbase
// with the wrong amount is caught as BAD_SUPPLY.
inline constexpr std::uint64_t COINBASE_REWARD = 50;

// V4 Proof-of-Work target: a non-genesis block hash must have at least this many
// leading zero bits (Data Model §3.3, Mempool/PoW §4.1). The M3 default; the
// Chain/replay difficulty is configurable (0 keeps V4 a no-op for M1–M2 paths)
// and the miner (m3-miner-loop-commit) flips the node to this value. Final
// empirical calibration of the numeric default is m3-difficulty-calibration.
inline constexpr unsigned DIFFICULTY_BITS = 16;

}  // namespace txchain::chain
