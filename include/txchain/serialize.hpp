#pragma once
// serialize module — public umbrella header (txchain_core).
//
// The canonical data model + THE single serializer (the one place that touches
// byte order for Txn/BlockHeader) + the chain.jsonl JSON display boundary.

#include "txchain/serialize/canonical.hpp"
#include "txchain/serialize/json.hpp"
#include "txchain/serialize/types.hpp"

namespace txchain::serialize {

// Scaffold marker (kept from CRE-188) so the module object is non-empty.
const char* module_name() noexcept;

}  // namespace txchain::serialize
