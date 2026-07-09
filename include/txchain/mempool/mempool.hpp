#pragma once
// The mempool: validated, not-yet-mined transactions awaiting inclusion
// (Architecture Mempool §1–§3). Two load-bearing rules:
//   (a) at most ONE pending nonce per sender — a flat single-slot design (no fee
//       market, no RBF, no gap-buffering — stricter than Bitcoin/Ethereum on
//       purpose), and
//   (b) admission REUSES the exact consensus verify gate (chain::applyTxn) so a
//       txn that admits will also connect — the mempool never has its own copy
//       of the address/sig/nonce/funds logic that could drift from consensus.
//
// AdmitResult is a DISTINCT type from the consensus Reason enum: its four
// consensus-shared members carry the same numeric values + spelling (so an
// admission rejection surfaces the identical token in an RPC response), while the
// mempool-local members (UNPARSEABLE/DUPLICATE/MEMPOOL_FULL/PENDING_SLOT_BUSY)
// exist only here and never appear in a block, in monitor verify, or on the wire.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "txchain/chain/reason.hpp"
#include "txchain/chain/types.hpp"
#include "txchain/crypto/fixedbytes.hpp"

namespace txchain::mempool {

using chain::AccountState;
using chain::Address;
using chain::Reason;
using chain::Txn;
using crypto::Digest32;

// A pending txn plus its cached content id and admission sequence number.
struct MempoolEntry {
  Txn txn;
  Digest32 txid{};
  std::uint64_t feeless_seq = 0;  // strictly increasing admission order (no fees)
};

// Admission outcome. Shared members == the same-named chain::Reason numeric value
// (bridged by reason_to_admit); local members are >= 100 to avoid any collision.
enum class AdmitResult : std::uint8_t {
  OK = 0,
  BAD_SIG = 4,             // == Reason::BAD_SIG
  BAD_ADDR = 5,            // == Reason::BAD_ADDR
  STALE_NONCE = 6,         // == Reason::STALE_NONCE
  INSUFFICIENT_FUNDS = 7,  // == Reason::INSUFFICIENT_FUNDS
  UNPARSEABLE = 100,       // local-only (never on the wire / in a block / monitor verify)
  DUPLICATE = 101,
  MEMPOOL_FULL = 102,
  PENDING_SLOT_BUSY = 103,
};

// The shared members must share numeric values with the consensus enum so the
// bridge is a value-preserving mapping.
static_assert(static_cast<std::uint8_t>(AdmitResult::BAD_SIG) ==
                  static_cast<std::uint8_t>(Reason::BAD_SIG),
              "AdmitResult::BAD_SIG must match Reason::BAD_SIG");
static_assert(static_cast<std::uint8_t>(AdmitResult::BAD_ADDR) ==
                  static_cast<std::uint8_t>(Reason::BAD_ADDR),
              "AdmitResult::BAD_ADDR must match Reason::BAD_ADDR");
static_assert(static_cast<std::uint8_t>(AdmitResult::STALE_NONCE) ==
                  static_cast<std::uint8_t>(Reason::STALE_NONCE),
              "AdmitResult::STALE_NONCE must match Reason::STALE_NONCE");
static_assert(static_cast<std::uint8_t>(AdmitResult::INSUFFICIENT_FUNDS) ==
                  static_cast<std::uint8_t>(Reason::INSUFFICIENT_FUNDS),
              "AdmitResult::INSUFFICIENT_FUNDS must match Reason::INSUFFICIENT_FUNDS");

// Bridge a gate Reason to the same-named AdmitResult (shared members only).
AdmitResult reason_to_admit(Reason r) noexcept;

// Token spelling; the consensus-shared members return the exact same string as
// chain::reasonName so an RPC surfaces one token.
const char* admit_result_name(AdmitResult a) noexcept;

class Mempool {
 public:
  // Read-only view of the committed tip state; an absent address ⇒ {0,0}.
  using AccountLookup = std::function<AccountState(const Address&)>;

  explicit Mempool(AccountLookup account_at, std::size_t cap = 4096);

  // Admit a txn in the frozen normative order (Mempool §3). Single mutation point.
  AdmitResult admit(const Txn& t);
  // Byte-level entry: a wrong length or ver != 0x01 is UNPARSEABLE.
  AdmitResult admit(crypto::ByteView txn_bytes);

  // Commit hook (Chain/State §4.6 step 4): drop every included txn and free its
  // sender slot so the sender's next nonce can be admitted.
  void evictIncluded(const std::vector<Txn>& txns);

  // Ascending-feeless_seq (admission order) view — no per-call sort (bySeq_ is
  // an ordered map, so iteration is already sorted).
  std::vector<const MempoolEntry*> sortedBySeq() const;

  AccountState accountAt(const Address& a) const;
  std::size_t size() const noexcept { return byTxid_.size(); }
  bool empty() const noexcept { return byTxid_.empty(); }
  bool contains(const Digest32& txid) const { return byTxid_.count(txid) != 0; }
  std::size_t cap() const noexcept { return cap_; }

 private:
  // FNV-1a over a fixed-width byte array — std provides no hash for std::array.
  struct ArrayHash {
    template <std::size_t N>
    std::size_t operator()(const std::array<crypto::Byte, N>& a) const noexcept {
      std::size_t h = 1469598103934665603ULL;
      for (crypto::Byte b : a) {
        h ^= b;
        h *= 1099511628211ULL;
      }
      return h;
    }
  };

  AccountLookup account_at_;
  std::size_t cap_;
  std::uint64_t next_seq_ = 0;
  std::unordered_map<Digest32, MempoolEntry, ArrayHash> byTxid_;
  std::unordered_map<Address, Digest32, ArrayHash> bySender_;  // single pending slot / sender
  std::map<std::uint64_t, Digest32> bySeq_;                    // admission-ordered selection view
};

}  // namespace txchain::mempool
