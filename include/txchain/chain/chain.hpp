#pragma once
// The in-memory hash-linked chain (Architecture Chain/State §2). blocks_[0] is
// genesis; state_ is the authoritative committed account map (a cache of the
// replay result); cumWork_ is the 128-bit fork-choice accumulator; undoLog_ is
// declared for Pillar-4 reorg but unused in the MVP.

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "txchain/chain/params.hpp"
#include "txchain/chain/reason.hpp"
#include "txchain/chain/types.hpp"

namespace txchain::chain {

// Per-committed-block undo record for reorg rollback (Pillar 4; unused in M1).
struct CommitDelta {
  std::vector<std::pair<Address, AccountState>> touched;
};

class Chain {
public:
  Chain();  // initializes to genesis (height 0) + applyGenesis(state_)

  // ---- reads (shared lock in Pillar 5) ----
  std::uint64_t height() const;                   // blocks_.size() - 1
  std::uint64_t numBlocks() const;                // blocks_.size()
  Hash256 tipHash() const;                        // tip header hash
  Work cumWork() const;                           // Σ per-block work
  AccountState account(const Address& a) const;   // {0,0} if absent
  const Block& blockAt(std::uint64_t idx) const;  // throws std::out_of_range if oob

  // ---- writes (exclusive lock in Pillar 5) ----
  // The single write path: validate-against-tip (V1–V7) then atomic commit.
  // Returns Reason::OK on success, else the first failing reason (state
  // unchanged). now_s is injectable for deterministic tests; the no-arg form
  // uses the wall clock (V3 skew check).
  Reason connectBlock(const Block& b);
  Reason connectBlock(const Block& b, std::uint64_t now_s);

  // From-genesis replay, the ground-truth validator. TODO(CRE-197): real impl;
  // currently a stub returning OK.
  Reason validateChain() const;

private:
  std::vector<Block> blocks_;
  std::map<Address, AccountState> state_;
  Work cumWork_ = 0;
  unsigned difficulty_ = 0;  // M1 runs at D=0; Pillar 3 (M3) flips this to 16
  std::vector<CommitDelta> undoLog_;
};

}  // namespace txchain::chain
