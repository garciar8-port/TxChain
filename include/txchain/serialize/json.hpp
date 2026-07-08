#pragma once
// chain.jsonl display boundary (Architecture Data Model §4). JSON is used ONLY
// here (and RPC) — it is NEVER hashed (R6). Binary fields render as fixed-width
// lowercase hex; u8/u64 as decimal numbers. On load, blockHash / txid are
// display-only and recomputed from the canonical binary, never trusted.

#include <optional>
#include <string>
#include <string_view>

#include "txchain/serialize/types.hpp"

namespace txchain::serialize {

// Render one block as a single-line JSON object (one chain.jsonl line). Includes
// the display-only blockHash and per-txn txid (recomputed on load).
std::string to_json_line(const Block& block);

// Parse one chain.jsonl line into a Block: parse JSON, hex-decode each field to
// its fixed-width bytes. Returns nullopt on malformed JSON, a wrong-width hex
// field, a bad ver, or a negative number. Stored blockHash/txid are ignored
// (recomputed by the caller against the reconstructed binary).
std::optional<Block> from_json_line(std::string_view line);

}  // namespace txchain::serialize
