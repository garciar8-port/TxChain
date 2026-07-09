// txchain — the TxChain v2 CLI (thin shim).
//
// Arg parsing / I/O glue only; all logic lives in txchain_core. Wired verbs:
//   monitor verify (--file <chain.jsonl> | --datadir <dir>)   [CRE-197/198]
//   wallet keygen  [--datadir D] [--out F]                    [CRE-200]
//   wallet address [--datadir D] [--key F]                    [CRE-200]
//   send --to <40hex> --amount <u64> [--node host:port] ...   [CRE-204]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "txchain/chain/store.hpp"
#include "txchain/chain/validate.hpp"
#include "txchain/config.hpp"
#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/rpc.hpp"
#include "txchain/serialize/canonical.hpp"
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

// Split "host:port" on the last colon. Returns false if malformed.
bool parse_node(const std::string& s, std::string& host, int& port) {
  const auto colon = s.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= s.size()) return false;
  host = s.substr(0, colon);
  port = std::atoi(s.substr(colon + 1).c_str());
  return port > 0;
}

// txchain send --to <40hex> --amount <u64> [--node host:port] [--datadir D | --key F]
// Loads the wallet, resolves the next nonce (via GET /account in --node mode, or
// an offline replay in --datadir mode), builds+signs the canonical txn, and (with
// --node) POSTs it to /tx. Prints the txid on success; on rejection prints the
// reason token to stderr and exits non-zero.
int run_send(int argc, char** argv) {
  const std::string to_arg = opt(argc, argv, 2, "--to");
  const std::string amount_arg = opt(argc, argv, 2, "--amount");
  const std::string node = opt(argc, argv, 2, "--node");
  const std::string keyf = opt(argc, argv, 2, "--key");
  std::string datadir = opt(argc, argv, 2, "--datadir");
  if (datadir.empty()) datadir = "./txchain-data";
  if (to_arg.empty() || amount_arg.empty()) {
    std::fprintf(stderr,
                 "usage: txchain send --to <40hex> --amount <u64> "
                 "[--node host:port] [--datadir D | --key F]\n");
    return 2;
  }
  txchain::crypto::Address20 to{};
  if (!txchain::crypto::from_hex<20>(to_arg, to)) {
    std::fprintf(stderr, "send: --to must be 40 hex chars\n");
    return 2;
  }
  const std::uint64_t amount = std::strtoull(amount_arg.c_str(), nullptr, 10);

  // Load the wallet → seed / from address.
  const std::string wpath =
      !keyf.empty() ? keyf : txchain::wallet::wallet_path_for_datadir(datadir);
  const auto wl = txchain::wallet::wallet_load(wpath);
  if (!wl.ok) {
    std::fprintf(stderr, "send: %s\n", wl.error.c_str());
    return 1;
  }
  const std::string from_addr_hex = txchain::crypto::to_hex(wl.wallet.address);

  std::string host;
  int port = 0;
  const bool have_node = !node.empty() && parse_node(node, host, port);
  if (!node.empty() && !have_node) {
    std::fprintf(stderr, "send: bad --node (want host:port)\n");
    return 2;
  }

  // Resolve the next-expected nonce.
  std::uint64_t nonce = 0;
  if (have_node) {
    const auto r = txchain::rpc::http_get(host, port, "/account/" + from_addr_hex);
    if (!r.ok || r.status != 200) {
      std::fprintf(stderr, "send: cannot read /account from node\n");
      return 1;
    }
    const auto pn = txchain::rpc::json_get_u64(r.body, "pendingNonce");
    if (!pn) {
      std::fprintf(stderr, "send: malformed /account response\n");
      return 1;
    }
    nonce = *pn;
  } else {
    // Offline: reconstruct the next nonce from the datadir chain. A missing/empty
    // chain.jsonl means genesis-only state (nonce 0 for a genesis identity).
    const auto lr = txchain::chain::ChainStore::forDatadir(datadir).load();
    const auto& blocks = lr.blocks;
    if (lr.status.ok && !blocks.empty() && !txchain::chain::replayFromGenesis(blocks).ok) {
      std::fprintf(stderr, "send: datadir chain fails replay\n");
      return 1;
    }
    const auto state = txchain::chain::replayState(blocks);
    const auto it = state.find(wl.wallet.address);
    nonce = it == state.end() ? 0 : it->second.nonce;
  }

  // Build + sign the canonical txn (single serializer, real Ed25519).
  const auto tx = txchain::wallet::sign_txn(wl.wallet.seed, to, amount, nonce);
  const auto bytes = txchain::serialize::serialize(tx);
  const std::string body = txchain::crypto::to_hex(bytes);
  const std::string txid = txchain::crypto::to_hex(txchain::serialize::txid(tx));

  if (!have_node) {
    // Offline produce-only (no node to submit to): emit the signed bytes + txid.
    std::printf("{\"txid\":\"%s\",\"nonce\":%llu,\"raw\":\"%s\"}\n", txid.c_str(),
                static_cast<unsigned long long>(nonce), body.c_str());
    return 0;
  }

  const auto pr = txchain::rpc::http_post(host, port, "/tx", body);
  if (!pr.ok) {
    std::fprintf(stderr, "send: cannot reach node at %s:%d\n", host.c_str(), port);
    return 1;
  }
  if (pr.status / 100 == 2) {
    const auto id = txchain::rpc::json_get_string(pr.body, "txid");
    std::printf("%s\n", id ? id->c_str() : txid.c_str());
    return 0;
  }
  const auto errtok = txchain::rpc::json_get_string(pr.body, "error");
  std::fprintf(stderr, "send rejected: %s\n", errtok ? errtok->c_str() : pr.body.c_str());
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], "monitor") == 0 && std::strcmp(argv[2], "verify") == 0) {
    return run_monitor_verify(argc, argv);
  }
  if (argc >= 2 && std::strcmp(argv[1], "wallet") == 0) {
    return run_wallet(argc, argv);
  }
  if (argc >= 2 && std::strcmp(argv[1], "send") == 0) {
    return run_send(argc, argv);
  }

  std::printf("txchain: TxChain v2 CLI (core module=%s).\n"
              "  monitor verify (--file <chain.jsonl> | --datadir <dir>)\n"
              "  wallet keygen  [--datadir D] [--out F]\n"
              "  wallet address [--datadir D] [--key F]\n"
              "  send --to <40hex> --amount <u64> [--node host:port] [--datadir D | --key F]\n"
              "  (tip / import land in later tickets)\n",
              txchain::config::module_name());
  return 0;
}
