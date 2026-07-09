#pragma once
// chain.jsonl persistence (Architecture Data Model §4, Concurrency §6). The
// on-disk chain is a hex-rendered DISPLAY form (rule R6 — never hashed); the
// loader hex-decodes each field and hands a vector<Block> to replayFromGenesis,
// which recomputes every digest from the reconstructed canonical binary. So
// editing any hex digit changes a recomputed digest and fails a replay check —
// this is the mechanism behind the tamper-evident demo.
//
// JSON parsing reuses the already-vendored picojson via serialize::from_json_line
// (no new dependency); the writer uses serialize::to_json_line.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "txchain/chain/types.hpp"
#include "txchain/chain/validate.hpp"

namespace txchain::chain {

struct LoadResult {
  VerifyResult status;         // status.ok == loaded/parsed cleanly
  std::vector<Block> blocks;   // populated on success (genesis at [0])
};

// Durable append-only chain.jsonl store. Constructed with the full file path; use
// forDatadir() to derive <datadir>/chain.jsonl.
class ChainStore {
public:
  explicit ChainStore(std::string chain_file_path);
  static ChainStore forDatadir(const std::string& datadir);

  const std::string& path() const noexcept { return path_; }

  // Serialize one committed block to a JSONL line, append, then fsync — the
  // durability barrier. Called by the sealer AFTER connectBlock returns OK, and
  // OUTSIDE any held lock (Concurrency §6: no syscall under the lock). Returns
  // false on any I/O error.
  bool appendBlock(const Block& b) const;

  // Load the whole chain.jsonl into a vector<Block> per the anchor §4.3 pipeline.
  // A malformed line or a wrong-length hex field is a parse failure mapped to the
  // reason enum at that line index. On success, status.ok == true and blocks is
  // ready for replayFromGenesis.
  LoadResult load() const;

private:
  std::string path_;
};

}  // namespace txchain::chain
