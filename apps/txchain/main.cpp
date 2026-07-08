// txchain — the TxChain v2 CLI (thin shim).
//
// Phase-0 scaffold (CRE-188): argument parsing and I/O glue only. All logic
// lives in txchain_core. Subcommands (wallet keygen|address, node, send,
// monitor verify, tip, import) are wired in later M1–M3 tickets (CRE-200+).

#include <cstdio>

#include "txchain/config.hpp"

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  std::printf("txchain: TxChain v2 CLI (scaffold, core module=%s). "
              "Not yet implemented — see CRE-200+.\n",
              txchain::config::module_name());
  return 0;
}
