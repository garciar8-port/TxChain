// txchain — the TxChain v2 CLI (thin shim).
//
// Arg parsing / I/O glue only; all logic lives in txchain_core. The `monitor
// verify` subcommand is wired now that the chain.jsonl loader (CRE-198) and the
// replay validator (CRE-197) exist. wallet / send / tip / import land in later
// M2–M3 tickets.

#include <cstdio>
#include <cstring>
#include <string>

#include "txchain/chain/store.hpp"
#include "txchain/chain/validate.hpp"
#include "txchain/config.hpp"

namespace {

// txchain monitor verify (--file <chain.jsonl> | --datadir <dir>)
// Prints "OK <N> blocks" or "FAIL block <idx>: <REASON> (<detail>)" and returns
// 0 on OK, non-zero on any failure — so scripts / CI can gate on it.
int run_monitor_verify(int argc, char** argv) {
  std::string path;
  bool is_datadir = false;
  for (int i = 3; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--file") == 0) {
      path = argv[i + 1];
      is_datadir = false;
    } else if (std::strcmp(argv[i], "--datadir") == 0) {
      path = argv[i + 1];
      is_datadir = true;
    }
  }
  if (path.empty()) {
    std::fprintf(stderr,
                 "usage: txchain monitor verify (--file <chain.jsonl> | --datadir <dir>)\n");
    return 2;
  }

  const txchain::chain::ChainStore store =
      is_datadir ? txchain::chain::ChainStore::forDatadir(path)
                 : txchain::chain::ChainStore(path);

  const auto loaded = store.load();
  if (!loaded.status.ok) {
    std::printf("%s\n",
                txchain::chain::monitor_verify_line(loaded.status, loaded.blocks.size()).c_str());
    return txchain::chain::monitor_verify_exit_code(loaded.status);
  }

  const auto result = txchain::chain::replayFromGenesis(loaded.blocks);
  std::printf("%s\n",
              txchain::chain::monitor_verify_line(result, loaded.blocks.size()).c_str());
  return txchain::chain::monitor_verify_exit_code(result);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], "monitor") == 0 && std::strcmp(argv[2], "verify") == 0) {
    return run_monitor_verify(argc, argv);
  }

  std::printf("txchain: TxChain v2 CLI (core module=%s).\n"
              "  monitor verify (--file <chain.jsonl> | --datadir <dir>)\n"
              "  (wallet / send / tip / import land in later tickets)\n",
              txchain::config::module_name());
  return 0;
}
