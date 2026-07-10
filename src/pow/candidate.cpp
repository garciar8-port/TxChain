#include "txchain/pow/candidate.hpp"

#include <algorithm>
#include <map>

#include "txchain/chain/gate.hpp"     // coinbaseTxn
#include "txchain/chain/params.hpp"   // MAX_TXNS_PER_BLOCK, COINBASE_REWARD
#include "txchain/serialize/canonical.hpp"  // txns_hash

namespace txchain::pow {

Candidate buildCandidate(const Address& minerAddr, std::uint64_t tip_index,
                         const Hash256& tip_hash, std::uint64_t tip_timestamp,
                         std::uint64_t now_s, const mempool::Mempool& mp,
                         const AccountLookup& accountAt) {
  Candidate c;
  const std::uint64_t height = tip_index + 1;

  // Coinbase is always tx0.
  c.txns.push_back(chain::coinbaseTxn(minerAddr, height));

  // Shadow balance index, materialized lazily on first touch (only touched
  // accounts are read from the committed index, never the whole map).
  std::map<Address, AccountState> shadow;
  const auto touch = [&](const Address& a) -> AccountState& {
    const auto it = shadow.find(a);
    if (it != shadow.end()) return it->second;
    return shadow.emplace(a, accountAt(a)).first->second;
  };

  // Seed the coinbase credit so a same-block txn from the miner would see it.
  touch(minerAddr).balance += chain::COINBASE_REWARD;

  // Greedy selection in ascending admission-seq order (no sort on the hot path).
  for (const mempool::MempoolEntry* e : mp.sortedBySeq()) {
    if (c.txns.size() >= chain::MAX_TXNS_PER_BLOCK) break;  // k ≤ 8 incl. coinbase
    const Txn& t = e->txn;
    AccountState& from = touch(t.from);
    AccountState& to = touch(t.to);  // materialize both on first touch
    if (t.nonce != from.nonce) continue;         // slot rule ⇒ ≤1 per sender anyway
    if (t.amount > from.balance) continue;        // re-check funds vs. the live tip
    c.txns.push_back(t);
    from.balance -= t.amount;
    from.nonce += 1;
    to.balance += t.amount;
  }

  // Assemble the header; nonce = 0 (the miner searches from there).
  c.hdr.index = height;
  c.hdr.timestamp = std::max<std::uint64_t>(now_s, tip_timestamp + 1);  // strictly increasing
  c.hdr.prevHash = tip_hash;
  c.hdr.txnsHash = serialize::txns_hash(c.txns);  // §3.2 single-commitment over 153-byte txns
  c.hdr.nonce = 0;
  return c;
}

}  // namespace txchain::pow
