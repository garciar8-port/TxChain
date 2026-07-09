#include "txchain/rpc/rpc.hpp"

#include <array>
#include <cstddef>
#include <string>

#include <picojson.h>  // vendored (SYSTEM include on txchain_core), INT64 mode

#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/crypto/hashutil.hpp"  // to_hex / from_hex
#include "txchain/crypto/sha256.hpp"
#include "txchain/serialize/types.hpp"  // kTxnSize

namespace txchain::rpc {

const char* module_name() noexcept { return "rpc"; }

std::optional<std::string> json_get_string(const std::string& json, const std::string& key) {
  picojson::value v;
  if (!picojson::parse(v, json).empty() || !v.is<picojson::object>()) return std::nullopt;
  const auto& o = v.get<picojson::object>();
  const auto it = o.find(key);
  if (it == o.end() || !it->second.is<std::string>()) return std::nullopt;
  return it->second.get<std::string>();
}

std::optional<std::uint64_t> json_get_u64(const std::string& json, const std::string& key) {
  picojson::value v;
  if (!picojson::parse(v, json).empty() || !v.is<picojson::object>()) return std::nullopt;
  const auto& o = v.get<picojson::object>();
  const auto it = o.find(key);
  if (it == o.end()) return std::nullopt;
  if (it->second.is<std::int64_t>() && it->second.get<std::int64_t>() >= 0)
    return static_cast<std::uint64_t>(it->second.get<std::int64_t>());
  if (it->second.is<double>() && it->second.get<double>() >= 0)
    return static_cast<std::uint64_t>(it->second.get<double>());
  return std::nullopt;
}

namespace {

using mempool::AdmitResult;

std::string json_error(const char* token, const std::string& detail) {
  return std::string("{\"error\":\"") + token + "\",\"detail\":\"" + detail + "\"}";
}

// Decimal string for a 128-bit cumulative-work value (JSON numbers can't hold u128).
std::string u128_to_string(Work v) {
  if (v == 0) return "0";
  std::string s;
  while (v > 0) {
    s.push_back(static_cast<char>('0' + static_cast<int>(v % 10)));
    v /= 10;
  }
  return std::string(s.rbegin(), s.rend());
}

std::string trim(const std::string& s) {
  std::size_t b = 0;
  std::size_t e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
  return s.substr(b, e - b);
}

// HTTP status for an AdmitResult. STALE_NONCE (replay) is the demoable 409; the
// other single-slot/dup conflicts are also 409; a full mempool is 503; parse
// issues and the remaining consensus rejections are 400.
int admit_status(AdmitResult a) {
  switch (a) {
    case AdmitResult::OK: return 200;
    case AdmitResult::STALE_NONCE:
    case AdmitResult::DUPLICATE:
    case AdmitResult::PENDING_SLOT_BUSY: return 409;
    case AdmitResult::MEMPOOL_FULL: return 503;
    case AdmitResult::UNPARSEABLE: return 400;
    case AdmitResult::BAD_ADDR:
    case AdmitResult::BAD_SIG:
    case AdmitResult::INSUFFICIENT_FUNDS: return 400;
  }
  return 400;
}

RpcResponse submit_tx(const std::string& body, RpcContext& ctx) {
  // M2 wire form: the canonical 153-byte txn, hex-encoded (306 lowercase hex).
  const std::string hex = trim(body);
  std::array<crypto::Byte, serialize::kTxnSize> bytes{};
  if (!crypto::from_hex<serialize::kTxnSize>(hex, bytes))
    return {400, json_error("PARSE_ERROR", "body must be a 153-byte hex-encoded txn")};

  const crypto::ByteView view(bytes.data(), bytes.size());
  const AdmitResult ar = ctx.mempool.admit(view);
  if (ar == AdmitResult::OK) {
    const auto txid = crypto::sha256(view);  // txid = SHA-256(txn153)
    return {200, std::string("{\"txid\":\"") + crypto::to_hex(txid) + "\"}"};
  }
  // UNPARSEABLE (a right-length buffer that isn't a valid txn) surfaces as the
  // transport PARSE_ERROR token, not a consensus reason.
  const char* token =
      (ar == AdmitResult::UNPARSEABLE) ? "PARSE_ERROR" : mempool::admit_result_name(ar);
  return {admit_status(ar), json_error(token, "rejected by admit()")};
}

RpcResponse account_resp(const std::string& addr_hex, RpcContext& ctx) {
  Address a{};
  if (!crypto::from_hex<20>(addr_hex, a))
    return {400, json_error("BAD_REQUEST", "address must be 40 hex chars")};

  const AccountState acct = ctx.account(a);
  const std::uint64_t confirmed = acct.nonce;
  const std::uint64_t pending = confirmed + (ctx.mempool.hasPending(a) ? 1 : 0);
  return {200, std::string("{\"address\":\"") + crypto::to_hex(a) +
                   "\",\"balance\":" + std::to_string(acct.balance) +
                   ",\"confirmedNonce\":" + std::to_string(confirmed) +
                   ",\"pendingNonce\":" + std::to_string(pending) + "}"};
}

RpcResponse tip_resp(RpcContext& ctx) {
  const TipInfo t = ctx.tip();
  return {200, std::string("{\"index\":") + std::to_string(t.index) + ",\"blockHash\":\"" +
                   crypto::to_hex(t.blockHash) + "\",\"prevHash\":\"" + crypto::to_hex(t.prevHash) +
                   "\",\"cumWork\":\"" + u128_to_string(t.cumWork) +
                   "\",\"difficulty\":" + std::to_string(t.difficulty) +
                   ",\"timestamp\":" + std::to_string(t.timestamp) + "}"};
}

}  // namespace

RpcResponse handle_request(const std::string& method, const std::string& target,
                           const std::string& body, RpcContext& ctx) {
  // Strip any query string; M2 routes are matched on the path alone.
  std::string path = target;
  const auto q = path.find('?');
  if (q != std::string::npos) path = path.substr(0, q);

  if (method == "POST" && path == "/tx") return submit_tx(body, ctx);
  if (method == "GET" && path == "/tip") return tip_resp(ctx);
  if (method == "GET" && path.rfind("/account/", 0) == 0)
    return account_resp(path.substr(std::string("/account/").size()), ctx);

  return {404, json_error("NOT_FOUND", "unknown route")};
}

}  // namespace txchain::rpc
