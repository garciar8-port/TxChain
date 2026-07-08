#pragma once
// Canonical data-model structs (Architecture Data Model §2, §3). These hold the
// FIELD VALUES; the single serializer (canonical.hpp) is the only place that
// turns them into the frozen big-endian, fixed-width bytes that get hashed and
// signed. v1's plaintext-username + server-serial + native-endian struct is
// gone: identities are 20-byte addresses, ordering is block inclusion + nonce,
// and the bytes themselves are the hashed object.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "txchain/crypto/fixedbytes.hpp"

namespace txchain::serialize {

using crypto::Address20;
using crypto::Byte;
using crypto::Digest32;
using crypto::PubKey32;
using crypto::Sig64;

// Frozen sizes (anchor §2.1, §3.1) and the frozen schema version.
inline constexpr std::size_t kTxnSize = 153;
inline constexpr std::size_t kHeaderSize = 88;
inline constexpr std::size_t kSignedPayloadSize = 89;
inline constexpr std::uint8_t kTxnVersion = 0x01;

struct Txn {
  std::uint8_t ver = kTxnVersion;
  Address20 from{};
  Address20 to{};
  std::uint64_t amount = 0;
  std::uint64_t nonce = 0;
  PubKey32 pubkey{};
  Sig64 sig{};
};

inline bool operator==(const Txn& a, const Txn& b) noexcept {
  return a.ver == b.ver && a.from == b.from && a.to == b.to &&
         a.amount == b.amount && a.nonce == b.nonce && a.pubkey == b.pubkey &&
         a.sig == b.sig;
}
inline bool operator!=(const Txn& a, const Txn& b) noexcept { return !(a == b); }

struct BlockHeader {
  std::uint64_t index = 0;
  std::uint64_t timestamp = 0;
  Digest32 prevHash{};
  Digest32 txnsHash{};
  std::uint64_t nonce = 0;
};

inline bool operator==(const BlockHeader& a, const BlockHeader& b) noexcept {
  return a.index == b.index && a.timestamp == b.timestamp &&
         a.prevHash == b.prevHash && a.txnsHash == b.txnsHash &&
         a.nonce == b.nonce;
}
inline bool operator!=(const BlockHeader& a, const BlockHeader& b) noexcept {
  return !(a == b);
}

struct Block {
  BlockHeader header;
  std::vector<Txn> txns;
};

inline bool operator==(const Block& a, const Block& b) noexcept {
  return a.header == b.header && a.txns == b.txns;
}
inline bool operator!=(const Block& a, const Block& b) noexcept {
  return !(a == b);
}

}  // namespace txchain::serialize
