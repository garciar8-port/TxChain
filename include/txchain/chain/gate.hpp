#pragma once
// The per-transaction verification gate (Architecture Data Model §2.3, Chain/State
// §4/§5) — the SINGLE predicate wired into BOTH Chain::connectBlock (V7) and
// replayFromGenesis. Sharing one implementation is what guarantees the
// incremental commit path and the from-genesis ground-truth validator can never
// disagree — so mining and `monitor verify` always reach the same verdict
// (Chain/State §4.5). The gate is pure over (txn, state): no clock, no I/O.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include "txchain/chain/reason.hpp"
#include "txchain/chain/types.hpp"

namespace txchain::chain {

// Run the frozen verification order against `work` for one txn, then apply it.
//
// Normal txn (from != 0x00×20), in order (Data Model §2.3):
//   1. address_matches(from, pubkey)               else BAD_ADDR   (cheap gate first)
//   2. Ed25519 verify over the 89-byte payload     else BAD_SIG
//   3. nonce == work[from].nonce (next-expected)   else STALE_NONCE (below=spent, above=gap)
//   4. amount <= work[from].balance                else INSUFFICIENT_FUNDS
//   5. apply: work[from].balance -= amount; work[from].nonce += 1; work[to].balance += amount
//
// Coinbase (from == 0x00×20 AND txn_index == 0): exempt from addr/sig/nonce; a
// pure +COINBASE_REWARD credit to `to` with nonce == block_index, pubkey/sig all
// zero. Wrong amount ⇒ BAD_SUPPLY; malformed/nonce!=index ⇒ BAD_TXNS_HASH. A
// null-`from` txn anywhere but tx0 is a misplaced coinbase ⇒ BAD_TXNS_HASH.
//
// Returns Reason::OK (with `work` mutated) or the first failing Reason (with
// `work` left UNMUTATED — every check precedes any mutation). block_index and
// txn_index locate the txn for the coinbase rule; the caller maps them into the
// block-level failIndex/detail.
Reason applyTxn(std::map<Address, AccountState>& work, const Txn& t,
                std::uint64_t block_index, std::size_t txn_index) noexcept;

// Outcome of applying a whole block's txns (coinbase-aware). detail is the
// monitor-verify string on failure; txn_index locates the offending txn.
struct BlockApplyResult {
  Reason reason = Reason::OK;
  std::string detail;
  std::size_t txn_index = 0;
};

// Apply a block's txns to `work` in inclusion order via the shared per-txn gate,
// with the coinbase special-case: a coinbase (from == 0x00×20) must be tx0, the
// only one, well-formed (pubkey/sig zero, nonce == block index), and carry
// exactly COINBASE_REWARD — a pure +reward credit exempt from the addr/sig/nonce
// gates. `reward > 0` (M3) requires exactly one coinbase per non-genesis block;
// `reward == 0` (M1–M2) expects none. Shared by connectBlock (V7) and replay so
// they can never disagree. On OK `work` holds the post-block state.
BlockApplyResult applyBlockTxns(std::map<Address, AccountState>& work, const Block& b,
                                std::uint64_t reward);

}  // namespace txchain::chain
