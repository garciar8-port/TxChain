#pragma once
// p2p module — public umbrella header (txchain_core).
//
// The Phase-0 length-prefixed frame codec (the byte-boundary layer P2P depends
// on) lands here. The connection model, handshake, gossip, IBD, and reorg
// arrive in Pillar 4 (M4).

#include "txchain/net/frame.hpp"

namespace txchain::p2p {

// Scaffold marker (kept from CRE-188) so the module object is non-empty.
const char* module_name() noexcept;

}  // namespace txchain::p2p
