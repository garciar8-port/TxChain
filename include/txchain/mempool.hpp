#pragma once
// mempool module — public umbrella header (txchain_core).
//
// The bounded, deduplicated pending-transaction set and its admission gate. The
// real API lives in mempool/mempool.hpp (CRE-202); the module depends on chain
// (the shared verify gate + Reason enum) and serialize (txid), bottom-up.

#include "txchain/mempool/mempool.hpp"

namespace txchain::mempool {

// Returns the module's name. Exists solely so the object is non-empty and the
// symbol can be linked from tests, proving the library boundary is wired.
const char* module_name() noexcept;

}  // namespace txchain::mempool
