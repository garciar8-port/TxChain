#pragma once
// rpc module — public umbrella header (txchain_core).
//
// The node's HTTP RPC surface: the pure request handler (rpc/rpc.hpp) and the
// minimal loopback HTTP/1.1 server (rpc/http_server.hpp). Depends on chain
// (tip/account reads) and mempool (POST /tx → admit), bottom-up.

#include "txchain/rpc/http_server.hpp"
#include "txchain/rpc/rpc.hpp"

namespace txchain::rpc {

// Returns the module's name. Exists solely so the object is non-empty and the
// symbol can be linked from tests, proving the library boundary is wired.
const char* module_name() noexcept;

}  // namespace txchain::rpc
