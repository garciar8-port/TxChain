#include "txchain/serialize/json.hpp"

#include <cstdint>
#include <string>

#include <picojson.h>  // vendored, included as a SYSTEM header (no -Werror)

#include "txchain/crypto/hashutil.hpp"
#include "txchain/serialize/canonical.hpp"

namespace txchain::serialize {

namespace {

using crypto::from_hex;
using crypto::to_hex;

std::string txn_to_json(const Txn& t) {
  std::string s = "{";
  s += "\"ver\":" + std::to_string(static_cast<unsigned>(t.ver)) + ",";
  s += "\"from\":\"" + to_hex(t.from) + "\",";
  s += "\"to\":\"" + to_hex(t.to) + "\",";
  s += "\"amount\":" + std::to_string(t.amount) + ",";
  s += "\"nonce\":" + std::to_string(t.nonce) + ",";
  s += "\"pubkey\":\"" + to_hex(t.pubkey) + "\",";
  s += "\"sig\":\"" + to_hex(t.sig) + "\",";
  s += "\"txid\":\"" + to_hex(txid(t)) + "\"}";  // display-only
  return s;
}

// Extract a non-negative u64 from an object key (accepts int64 or double form).
bool get_u64(const picojson::object& o, const char* key, std::uint64_t& out) {
  const auto it = o.find(key);
  if (it == o.end()) return false;
  if (it->second.is<std::int64_t>()) {
    const std::int64_t x = it->second.get<std::int64_t>();
    if (x < 0) return false;
    out = static_cast<std::uint64_t>(x);
    return true;
  }
  if (it->second.is<double>()) {
    const double d = it->second.get<double>();
    if (d < 0) return false;
    out = static_cast<std::uint64_t>(d);
    return true;
  }
  return false;
}

// Hex-decode a fixed-width array field (from_hex enforces the exact length).
template <std::size_t N>
bool get_hex(const picojson::object& o, const char* key, std::array<Byte, N>& out) {
  const auto it = o.find(key);
  if (it == o.end() || !it->second.is<std::string>()) return false;
  return from_hex(it->second.get<std::string>(), out);
}

}  // namespace

std::string to_json_line(const Block& block) {
  std::string s = "{\"header\":{";
  s += "\"index\":" + std::to_string(block.header.index) + ",";
  s += "\"timestamp\":" + std::to_string(block.header.timestamp) + ",";
  s += "\"prevHash\":\"" + to_hex(block.header.prevHash) + "\",";
  s += "\"txnsHash\":\"" + to_hex(block.header.txnsHash) + "\",";
  s += "\"nonce\":" + std::to_string(block.header.nonce) + "},";
  s += "\"txns\":[";
  for (std::size_t i = 0; i < block.txns.size(); ++i) {
    if (i) s += ",";
    s += txn_to_json(block.txns[i]);
  }
  s += "],";
  s += "\"blockHash\":\"" + to_hex(block_hash(block.header)) + "\"}";  // display-only
  return s;
}

std::optional<Block> from_json_line(std::string_view line) {
  picojson::value v;
  const std::string err = picojson::parse(v, std::string(line));
  if (!err.empty() || !v.is<picojson::object>()) return std::nullopt;
  const auto& root = v.get<picojson::object>();

  const auto hit = root.find("header");
  if (hit == root.end() || !hit->second.is<picojson::object>()) return std::nullopt;
  const auto& hobj = hit->second.get<picojson::object>();

  Block blk;
  if (!get_u64(hobj, "index", blk.header.index)) return std::nullopt;
  if (!get_u64(hobj, "timestamp", blk.header.timestamp)) return std::nullopt;
  if (!get_hex(hobj, "prevHash", blk.header.prevHash)) return std::nullopt;
  if (!get_hex(hobj, "txnsHash", blk.header.txnsHash)) return std::nullopt;
  if (!get_u64(hobj, "nonce", blk.header.nonce)) return std::nullopt;

  const auto tit = root.find("txns");
  if (tit == root.end() || !tit->second.is<picojson::array>()) return std::nullopt;
  for (const auto& tv : tit->second.get<picojson::array>()) {
    if (!tv.is<picojson::object>()) return std::nullopt;
    const auto& to = tv.get<picojson::object>();
    Txn t;
    std::uint64_t ver = 0;
    if (!get_u64(to, "ver", ver) || ver != kTxnVersion) return std::nullopt;
    t.ver = static_cast<std::uint8_t>(ver);
    if (!get_hex(to, "from", t.from)) return std::nullopt;
    if (!get_hex(to, "to", t.to)) return std::nullopt;
    if (!get_u64(to, "amount", t.amount)) return std::nullopt;
    if (!get_u64(to, "nonce", t.nonce)) return std::nullopt;
    if (!get_hex(to, "pubkey", t.pubkey)) return std::nullopt;
    if (!get_hex(to, "sig", t.sig)) return std::nullopt;
    blk.txns.push_back(t);
  }
  return blk;
}

}  // namespace txchain::serialize
