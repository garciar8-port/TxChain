#pragma once
// Chain/State core types (Architecture Chain/State §1). The canonical Txn /
// BlockHeader / Block structs live in the serialize module (the single
// serializer); the chain layer speaks in the SAME types — no duplicate byte
// layout. This file adds the self-documenting typedefs and the committed
// account-state record.

#include <cstdint>

#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/crypto/hashutil.hpp"  // crypto::Work
#include "txchain/serialize/types.hpp"

namespace txchain::chain {

using Hash256 = crypto::Digest32;   // 32 bytes
using Address = crypto::Address20;  // 20 bytes
using PubKey = crypto::PubKey32;    // 32 bytes
using Sig = crypto::Sig64;          // 64 bytes
using Work = crypto::Work;          // unsigned __int128 (crypto §7)

using Txn = serialize::Txn;
using BlockHeader = serialize::BlockHeader;
using Block = serialize::Block;

// Committed per-account state. Defaults {0,0} — the deliberate replacement for
// v1's implicit INITIAL_BALANCE = 1000. New addresses start empty.
struct AccountState {
  std::uint64_t balance = 0;
  std::uint64_t nonce = 0;
};

inline bool operator==(const AccountState& a, const AccountState& b) noexcept {
  return a.balance == b.balance && a.nonce == b.nonce;
}
inline bool operator!=(const AccountState& a, const AccountState& b) noexcept {
  return !(a == b);
}

}  // namespace txchain::chain
