#pragma once
// Candidate block assembly (Architecture Mempool/PoW §4.2). buildCandidate turns
// the current tip + the mempool into an UNMINED block ready for the nonce search:
// coinbase as tx0, then up to MAX_TXNS_PER_BLOCK-1 mempool txns in ascending
// admission-sequence order, each re-validated against a lazily-materialized
// shadow of the balance index (a txn admitted OK earlier is dropped here if the
// tip has since advanced — "admission balance is provisional", Mempool §3).
//
// The function is PURE over its inputs (no lock, no clock): the caller snapshots
// the tip/accounts under the shared lock and passes them in, then runs the long
// nonce search (m3-miner-loop-commit) lock-free. nonce is 0 (the search start).

#include <cstdint>
#include <functional>
#include <vector>

#include "txchain/chain/types.hpp"
#include "txchain/mempool/mempool.hpp"

namespace txchain::pow {

using chain::AccountState;
using chain::Address;
using chain::Hash256;
using serialize::BlockHeader;
using serialize::Txn;

// A committed-tip account lookup (absent ⇒ {0,0}); the shadow copies from it.
using AccountLookup = std::function<AccountState(const Address&)>;

struct Candidate {
  BlockHeader hdr;           // index/timestamp/prevHash/txnsHash set; nonce = 0 (unmined)
  std::vector<Txn> txns;     // tx0 = coinbase, then selected transfers in inclusion order
};

// Build an unmined candidate atop the given tip. `tip_index`/`tip_prev...` come
// from the caller's snapshot: tip_index is the tip's height, tip_hash is the
// recomputed tip blockHash (the new block's prevHash), tip_timestamp bounds the
// strictly-increasing timestamp. Selects coinbase-first, ≤ MAX_TXNS_PER_BLOCK.
Candidate buildCandidate(const Address& minerAddr, std::uint64_t tip_index,
                         const Hash256& tip_hash, std::uint64_t tip_timestamp,
                         std::uint64_t now_s, const mempool::Mempool& mp,
                         const AccountLookup& accountAt);

}  // namespace txchain::pow
