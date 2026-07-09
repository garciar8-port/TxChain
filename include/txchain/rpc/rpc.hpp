#pragma once
// The node's HTTP RPC surface (Architecture Node/CLI §4). Three M2 endpoints:
//   POST /tx              — submit a signed txn → Mempool::admit → {txid} | {error}
//   GET  /account/{addr}  — {address, balance, confirmedNonce, pendingNonce}
//   GET  /tip             — {index, blockHash, prevHash, cumWork, difficulty, timestamp}
//
// handle_request is a PURE dispatch function (no sockets, no locking) so the
// endpoint logic is unit-tested without a network. The socket layer (HttpServer)
// and the shared-state lock live outside it; the caller holds the lock across a
// handle_request call so a response is a consistent snapshot.

#include <cstdint>
#include <functional>
#include <string>

#include "txchain/chain/types.hpp"
#include "txchain/mempool/mempool.hpp"

namespace txchain::rpc {

using chain::AccountState;
using chain::Address;
using chain::Hash256;
using chain::Work;

// A recomputed snapshot of the chain tip for GET /tip (never trusted from disk).
struct TipInfo {
  std::uint64_t index = 0;
  Hash256 blockHash{};
  Hash256 prevHash{};
  Work cumWork = 0;
  unsigned difficulty = 0;
  std::uint64_t timestamp = 0;
};

// Everything a handler reads/mutates, decoupled from Chain/Node internals so the
// pure handler is testable without sockets. `account`/`tip` are read closures
// invoked under the caller's lock; `mempool` is the single mutation point.
struct RpcContext {
  std::function<AccountState(const Address&)> account;  // committed tip lookup
  std::function<TipInfo()> tip;                          // recomputed tip snapshot
  mempool::Mempool& mempool;
};

struct RpcResponse {
  int status = 200;  // HTTP status code
  std::string body;  // JSON
};

// Dispatch method + request-target (path, may carry a query) to the M2 routes.
// Body is the request body (used by POST /tx). Never throws.
RpcResponse handle_request(const std::string& method, const std::string& target,
                           const std::string& body, RpcContext& ctx);

// Scaffold marker (kept from CRE-188) so the module object is non-empty.
const char* module_name() noexcept;

}  // namespace txchain::rpc
