#include "txchain/chain/store.hpp"

#include <cerrno>
#include <cstddef>
#include <fstream>
#include <string>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include "txchain/serialize/json.hpp"

namespace txchain::chain {

ChainStore::ChainStore(std::string chain_file_path) : path_(std::move(chain_file_path)) {}

ChainStore ChainStore::forDatadir(const std::string& datadir) {
  std::string p = datadir;
  if (!p.empty() && p.back() != '/') p.push_back('/');
  p += "chain.jsonl";
  return ChainStore(std::move(p));
}

bool ChainStore::appendBlock(const Block& b) const {
  const std::string line = serialize::to_json_line(b) + "\n";

  const int fd = ::open(path_.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
  if (fd < 0) return false;

  const char* p = line.data();
  std::size_t remaining = line.size();
  while (remaining > 0) {
    const ssize_t w = ::write(fd, p, remaining);
    if (w < 0) {
      if (errno == EINTR) continue;
      ::close(fd);
      return false;
    }
    p += w;
    remaining -= static_cast<std::size_t>(w);
  }

  // Durability barrier: the block is not "committed to disk" until fsync returns.
  if (::fsync(fd) != 0) {
    ::close(fd);
    return false;
  }
  ::close(fd);
  return true;
}

LoadResult ChainStore::load() const {
  LoadResult lr;

  std::ifstream in(path_);
  if (!in) {
    lr.status = VerifyResult{false, 0, Reason::BAD_LINK, "cannot open chain.jsonl"};
    return lr;
  }

  std::string line;
  std::uint64_t idx = 0;
  while (std::getline(in, line)) {
    if (line.empty()) continue;  // tolerate a trailing blank line
    // from_json_line hex-decodes every field (from_hex<N> rejects wrong lengths)
    // and rebuilds the canonical Block; nullopt ⇒ malformed line / bad hex.
    const auto blk = serialize::from_json_line(line);
    if (!blk.has_value()) {
      lr.status = VerifyResult{false, idx, Reason::BAD_TXNS_HASH, "malformed chain.jsonl line"};
      return lr;
    }
    lr.blocks.push_back(*blk);
    ++idx;
  }

  lr.status.ok = true;
  return lr;
}

}  // namespace txchain::chain
