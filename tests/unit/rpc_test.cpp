// HTTP RPC surface (Architecture Node/CLI §4): the pure handle_request dispatch
// (POST /tx, GET /account/{addr}, GET /tip), RPC port derivation, and an
// in-process HTTP round-trip over real loopback sockets. Signed txns come from
// genesis identities. Label: rpc.

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "txchain/chain/genesis.hpp"
#include "txchain/chain/types.hpp"
#include "txchain/crypto/address.hpp"
#include "txchain/crypto/ed25519.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/crypto/sha256.hpp"
#include "txchain/mempool/mempool.hpp"
#include "txchain/node/node.hpp"
#include "txchain/rpc.hpp"
#include "txchain/serialize/canonical.hpp"

namespace {

using txchain::chain::AccountState;
using txchain::chain::Address;
using txchain::chain::applyGenesis;
using txchain::chain::genesisBlock;
using txchain::mempool::Mempool;
using txchain::serialize::Txn;
using txchain::crypto::to_hex;

txchain::crypto::Seed32 genesis_seed(const std::string& name) {
  const std::string s = "txchain-genesis-" + name;
  return txchain::crypto::sha256(txchain::crypto::ByteView(
      reinterpret_cast<const txchain::crypto::Byte*>(s.data()), s.size()));
}
Address addr_of(const std::string& name) {
  return txchain::crypto::address(txchain::crypto::derive_pubkey(genesis_seed(name)));
}
Txn signed_txn(const std::string& from, const Address& to, std::uint64_t amount,
               std::uint64_t nonce) {
  const auto seed = genesis_seed(from);
  Txn t;
  t.ver = txchain::serialize::kTxnVersion;
  t.pubkey = txchain::crypto::derive_pubkey(seed);
  t.from = txchain::crypto::address(t.pubkey);
  t.to = to;
  t.amount = amount;
  t.nonce = nonce;
  const auto p = txchain::serialize::signed_payload(t);
  t.sig = txchain::crypto::sign(seed, txchain::crypto::ByteView(p.data(), p.size()));
  return t;
}
std::string tx_hex(const Txn& t) { return to_hex(txchain::serialize::serialize(t)); }

txchain::rpc::TipInfo genesis_tip() {
  const auto g = genesisBlock();
  txchain::rpc::TipInfo t;
  t.index = 0;
  t.blockHash = g.header.hash();
  t.prevHash = g.header.prevHash;
  t.cumWork = 0;
  t.difficulty = 0;
  t.timestamp = g.header.timestamp;
  return t;
}

// A committed-state map + a mempool over it, plus the RpcContext they back.
struct Env {
  std::map<Address, AccountState> state;
  Mempool mp;
  Env()
      : state(gen_state()),
        mp([this](const Address& a) { return look(a); }) {}
  AccountState look(const Address& a) const {
    const auto it = state.find(a);
    return it == state.end() ? AccountState{} : it->second;
  }
  txchain::rpc::RpcContext ctx() {
    return txchain::rpc::RpcContext{[this](const Address& a) { return look(a); },
                                    []() { return genesis_tip(); }, mp};
  }
  static std::map<Address, AccountState> gen_state() {
    std::map<Address, AccountState> m;
    applyGenesis(m);
    return m;
  }
};

txchain::node::NodeConfig resolve_args(std::vector<std::string> args) {
  std::vector<char*> argv;
  for (auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
  return txchain::node::resolve_config(static_cast<int>(argv.size()), argv.data());
}

std::string http_roundtrip(int port, const std::string& raw) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return "";
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<std::uint16_t>(port));
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
    ::close(fd);
    return "";
  }
  ::write(fd, raw.data(), raw.size());
  std::string resp;
  char buf[4096];
  ssize_t n;
  while ((n = ::read(fd, buf, sizeof(buf))) > 0) resp.append(buf, static_cast<std::size_t>(n));
  ::close(fd);
  return resp;
}

bool has(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

}  // namespace

