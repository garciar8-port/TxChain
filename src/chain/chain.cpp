#include "txchain/chain/chain.hpp"

#include "txchain/chain.hpp"        // umbrella (module_name)
#include "txchain/chain/genesis.hpp"
#include "txchain/chain/validate.hpp"

#include <ctime>
#include <map>
#include <utility>
#include <vector>

#include "txchain/crypto/hashutil.hpp"  // meets_difficulty, block_work

namespace txchain::chain {

namespace {
std::uint64_t wall_clock_now_s() { return static_cast<std::uint64_t>(std::time(nullptr)); }
}  // namespace

const char* module_name() noexcept { return "chain"; }

Chain::Chain() {
  blocks_.push_back(genesisBlock());
  applyGenesis(state_);
  cumWork_ = 0;  // genesis is not mined
}

std::uint64_t Chain::height() const { return blocks_.size() - 1; }

std::uint64_t Chain::numBlocks() const { return blocks_.size(); }

Hash256 Chain::tipHash() const { return blocks_.back().header.hash(); }

Work Chain::cumWork() const { return cumWork_; }

AccountState Chain::account(const Address& a) const {
  const auto it = state_.find(a);
  return it == state_.end() ? AccountState{} : it->second;
}

const Block& Chain::blockAt(std::uint64_t idx) const { return blocks_.at(idx); }

Reason Chain::connectBlock(const Block& b) { return connectBlock(b, wall_clock_now_s()); }

Reason Chain::connectBlock(const Block& b, std::uint64_t now_s) {
  const BlockHeader& tip = blocks_.back().header;

  // V1 — index links to the current tip (genesis, block 0, is seeded separately).
  if (b.header.index != height() + 1) return Reason::BAD_LINK;

  // V2 — prevHash links to the tip (recomputed; a stored hash is never trusted).
  if (b.header.prevHash != tipHash()) return Reason::BAD_LINK;

  // V3 — timestamp strictly increasing and within the bounded future skew.
  if (b.header.timestamp <= tip.timestamp) return Reason::BAD_LINK;
  if (b.header.timestamp > now_s + MAX_CLOCK_SKEW_S) return Reason::BAD_LINK;

  // V4 — PoW. A no-op at M1 (difficulty_ == 0 ⇒ meets_difficulty always true);
  // Pillar 3 flips difficulty_ to 16 with no structural change. NOT stubbed
  // behind a crypto #ifdef — the real predicate runs in every build.
  if (!crypto::meets_difficulty(b.header.hash(), difficulty_)) return Reason::BAD_POW;

  // V5 — txnsHash commitment recomputed over the ordered canonical txns.
  if (b.computeTxnsHash() != b.header.txnsHash) return Reason::BAD_TXNS_HASH;

  // V6 — block-size cap.
  if (b.txns.size() > MAX_TXNS_PER_BLOCK) return Reason::BAD_TXNS_HASH;

  // V7 — per-txn gate + commit against a scratch copy. The address→sig→nonce
  // checks (steps 1–4) are inert at M1 (activated in M2); the balance commit and
  // its defensive underflow check are wired now so later pillars just turn on
  // their steps. All mutation happens on `work`, never state_, until commit.
  std::map<Address, AccountState> work = state_;
  std::vector<std::pair<Address, AccountState>> inverse;
  for (const auto& t : b.txns) {
    const AccountState from_before = work.count(t.from) ? work[t.from] : AccountState{};
    const AccountState to_before = work.count(t.to) ? work[t.to] : AccountState{};
    if (from_before.balance < t.amount) return Reason::INSUFFICIENT_FUNDS;  // discards work

    inverse.push_back({t.from, from_before});
    inverse.push_back({t.to, to_before});

    AccountState from_after = from_before;
    from_after.balance -= t.amount;
    from_after.nonce += 1;
    AccountState to_after = to_before;
    to_after.balance += t.amount;

    work[t.from] = from_after;
    work[t.to] = to_after;
  }

  // All checks passed — atomic commit. No earlier step mutated state_/blocks_/
  // cumWork_, so any failure above left the chain byte-identical.
  state_.swap(work);
  blocks_.push_back(b);
  cumWork_ += crypto::block_work(difficulty_);  // 2^D (== 1 at D=0)
  undoLog_.push_back(CommitDelta{std::move(inverse)});
  return Reason::OK;
}

Reason Chain::validateChain() const {
  // Ground truth: replay from genesis, ignoring the cached state_ entirely. Any
  // disagreement with state_ would indicate a commit-path bug.
  return replayFromGenesis(blocks_).reason;
}

}  // namespace txchain::chain
