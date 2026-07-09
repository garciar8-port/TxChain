#pragma once
// Frozen validation-reason enum (Architecture Chain/State §7). Returned by the
// single write path Chain::connectBlock (CRE-196) and the from-genesis replay
// validator (CRE-197). reasonName() + the `monitor verify` output string are
// added in CRE-197; the enum itself is shared infrastructure defined here where
// connectBlock first returns it.

#include <cstdint>

namespace txchain::chain {

enum class Reason : std::uint8_t {
  OK = 0,
  BAD_LINK,            // index / prevHash / timestamp linkage (V1–V3)
  BAD_TXNS_HASH,       // txnsHash mismatch or block-size cap exceeded (V5–V6)
  BAD_POW,             // blockHash does not meet difficulty (V4; real at M3)
  BAD_SIG,             // Ed25519 verify failed (M2)
  BAD_ADDR,            // from != SHA-256(pubkey)[:20] (M2)
  STALE_NONCE,         // txn nonce != account next-expected (M2)
  INSUFFICIENT_FUNDS,  // amount > sender balance (V7)
  BAD_SUPPLY,          // total-supply invariant broken (M3 coinbase)
};

// Exact enum spelling for output ("OK", "BAD_LINK", …). Used by monitor verify.
const char* reasonName(Reason r) noexcept;

}  // namespace txchain::chain
