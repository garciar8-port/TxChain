// txchain send end-to-end (Architecture Node/CLI §6, Data Flow §A): the pure
// signed-txn builder (wallet::sign_txn), the four M2 forgery/tamper/replay
// rejections through the shared gate, the mempool-drain seal that mines the sent
// txn, offline next-nonce reconstruction, and an HTTP client round-trip against
// the in-process server. Golden payload is Data Model Vector A. Label: send.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "txchain/chain/genesis.hpp"
#include "txchain/chain/store.hpp"
#include "txchain/chain/types.hpp"
#include "txchain/chain/validate.hpp"
#include "txchain/crypto/address.hpp"
#include "txchain/crypto/ed25519.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/crypto/sha256.hpp"
#include "txchain/mempool/mempool.hpp"
#include "txchain/node/node.hpp"
#include "txchain/rpc.hpp"
#include "txchain/serialize/canonical.hpp"
#include "txchain/wallet/wallet.hpp"

namespace {

namespace fs = std::filesystem;

using txchain::chain::AccountState;
using txchain::chain::Address;
using txchain::chain::applyGenesis;
using txchain::mempool::AdmitResult;
using txchain::mempool::Mempool;
using txchain::serialize::Txn;
using txchain::crypto::to_hex;

constexpr std::uint64_t NOW = 2000000000;

txchain::crypto::Seed32 genesis_seed(const std::string& name) {
  const std::string s = "txchain-genesis-" + name;
  return txchain::crypto::sha256(txchain::crypto::ByteView(
      reinterpret_cast<const txchain::crypto::Byte*>(s.data()), s.size()));
}
Address addr_of(const std::string& name) {
  return txchain::crypto::address(txchain::crypto::derive_pubkey(genesis_seed(name)));
}

std::string fresh_datadir() {
  static int n = 0;
  const auto p = fs::temp_directory_path() / ("txchain_send_test_" + std::to_string(n++));
  std::error_code ec;
  fs::remove_all(p, ec);
  return p.string();
}

Mempool::AccountLookup genesis_lookup(std::map<Address, AccountState>& st) {
  applyGenesis(st);
  return [&st](const Address& a) -> AccountState {
    const auto it = st.find(a);
    return it == st.end() ? AccountState{} : it->second;
  };
}

}  // namespace

// Data Model Vector A: the 89-byte signed payload for a fixed (from,to,amount,
// nonce,pubkey) serializes byte-for-byte to the frozen golden.
TEST(Send, GoldenPayloadVectorA) {
  Txn t;
  t.ver = 0x01;
  ASSERT_TRUE(txchain::crypto::from_hex<20>("75877bb41d393b5fb8455ce60ecd8dda001d0631", t.from));
  ASSERT_TRUE(txchain::crypto::from_hex<20>("aabbccddeeff00112233445566778899aabbccdd", t.to));
  t.amount = 100;
  t.nonce = 0;
  t.pubkey.fill(0x02);
  const auto payload = txchain::serialize::signed_payload(t);
  EXPECT_EQ(to_hex(payload),
            "0175877bb41d393b5fb8455ce60ecd8dda001d0631aabbccddeeff00112233445566778899aabbccdd"
            "000000000000006400000000000000000202020202020202020202020202020202020202020202020202"
            "020202020202");
}

// wallet::sign_txn produces a txn that admits, and is deterministic (same inputs
// ⇒ byte-identical txn — the basis of the --datadir/--node parity criterion).
TEST(Send, SignTxnAdmitsAndIsDeterministic) {
  const auto seed = genesis_seed("racheal");
  const Txn a = txchain::wallet::sign_txn(seed, addr_of("oliver"), 100, 0);
  const Txn b = txchain::wallet::sign_txn(seed, addr_of("oliver"), 100, 0);
  EXPECT_EQ(txchain::serialize::serialize(a), txchain::serialize::serialize(b));  // deterministic

  std::map<Address, AccountState> st;
  Mempool mp(genesis_lookup(st));
  EXPECT_EQ(mp.admit(a), AdmitResult::OK);
}

// Forged key: sig produced by a key other than the claimed pubkey ⇒ BAD_SIG.
TEST(Send, ForgedKeyBadSig) {
  Txn t = txchain::wallet::sign_txn(genesis_seed("racheal"), addr_of("oliver"), 100, 0);
  const auto payload = txchain::serialize::signed_payload(t);
  t.sig = txchain::crypto::sign(genesis_seed("oliver"),  // wrong signer
                                txchain::crypto::ByteView(payload.data(), payload.size()));
  std::map<Address, AccountState> st;
  Mempool mp(genesis_lookup(st));
  EXPECT_EQ(mp.admit(t), AdmitResult::BAD_SIG);
}

// Tampered field: mutate amount after signing ⇒ BAD_SIG (sig no longer covers it).
TEST(Send, TamperedAmountBadSig) {
  Txn t = txchain::wallet::sign_txn(genesis_seed("racheal"), addr_of("oliver"), 100, 0);
  t.amount = 101;  // signature was over amount = 100
  std::map<Address, AccountState> st;
  Mempool mp(genesis_lookup(st));
  EXPECT_EQ(mp.admit(t), AdmitResult::BAD_SIG);
}

