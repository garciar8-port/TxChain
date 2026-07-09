#include "txchain/chain/genesis.hpp"

namespace txchain::chain {

void applyGenesis(std::map<Address, AccountState>& state) {
  state.clear();
  for (const auto& e : GENESIS_ALLOC) {
    state[e.addr] = AccountState{e.amount, 0};
  }
}

Block genesisBlock() {
  Block b;
  b.header.index = 0;
  b.header.timestamp = GENESIS_TIMESTAMP;
  b.header.prevHash = Hash256{};        // 32 zero bytes
  b.header.txnsHash = b.computeTxnsHash();  // SHA-256("") over empty txns
  b.header.nonce = 0;                    // genesis is PoW-exempt
  return b;
}

}  // namespace txchain::chain
