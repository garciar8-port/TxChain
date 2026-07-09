#include "txchain/chain/chain.hpp"

#include "txchain/chain.hpp"        // umbrella (module_name)
#include "txchain/chain/genesis.hpp"

namespace txchain::chain {

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

bool Chain::connectBlock(const Block& b) {
  // Stub — CRE-196 implements validate-against-tip + commit. Reference the
  // Pillar-4 undo log so it is not flagged unused until then.
  (void)b;
  (void)undoLog_;
  return false;
}

bool Chain::validateChain() const { return false; }  // Stub — CRE-197

}  // namespace txchain::chain
