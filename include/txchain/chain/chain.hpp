#pragma once
// The in-memory hash-linked chain (Architecture Chain/State §2). blocks_[0] is
// genesis; state_ is the authoritative committed account map (a cache of the
// replay result); cumWork_ is the 128-bit fork-choice accumulator; undoLog_ is
// declared for Pillar-4 reorg but unused in the MVP.

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

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

  // ---- writes ----
  // Declared stubs; the real validate-against-tip + commit lands in CRE-196 and
  // the from-genesis replay in CRE-197 (both return the Reason enum then). They
  // are intentionally not implemented here (M1 scope is types + genesis).
  bool connectBlock(const Block& b);  // TODO(CRE-196): structural validate + commit
  bool validateChain() const;         // TODO(CRE-197): from-genesis replay

private:
  std::vector<Block> blocks_;
  std::map<Address, AccountState> state_;
  Work cumWork_ = 0;
  std::vector<CommitDelta> undoLog_;
};

}  // namespace txchain::chain
