#pragma once
// crypto module — public umbrella header (txchain_core).
//
// Aggregates the cryptographic trust root's public API. SHA-256 + the
// fixed-width byte types are wired (CRE-189); Ed25519, address derivation, the
// hex codec, and PoW helpers arrive in CRE-190 / CRE-191. This module depends on
// no sibling module (acyclic, bottom-up).

#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/crypto/sha256.hpp"

namespace txchain::crypto {

// Returns the module's name. Exists solely so the object is non-empty and the
// symbol can be linked from tests, proving the library boundary is wired.
const char* module_name() noexcept;

}  // namespace txchain::crypto
