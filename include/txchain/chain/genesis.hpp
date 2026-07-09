#pragma once
// Deterministic genesis allocation (Architecture Chain/State §3). Genesis is a
// fresh, reproducible block-0 shipped with the binary — the SOLE source of
// initial money supply. There is NO per-account 1000; every address not in the
// table starts {0,0}.
//
// Reproducible seed rule (FROZEN): each demo identity's Ed25519 seed is
//   seed = SHA-256("txchain-genesis-" || name)
// the pubkey is the standard Ed25519 public key for that seed, and the address
// is SHA-256(pubkey)[:20]. So the table is regenerable from names alone, with no
// committed secret material (scripts/gen_genesis.py re-derives it; genesis_test
// asserts it byte-for-byte).

#include <cstddef>
#include <cstdint>
#include <map>

#include "txchain/chain/types.hpp"

namespace txchain::chain {

struct GenesisEntry {
  Address addr;
  std::uint64_t amount;
};

inline constexpr std::size_t GENESIS_COUNT = 5;
inline constexpr std::uint64_t GENESIS_SUPPLY = 5000;      // Σ amounts
inline constexpr std::uint64_t GENESIS_TIMESTAMP = 1704067200;  // 2024-01-01Z, frozen

// Order-fixed allocation table. Identities: racheal, oliver, matthew, sophia,
// daniel — each 1000. Addresses derived per the frozen seed rule above
// (regenerate with scripts/gen_genesis.py).
inline constexpr GenesisEntry GENESIS_ALLOC[GENESIS_COUNT] = {
    {/* racheal */ Address{{0x62, 0x5c, 0x36, 0xc3, 0x4a, 0x65, 0x6b, 0xf2, 0x5a, 0x53,
                            0x8f, 0x8c, 0x23, 0xa1, 0x31, 0x86, 0x98, 0x5a, 0xf6, 0x69}}, 1000},
    {/* oliver  */ Address{{0x70, 0xdc, 0xb6, 0xdd, 0xa5, 0xae, 0xae, 0x7b, 0xfc, 0x6f,
                            0x08, 0xc5, 0x90, 0x40, 0x2f, 0x6c, 0x0c, 0x78, 0x97, 0x02}}, 1000},
    {/* matthew */ Address{{0x25, 0xb3, 0xab, 0x87, 0x8b, 0x41, 0x2c, 0x08, 0x67, 0x9b,
                            0x9d, 0x2f, 0xd0, 0xa3, 0x0b, 0xc5, 0x28, 0xdc, 0x07, 0x0d}}, 1000},
    {/* sophia  */ Address{{0x32, 0x62, 0xbd, 0x9c, 0x6c, 0xdb, 0xe7, 0x68, 0x05, 0x05,
                            0x8f, 0x64, 0x5f, 0x74, 0x1b, 0x8f, 0x15, 0xec, 0x14, 0xfa}}, 1000},
    {/* daniel  */ Address{{0x99, 0x3b, 0x59, 0x10, 0xd2, 0xfa, 0x78, 0x40, 0x01, 0x75,
                            0x98, 0x37, 0x91, 0xf4, 0x53, 0x5d, 0x16, 0x9e, 0xb7, 0xe3}}, 1000},
};

// Compile-time supply cross-check: GENESIS_SUPPLY must equal Σ amounts.
constexpr std::uint64_t genesis_supply_sum() {
  std::uint64_t s = 0;
  for (std::size_t i = 0; i < GENESIS_COUNT; ++i) s += GENESIS_ALLOC[i].amount;
  return s;
}
static_assert(genesis_supply_sum() == GENESIS_SUPPLY, "GENESIS_SUPPLY must equal Σ GENESIS_ALLOC amounts");

// Sole initializer of committed state at height 0: state.clear() then set each
// genesis address to {balance = amount, nonce = 0}.
void applyGenesis(std::map<Address, AccountState>& state);

// The deterministic genesis block-0: index 0, prevHash all-zero, txnsHash =
// SHA-256("") (no txns), nonce 0 (PoW-exempt).
Block genesisBlock();

}  // namespace txchain::chain
