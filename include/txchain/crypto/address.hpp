#pragma once
// Address derivation (Architecture Cryptography §6). The identity rule that
// replaces v1's plaintext-username "identity": an address is SHA-256(pubkey)[:20]
// — the first 20 raw bytes of the public key's digest. `address_matches` is the
// cheap structural gate Pillar 2 runs BEFORE signature verification (BAD_ADDR).

#include "txchain/crypto/fixedbytes.hpp"

namespace txchain::crypto {

// address = SHA-256(pubkey)[:20] (first 20 raw bytes of the 32-byte digest).
Address20 address(const PubKey32& pubkey) noexcept;

// True iff `from` equals address(pubkey). Named so the Pillar-2 gate reads
// `if (!address_matches(txn.from, txn.pubkey)) return BAD_ADDR;` and the check
// is impossible to accidentally skip.
[[nodiscard]] bool address_matches(const Address20& from, const PubKey32& pubkey) noexcept;

}  // namespace txchain::crypto
