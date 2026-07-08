// txnode — the TxChain v2 node process (thin shim).
//
// Phase-0 scaffold (CRE-188): argument parsing, process wiring, and I/O glue
// only. All ledger logic lives in txchain_core. The real node loop (datadir,
// P2P/RPC ports, --mine / --seal-cadence / --peers / --difficulty) is wired in
// later M1–M4 tickets (CRE-195+).

#include <cstdio>

#include "txchain/config.hpp"

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  std::printf("txnode: TxChain v2 node (scaffold, core module=%s). "
              "Not yet implemented — see CRE-195+.\n",
              txchain::config::module_name());
  return 0;
}