// Wrong address: from != SHA-256(pubkey)[:20] ⇒ BAD_ADDR, before signature check.
TEST(Send, WrongAddressBadAddr) {
  Txn t = txchain::wallet::sign_txn(genesis_seed("racheal"), addr_of("oliver"), 100, 0);
  t.from = addr_of("matthew");  // not derived from t.pubkey
  std::map<Address, AccountState> st;
  Mempool mp(genesis_lookup(st));
  EXPECT_EQ(mp.admit(t), AdmitResult::BAD_ADDR);
}

// The mempool-drain seal mines the sent txn: recipient balance +amount, sender
// nonce +1 — the "valid send succeeds once" criterion.
TEST(Send, DrainSealMinesTxnAndUpdatesBalances) {
  const auto dir = fresh_datadir();
  txchain::node::NodeConfig cfg;
  cfg.datadir = dir;
  txchain::node::BootResult br;
  txchain::node::Node nd = txchain::node::Node::boot(cfg, NOW, br);
  ASSERT_TRUE(br.ok);

  const Txn t = txchain::wallet::sign_txn(genesis_seed("racheal"), addr_of("oliver"), 100, 0);
  ASSERT_EQ(nd.seal_next_block(NOW, {t}), txchain::chain::Reason::OK);
  EXPECT_EQ(nd.height(), 1u);
  EXPECT_EQ(nd.chain().account(addr_of("racheal")).balance, 900u);
  EXPECT_EQ(nd.chain().account(addr_of("racheal")).nonce, 1u);
  EXPECT_EQ(nd.chain().account(addr_of("oliver")).balance, 1100u);

  // Replay of the now-committed txn is STALE_NONCE (nonce 0 < committed 1).
  std::map<Address, AccountState> st;  // unused lookup base; use the node's chain
  Mempool mp([&nd](const Address& a) { return nd.chain().account(a); });
  EXPECT_EQ(mp.admit(t), AdmitResult::STALE_NONCE);

  // monitor-verify contract: the sealed chain replays OK.
  const auto lr = txchain::chain::ChainStore::forDatadir(dir).load();
  ASSERT_TRUE(lr.status.ok);
  const auto vr = txchain::chain::replayFromGenesis(lr.blocks, NOW);
  EXPECT_TRUE(vr.ok);
  EXPECT_EQ(txchain::chain::monitor_verify_line(vr, lr.blocks.size()), "OK 2 blocks");
}

// Offline next-nonce reconstruction (the --datadir path): replayState advances
// the sender's nonce after the txn is committed.
TEST(Send, OfflineReplayStateNonce) {
  const auto dir = fresh_datadir();
  txchain::node::NodeConfig cfg;
  cfg.datadir = dir;
  txchain::node::BootResult br;
  txchain::node::Node nd = txchain::node::Node::boot(cfg, NOW, br);
  ASSERT_TRUE(br.ok);
  ASSERT_EQ(nd.seal_next_block(
                NOW, {txchain::wallet::sign_txn(genesis_seed("racheal"), addr_of("oliver"), 100, 0)}),
            txchain::chain::Reason::OK);

  const auto lr = txchain::chain::ChainStore::forDatadir(dir).load();
  ASSERT_TRUE(lr.status.ok);
  const auto state = txchain::chain::replayState(lr.blocks);
  EXPECT_EQ(state.at(addr_of("racheal")).nonce, 1u);   // next nonce for the sender
  EXPECT_EQ(state.at(addr_of("oliver")).balance, 1100u);
}

// The HTTP client (send's transport): GET /account + POST /tx round-trip against
// the in-process server, with JSON field extraction.
TEST(Send, HttpClientAccountAndSubmit) {
  std::map<Address, AccountState> st;
  Mempool mp(genesis_lookup(st));
  std::mutex mtx;
  txchain::rpc::RpcContext ctx{[&st](const Address& a) -> AccountState {
                                 const auto it = st.find(a);
                                 return it == st.end() ? AccountState{} : it->second;
                               },
                               []() { return txchain::rpc::TipInfo{}; }, mp};
  txchain::rpc::HttpServer srv(0, [&](const std::string& m, const std::string& t,
                                      const std::string& b) {
    std::lock_guard<std::mutex> lk(mtx);
    return txchain::rpc::handle_request(m, t, b, ctx);
  });
  ASSERT_TRUE(srv.start());
  const int port = srv.port();
  std::thread th([&srv]() { srv.run(); });

  const auto acct = txchain::rpc::http_get("127.0.0.1", port, "/account/" + to_hex(addr_of("racheal")));
  EXPECT_TRUE(acct.ok);
  EXPECT_EQ(acct.status, 200);
  EXPECT_EQ(txchain::rpc::json_get_u64(acct.body, "balance").value_or(0), 1000u);
  EXPECT_EQ(txchain::rpc::json_get_u64(acct.body, "pendingNonce").value_or(999u), 0u);

  const Txn tx = txchain::wallet::sign_txn(genesis_seed("racheal"), addr_of("oliver"), 100, 0);
  const auto post = txchain::rpc::http_post("127.0.0.1", port, "/tx",
                                            to_hex(txchain::serialize::serialize(tx)));
  EXPECT_TRUE(post.ok);
  EXPECT_EQ(post.status, 200);
  EXPECT_EQ(txchain::rpc::json_get_string(post.body, "txid").value_or(""),
            to_hex(txchain::serialize::txid(tx)));

  srv.stop();
  th.join();
}
