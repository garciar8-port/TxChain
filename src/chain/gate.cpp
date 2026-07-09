#include "txchain/chain/gate.hpp"

#include <array>

#include "txchain/chain/params.hpp"
#include "txchain/crypto/address.hpp"
#include "txchain/crypto/ed25519.hpp"
#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/serialize/canonical.hpp"

namespace txchain::chain {

namespace {
// Coinbase sentinels: from = 0x00×20, pubkey = 0x00×32, sig = 0x00×64.
constexpr Address kNullAddr{};
constexpr PubKey kNullPub{};
constexpr Sig kNullSig{};
}  // namespace

Reason applyTxn(std::map<Address, AccountState>& work, const Txn& t,
                std::uint64_t block_index, std::size_t txn_index) noexcept {
  const bool null_from = (t.from == kNullAddr);

  // ---- Coinbase exemption (Chain/State §4.4) ----
  if (null_from) {
    if (txn_index != 0) return Reason::BAD_TXNS_HASH;  // misplaced coinbase (only tx0)
    // Structurally a real txn but exempt from addr/sig/nonce: it mints supply
    // rather than moving it. Enforce the fixed shape.
    if (!(t.pubkey == kNullPub) || !(t.sig == kNullSig) || t.nonce != block_index)
      return Reason::BAD_TXNS_HASH;                    // malformed coinbase
    if (t.amount != COINBASE_REWARD) return Reason::BAD_SUPPLY;  // wrong mint amount
    work[t.to].balance += t.amount;                    // pure +reward credit, no debit
    return Reason::OK;
  }

  // ---- Normal txn: frozen verification order (Data Model §2.3) ----
  // 1. Cheap 20-byte address compare gates the expensive signature check.
  if (!crypto::address_matches(t.from, t.pubkey)) return Reason::BAD_ADDR;
  // 2. Ed25519 over the recomputed 89-byte payload (ver‖from‖to‖amount‖nonce‖pubkey).
  const std::array<crypto::Byte, serialize::kSignedPayloadSize> payload =
      serialize::signed_payload(t);
  if (!crypto::verify(t.pubkey, t.sig, crypto::ByteView(payload.data(), payload.size())))
    return Reason::BAD_SIG;

  const AccountState from_before = work.count(t.from) ? work[t.from] : AccountState{};
  // 3. nonce must equal the next-expected (below = already spent, above = gap).
  if (t.nonce != from_before.nonce) return Reason::STALE_NONCE;
  // 4. funds — only after every authenticity check has passed.
  if (t.amount > from_before.balance) return Reason::INSUFFICIENT_FUNDS;

  // ---- 5. Apply: debit sender (+nonce), credit receiver (conserves supply) ----
  const AccountState to_before = work.count(t.to) ? work[t.to] : AccountState{};
  AccountState from_after = from_before;
  from_after.balance -= t.amount;
  from_after.nonce += 1;
  AccountState to_after = to_before;
  to_after.balance += t.amount;
  work[t.from] = from_after;
  work[t.to] = to_after;
  return Reason::OK;
}

}  // namespace txchain::chain
