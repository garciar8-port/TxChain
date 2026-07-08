#pragma once
// THE single canonical serializer (Architecture Data Model §1–§3). This is the
// ONLY place in the codebase that touches byte order for Txn/BlockHeader —
// big-endian, fixed-width, raw digest bytes, no padding, no delimiters (R1–R7).
// Every hash/signature derives from serialize() output, so freezing this pins
// the cross-build contract and eliminates canonical-serialization drift.

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/serialize/types.hpp"

namespace txchain::serialize {

using crypto::ByteView;

// ---- Canonical binary form ----
std::array<Byte, kTxnSize> serialize(const Txn& tx) noexcept;         // 153 bytes
std::array<Byte, kHeaderSize> serialize(const BlockHeader& h) noexcept;  // 88 bytes

// Decode. Returns nullopt on wrong length, or (for a txn) ver != 0x01. No
// delimiters, raw digests — the length IS the frame.
std::optional<Txn> deserialize_txn(ByteView bytes) noexcept;
std::optional<BlockHeader> deserialize_header(ByteView bytes) noexcept;

// ---- Derived values (over serializer output only) ----
// The signed payload = the first 89 bytes (all fields EXCEPT sig).
std::array<Byte, kSignedPayloadSize> signed_payload(const Txn& tx) noexcept;

// txid = SHA-256(canonical 153-byte txn, including sig).
crypto::Digest32 txid(const Txn& tx) noexcept;

// txnsHash = SHA-256(tx0 ‖ tx1 ‖ …) over the ordered 153-byte canonical txns —
// a flat single commitment, NOT a Merkle tree. Empty list ⇒ SHA-256("").
crypto::Digest32 txns_hash(const std::vector<Txn>& txns) noexcept;

// blockHash = SHA-256(canonical 88-byte header).
crypto::Digest32 block_hash(const BlockHeader& h) noexcept;

}  // namespace txchain::serialize
