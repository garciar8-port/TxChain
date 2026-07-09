#pragma once
// chain module — public umbrella header (txchain_core).
//
// The in-memory hash-linked Block/Chain types, the deterministic genesis
// allocation, and the committed account-state map. connectBlock validation
// (CRE-196), the from-genesis replay validator (CRE-197), and chain.jsonl
// persistence (CRE-198) build on these.

#include "txchain/chain/chain.hpp"
#include "txchain/chain/genesis.hpp"
#include "txchain/chain/types.hpp"

namespace txchain::chain {

// Scaffold marker (kept from CRE-188) so the module object is non-empty.
const char* module_name() noexcept;

}  // namespace txchain::chain