// ---- pure handle_request dispatch ----

TEST(RpcHandler, PostTxValidReturnsTxid) {
  Env env;
  auto c = env.ctx();
  const Txn t = signed_txn("racheal", addr_of("oliver"), 100, 0);
  const auto r = txchain::rpc::handle_request("POST", "/tx", tx_hex(t), c);
  EXPECT_EQ(r.status, 200);
  EXPECT_TRUE(has(r.body, to_hex(txchain::serialize::txid(t))));
  EXPECT_EQ(env.mp.size(), 1u);  // admitted
}

TEST(RpcHandler, PostTxBadSig) {
  Env env;
  auto c = env.ctx();
  Txn t = signed_txn("racheal", addr_of("oliver"), 100, 0);
  t.sig[0] ^= 0x01;
  const auto r = txchain::rpc::handle_request("POST", "/tx", tx_hex(t), c);
  EXPECT_EQ(r.status, 400);
  EXPECT_TRUE(has(r.body, "BAD_SIG"));
}

TEST(RpcHandler, PostTxWrongAddr) {
  Env env;
  auto c = env.ctx();
  Txn t = signed_txn("racheal", addr_of("oliver"), 100, 0);
  t.from = addr_of("oliver");  // from != address(pubkey)
  const auto r = txchain::rpc::handle_request("POST", "/tx", tx_hex(t), c);
  EXPECT_EQ(r.status, 400);
  EXPECT_TRUE(has(r.body, "BAD_ADDR"));
}

TEST(RpcHandler, PostTxReplayIs409StaleNonce) {
  Env env;
  auto c = env.ctx();
  const Txn t = signed_txn("racheal", addr_of("oliver"), 100, 0);
  ASSERT_EQ(txchain::rpc::handle_request("POST", "/tx", tx_hex(t), c).status, 200);
  // Advance the committed nonce so the re-POST is a stale (below-expected) nonce,
  // and clear the mempool slot so DUPLICATE/PENDING don't mask it.
  env.state[addr_of("racheal")] = AccountState{900, 1};
  env.mp.evictIncluded({t});
  const auto r = txchain::rpc::handle_request("POST", "/tx", tx_hex(t), c);
  EXPECT_EQ(r.status, 409);  // the demoable "replay rejected" moment
  EXPECT_TRUE(has(r.body, "STALE_NONCE"));
}

TEST(RpcHandler, PostTxParseErrorNotConsensusReason) {
  Env env;
  auto c = env.ctx();
  const auto r = txchain::rpc::handle_request("POST", "/tx", "not-hex-and-wrong-length", c);
  EXPECT_EQ(r.status, 400);
  EXPECT_TRUE(has(r.body, "PARSE_ERROR"));
}

TEST(RpcHandler, AccountGenesisFunded) {
  Env env;
  auto c = env.ctx();
  const auto r = txchain::rpc::handle_request("GET", "/account/" + to_hex(addr_of("racheal")), "", c);
  EXPECT_EQ(r.status, 200);
  EXPECT_TRUE(has(r.body, "\"balance\":1000"));
  EXPECT_TRUE(has(r.body, "\"confirmedNonce\":0"));
  EXPECT_TRUE(has(r.body, "\"pendingNonce\":0"));
}

TEST(RpcHandler, AccountPendingNonceAfterAdmit) {
  Env env;
  auto c = env.ctx();
  ASSERT_EQ(txchain::rpc::handle_request("POST", "/tx",
                                         tx_hex(signed_txn("racheal", addr_of("oliver"), 100, 0)), c)
                .status,
            200);
  const auto r = txchain::rpc::handle_request("GET", "/account/" + to_hex(addr_of("racheal")), "", c);
  EXPECT_TRUE(has(r.body, "\"confirmedNonce\":0"));
  EXPECT_TRUE(has(r.body, "\"pendingNonce\":1"));  // unmined slot occupied
}

