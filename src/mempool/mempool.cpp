#include "txchain/mempool/mempool.hpp"

#include <map>
#include <utility>

#include "txchain/chain/gate.hpp"           // applyTxn — the shared consensus verify gate
#include "txchain/mempool.hpp"              // module_name (scaffold marker)
#include "txchain/serialize/canonical.hpp"  // txid, deserialize_txn

namespace txchain::mempool {

const char* module_name() noexcept { return "mempool"; }

AdmitResult reason_to_admit(Reason r) noexcept {
  switch (r) {
    case Reason::OK: return AdmitResult::OK;
    case Reason::BAD_ADDR: return AdmitResult::BAD_ADDR;
    case Reason::BAD_SIG: return AdmitResult::BAD_SIG;
    case Reason::STALE_NONCE: return AdmitResult::STALE_NONCE;
    case Reason::INSUFFICIENT_FUNDS: return AdmitResult::INSUFFICIENT_FUNDS;
    default: return AdmitResult::UNPARSEABLE;  // non-gate reasons are not admission outcomes
  }
}

const char* admit_result_name(AdmitResult a) noexcept {
  switch (a) {
    case AdmitResult::OK: return "OK";
    case AdmitResult::BAD_SIG: return "BAD_SIG";
    case AdmitResult::BAD_ADDR: return "BAD_ADDR";
    case AdmitResult::STALE_NONCE: return "STALE_NONCE";
    case AdmitResult::INSUFFICIENT_FUNDS: return "INSUFFICIENT_FUNDS";
    case AdmitResult::UNPARSEABLE: return "UNPARSEABLE";
    case AdmitResult::DUPLICATE: return "DUPLICATE";
    case AdmitResult::MEMPOOL_FULL: return "MEMPOOL_FULL";
    case AdmitResult::PENDING_SLOT_BUSY: return "PENDING_SLOT_BUSY";
  }
  return "UNKNOWN";  // unreachable; keeps -Wreturn-type happy
}

Mempool::Mempool(AccountLookup account_at, std::size_t cap)
    : account_at_(std::move(account_at)), cap_(cap) {}

AccountState Mempool::accountAt(const Address& a) const { return account_at_(a); }

AdmitResult Mempool::admit(crypto::ByteView txn_bytes) {
  // deserialize_txn returns nullopt on a wrong length OR ver != 0x01.
  const auto t = serialize::deserialize_txn(txn_bytes);
  if (!t.has_value()) return AdmitResult::UNPARSEABLE;
  return admit(*t);
}

AdmitResult Mempool::admit(const Txn& t) {
  // 0. Structural (Data Model §2.3 step 1). The Txn struct is always 153 bytes
  //    when serialized; the byte-level overload catches wrong lengths.
  if (t.ver != serialize::kTxnVersion) return AdmitResult::UNPARSEABLE;

  // 1. Dedup — compute txid ONCE and cache it (never recomputed on the hot path).
  const Digest32 id = serialize::txid(t);
  if (byTxid_.count(id) != 0) return AdmitResult::DUPLICATE;

  // 2–5. Reuse the SAME consensus gate as connect/replay, run against a shadow of
  //      tip state so admission and inclusion can never diverge. txn_index = 1
  //      (non-zero) guarantees the coinbase-exemption branch is never taken — a
  //      user txn is never a coinbase.
  std::map<Address, AccountState> shadow;
  shadow[t.from] = accountAt(t.from);
  const Reason r = chain::applyTxn(shadow, t, /*block_index=*/0, /*txn_index=*/1);
  if (r == Reason::BAD_ADDR || r == Reason::BAD_SIG || r == Reason::STALE_NONCE)
    return reason_to_admit(r);  // 2 (BAD_ADDR) → 3 (BAD_SIG) → 4a (STALE_NONCE)
  if (r != Reason::OK && r != Reason::INSUFFICIENT_FUNDS)
    return AdmitResult::BAD_ADDR;  // coinbase-shaped / null-from is not user-admissible

  // 4b. Single pending slot: even a correctly-next nonce is refused while a prior
  //     txn from this sender sits unmined (normative order: after nonce, before funds).
  if (bySender_.count(t.from) != 0) return AdmitResult::PENDING_SLOT_BUSY;

  // 5. Provisional funds, against the tip balance (re-checked authoritatively at commit).
  if (r == Reason::INSUFFICIENT_FUNDS) return AdmitResult::INSUFFICIENT_FUNDS;

  // 6. Capacity — a hard bound on pending set size.
  if (byTxid_.size() >= cap_) return AdmitResult::MEMPOOL_FULL;

  // 7. Accept.
  const std::uint64_t seq = next_seq_++;
  byTxid_.emplace(id, MempoolEntry{t, id, seq});
  bySender_[t.from] = id;
  bySeq_[seq] = id;
  return AdmitResult::OK;
}

void Mempool::evictIncluded(const std::vector<Txn>& txns) {
  for (const Txn& t : txns) {
    const Digest32 id = serialize::txid(t);
    const auto it = byTxid_.find(id);
    if (it == byTxid_.end()) continue;
    bySeq_.erase(it->second.feeless_seq);
    const auto sit = bySender_.find(t.from);
    if (sit != bySender_.end() && sit->second == id) bySender_.erase(sit);
    byTxid_.erase(it);
  }
}

std::vector<const MempoolEntry*> Mempool::sortedBySeq() const {
  std::vector<const MempoolEntry*> out;
  out.reserve(bySeq_.size());
  for (const auto& kv : bySeq_) {  // std::map iterates in ascending key (seq) order
    const auto it = byTxid_.find(kv.second);
    if (it != byTxid_.end()) out.push_back(&it->second);
  }
  return out;
}

}  // namespace txchain::mempool
