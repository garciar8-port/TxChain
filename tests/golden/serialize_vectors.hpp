#pragma once
// Canonical-serializer golden vectors (Architecture Data Model §5). The txid /
// txnsHash / blockHash constants were computed by an independent Python SHA-256
// over the exact frozen bytes. DO NOT EDIT BY HAND.
#include <cstdint>
#include <string_view>

namespace txchain::serialize::test {

// ---- Vector A: Transaction (anchor §5.1) ----
inline constexpr std::string_view kVecA_from   = "75877bb41d393b5fb8455ce60ecd8dda001d0631";
inline constexpr std::string_view kVecA_to     = "aabbccddeeff00112233445566778899aabbccdd";
inline constexpr std::uint64_t     kVecA_amount = 100;
inline constexpr std::uint64_t     kVecA_nonce  = 0;
inline constexpr std::string_view kVecA_pubkey = "0202020202020202020202020202020202020202020202020202020202020202";
inline constexpr std::string_view kVecA_sig    = "07070707070707070707070707070707070707070707070707070707070707070707070707070707070707070707070707070707070707070707070707070707";
inline constexpr std::string_view kVecA_signed_payload = "0175877bb41d393b5fb8455ce60ecd8dda001d0631aabbccddeeff00112233445566778899aabbccdd000000000000006400000000000000000202020202020202020202020202020202020202020202020202020202020202";  // 89 bytes
inline constexpr std::string_view kVecA_txid   = "f494b9e7e22da1dfd4d226dae987527c51cd65aaf8c08c747ff17ab5e5de32b0";

// ---- Vector C: Block header committing to exactly Vector A (anchor §5.3) ----
inline constexpr std::uint64_t     kVecC_index     = 1;
inline constexpr std::uint64_t     kVecC_timestamp = 1704067201;  // 0x65921c01
inline constexpr std::string_view kVecC_prevHash  = "00000000000000000000000000000000000000000000000000000000000000ff";
inline constexpr std::string_view kVecC_txnsHash  = "f494b9e7e22da1dfd4d226dae987527c51cd65aaf8c08c747ff17ab5e5de32b0";  // == SHA-256(txA)
inline constexpr std::uint64_t     kVecC_nonce     = 42;
inline constexpr std::string_view kVecC_blockHash = "c80bac7618b8618e4674f91d069cdc441a247357dab38c3a180305825205defc";

// SHA-256("") — the empty-block txnsHash constant.
inline constexpr std::string_view kEmptyTxnsHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

}  // namespace txchain::serialize::test