TEST(RpcHandler, AccountUnseenIsZero) {
  Env env;
  auto c = env.ctx();
  Address never{};
  never.fill(0xAB);
  const auto r = txchain::rpc::handle_request("GET", "/account/" + to_hex(never), "", c);
  EXPECT_EQ(r.status, 200);
  EXPECT_TRUE(has(r.body, "\"balance\":0"));
  EXPECT_TRUE(has(r.body, "\"confirmedNonce\":0"));
  EXPECT_TRUE(has(r.body, "\"pendingNonce\":0"));
}

TEST(RpcHandler, AccountBadHexIsBadRequest) {
  Env env;
  auto c = env.ctx();
  const auto r = txchain::rpc::handle_request("GET", "/account/zzzz", "", c);
  EXPECT_EQ(r.status, 400);
  EXPECT_TRUE(has(r.body, "BAD_REQUEST"));
}

TEST(RpcHandler, TipReportsGenesis) {
  Env env;
  auto c = env.ctx();
  const auto r = txchain::rpc::handle_request("GET", "/tip", "", c);
  EXPECT_EQ(r.status, 200);
  EXPECT_TRUE(has(r.body, "\"index\":0"));
  EXPECT_TRUE(has(r.body, to_hex(genesisBlock().header.hash())));
  EXPECT_TRUE(has(r.body, "\"difficulty\":0"));
}

TEST(RpcHandler, UnknownRouteIs404) {
  Env env;
  auto c = env.ctx();
  EXPECT_EQ(txchain::rpc::handle_request("GET", "/chain", "", c).status, 404);
}

// ---- RPC port derivation (Node/CLI §1.2) ----

TEST(RpcPort, DerivesFromNodeIndex) {
  EXPECT_EQ(txchain::node::rpc_port_for(resolve_args({"txnode", "--node-index", "3"})), 30003);
  EXPECT_EQ(txchain::node::rpc_port_for(resolve_args({"txnode"})), 30000);
}
TEST(RpcPort, RpcPortOverrides) {
  EXPECT_EQ(txchain::node::rpc_port_for(resolve_args({"txnode", "--node-index", "3", "--rpc-port",
                                                      "40000"})),
            40000);
}

// ---- in-process HTTP round-trip over real loopback sockets ----

TEST(RpcHttp, PostTxAndTipOverHttp) {
  Env env;
  std::mutex mtx;
  auto c = env.ctx();
  txchain::rpc::HttpServer srv(0, [&](const std::string& m, const std::string& t,
                                      const std::string& b) {
    std::lock_guard<std::mutex> lk(mtx);
    return txchain::rpc::handle_request(m, t, b, c);
  });
  ASSERT_TRUE(srv.start());
  const int port = srv.port();
  std::thread th([&srv]() { srv.run(); });

  const Txn tx = signed_txn("racheal", addr_of("oliver"), 100, 0);
  const std::string hex = tx_hex(tx);
  const std::string post = "POST /tx HTTP/1.1\r\nHost: x\r\nContent-Length: " +
                           std::to_string(hex.size()) + "\r\n\r\n" + hex;
  const std::string post_resp = http_roundtrip(port, post);
  EXPECT_TRUE(has(post_resp, "200 OK"));
  EXPECT_TRUE(has(post_resp, to_hex(txchain::serialize::txid(tx))));

  const std::string tip_resp = http_roundtrip(port, "GET /tip HTTP/1.1\r\nHost: x\r\n\r\n");
  EXPECT_TRUE(has(tip_resp, "200 OK"));
  EXPECT_TRUE(has(tip_resp, "\"index\":0"));

  const std::string acct_resp =
      http_roundtrip(port, "GET /account/" + to_hex(addr_of("racheal")) + " HTTP/1.1\r\nHost: x\r\n\r\n");
  EXPECT_TRUE(has(acct_resp, "\"balance\":1000"));
  EXPECT_TRUE(has(acct_resp, "\"pendingNonce\":1"));  // the POST above admitted it

  srv.stop();
  th.join();
}
