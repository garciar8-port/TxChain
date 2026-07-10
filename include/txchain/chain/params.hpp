#pragma once
// Frozen block/validation parameters (Architecture Mempool/PoW §4.1, Node/CLI
// §1.1) — the SINGLE SOURCE shared by connectBlock, the mempool, the replay
// validator, candidate assembly, and node/CLI config. Never re-literal these in
// another file. Difficulty is a *runtime constant, not a hashed field*, so the
// value below moves only the PoW accept/reject threshold — it never changes any
// golden vector or blockHash (CRE-209 format-invariance).

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
// leading zero bits (Data Model §3.3, Mempool/PoW §4.1). The Chain/replay
// difficulty is configurable (0 keeps V4 a no-op for the M1–M2 paths); this
// constant is the CALIBRATED default used by the demo (`txnode --difficulty
// DIFFICULTY_BITS`) and by `txchain monitor verify` / `import` (`--difficulty`
// overrides). Runtime-only — changing it never alters a hashed byte.
//
// CRE-209 (resolves Open Design Question #1). Calibrated to land a single-node
// mine in the visible-but-fast ~1–5 s window. Measured s/block (mine_bench,
// Release, Apple M3, 8 samples, coinbase-only): D=12 ~0.001, D=16 ~0.05,
// D=20 ~0.55, D=22 ~2.3 (1.3–3.7), D=24 ~3.9 (long tail to ~26). D=22 sits
// squarely in target with a tight spread; D=16 (old placeholder) is ~0.05 s
// (looks instant) and D=24's variance is too wide. Reproduce: `mine_bench 8 12
// 16 20 22 24`.
inline constexpr unsigned DIFFICULTY_BITS = 22;

}  // namespace txchain::chain
