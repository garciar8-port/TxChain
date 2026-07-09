// txchain — the TxChain v2 CLI (thin shim).
//
// Arg parsing / I/O glue only; all logic lives in txchain_core. Wired verbs:
//   monitor verify (--file <chain.jsonl> | --datadir <dir>)   [CRE-197/198]
//   wallet keygen  [--datadir D] [--out F]                    [CRE-200]
//   wallet address [--datadir D] [--key F]                    [CRE-200]
// send / tip / import land in later M2–M3 tickets.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "txchain/chain/store.hpp"
#include "txchain/chain/validate.hpp"
#include "txchain/config.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/wallet/wallet.hpp"

namespace {

// Return the value following `flag` in argv[start..], or "" if absent.
std::string opt(int argc, char** argv, int start, const char* flag) {
  for (int i = start; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
  return "";
}

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

// Resolve the wallet.key path: --out/--key wins, else <datadir>/wallet.key, else
// the default datadir. Returns {path, datadir-if-known}.
std::string wallet_path(int argc, char** argv, const char* file_flag) {
  const std::string f = opt(argc, argv, 3, file_flag);
  if (!f.empty()) return f;
  std::string dd = opt(argc, argv, 3, "--datadir");
  if (dd.empty()) dd = "./txchain-data";
  return txchain::wallet::wallet_path_for_datadir(dd);
}

// txchain wallet keygen [--datadir D] [--out F] — generate a keypair, write
// wallet.key (mode 0600), print the derived 40-hex address.
int run_wallet_keygen(int argc, char** argv) {
  const std::string path = wallet_path(argc, argv, "--out");
  std::error_code ec;
  const auto parent = std::filesystem::path(path).parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent, ec);

  txchain::wallet::Wallet w;
  try {
    w = txchain::wallet::wallet_create();  // throws if the CSPRNG is unavailable
  } catch (const std::exception& e) {
    std::fprintf(stderr, "wallet keygen: %s\n", e.what());
    return 1;
  }
  if (!txchain::wallet::wallet_save(path, w)) {
    std::fprintf(stderr, "wallet keygen: cannot write %s\n", path.c_str());
    return 1;
  }
  std::printf("%s\n", txchain::crypto::to_hex(w.address).c_str());
  return 0;
}

// txchain wallet address [--datadir D] [--key F] — load the seed and print the
// derived 40-hex address. Pure/offline (no node contact).
int run_wallet_address(int argc, char** argv) {
  const std::string path = wallet_path(argc, argv, "--key");
  const auto r = txchain::wallet::wallet_load(path);
  if (!r.ok) {
    std::fprintf(stderr, "wallet address: %s\n", r.error.c_str());
    return 1;
  }
  if (r.world_readable)
    std::fprintf(stderr, "warning: %s is group/other-accessible (expected mode 0600)\n",
                 path.c_str());
  std::printf("%s\n", txchain::crypto::to_hex(r.wallet.address).c_str());
  return 0;
}

int run_wallet(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[2], "keygen") == 0) return run_wallet_keygen(argc, argv);
  if (argc >= 3 && std::strcmp(argv[2], "address") == 0) return run_wallet_address(argc, argv);
  std::fprintf(stderr,
               "usage: txchain wallet (keygen [--datadir D] [--out F] | "
               "address [--datadir D] [--key F])\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], "monitor") == 0 && std::strcmp(argv[2], "verify") == 0) {
    return run_monitor_verify(argc, argv);
  }
  if (argc >= 2 && std::strcmp(argv[1], "wallet") == 0) {
    return run_wallet(argc, argv);
  }

  std::printf("txchain: TxChain v2 CLI (core module=%s).\n"
              "  monitor verify (--file <chain.jsonl> | --datadir <dir>)\n"
              "  wallet keygen  [--datadir D] [--out F]\n"
              "  wallet address [--datadir D] [--key F]\n"
              "  (send / tip / import land in later tickets)\n",
              txchain::config::module_name());
  return 0;
}
