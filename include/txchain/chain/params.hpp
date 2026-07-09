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

}  // namespace txchain::chain
