#pragma once
// mempool module — public interface (txchain_core).
//
// Phase-0 scaffold (CRE-188): this header only exposes a scaffold marker that
// proves the module compiles and links into the txchain_core static library.
// The real mempool API lands in a later M0/pillar ticket. This module has no
// dependency on any sibling module at this stage (acyclic, bottom-up).

namespace txchain::mempool {

// Returns the module's name. Exists solely so the object is non-empty and the
// symbol can be linked from tests, proving the library boundary is wired.
const char* module_name() noexcept;

}  // namespace txchain::mempool
