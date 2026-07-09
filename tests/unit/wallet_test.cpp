// wallet.key format + keygen/load tests (Architecture Cryptography §10, Node/CLI
// §3.1). Golden pub/address come from a fixed seed via the RFC 8032 derivation,
// so the value is byte-identical on the sodium and ref10 backends — the
// cross-backend portability guarantee. Oracle: python3 cryptography + hashlib.
// Label: wallet.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <sys/stat.h>

#include "txchain/crypto/address.hpp"
#include "txchain/crypto/ed25519.hpp"
#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/crypto/hashutil.hpp"
#include "txchain/wallet/wallet.hpp"

namespace {

namespace fs = std::filesystem;

using txchain::crypto::Seed32;
using txchain::crypto::to_hex;
using txchain::wallet::LoadResult;
using txchain::wallet::Wallet;

// Fixed seed 0x01×32 and its RFC-8032-derived pubkey + SHA-256(pub)[:20] address
// (python oracle). Backend-independent: sodium and ref10 must both reproduce it.
constexpr const char* kSeedHex = "0101010101010101010101010101010101010101010101010101010101010101";
constexpr const char* kPubHex = "8a88e3dd7409f195fd52db2d3cba5d72ca6709bf1d94121bf3748801b40f6f5c";
constexpr const char* kAddrHex = "34750f98bd59fcfc946da45aaabe933be154a4b5";

Seed32 fixed_seed() {
  Seed32 s{};
  s.fill(0x01);
  return s;
}

std::string fresh_dir() {
  static int n = 0;
  const auto p = fs::temp_directory_path() / ("txchain_wallet_test_" + std::to_string(n++));
  std::error_code ec;
  fs::remove_all(p, ec);
  fs::create_directories(p, ec);
  return p.string();
}

void write_file(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::trunc);
  out << content;
}

std::string make_json(const std::string& priv, const std::string& pub, const std::string& addr) {
  return "{\"version\":1,\"priv\":\"" + priv + "\",\"pub\":\"" + pub + "\",\"address\":\"" + addr +
         "\"}\n";
}

unsigned file_mode(const std::string& path) {
  struct stat st {};
  EXPECT_EQ(::stat(path.c_str(), &st), 0);
  return st.st_mode & 0777u;
}

}  // namespace

// The derivation is real and backend-independent: a fixed seed yields the golden
// pubkey + address on every build (sodium and ref10 alike).
TEST(Wallet, GoldenDerivationFromFixedSeed) {
  const Seed32 seed = fixed_seed();
  const auto pub = txchain::crypto::derive_pubkey(seed);
  const auto addr = txchain::crypto::address(pub);
  EXPECT_EQ(to_hex(pub), kPubHex);
  EXPECT_EQ(to_hex(addr), kAddrHex);
}

// wallet_create() derives its own pubkey/address consistently (never stored blind).
TEST(Wallet, CreateHasConsistentDerivation) {
  const Wallet w = txchain::wallet::wallet_create();
  EXPECT_EQ(w.pubkey, txchain::crypto::derive_pubkey(w.seed));
  EXPECT_EQ(w.address, txchain::crypto::address(w.pubkey));
}

// Round-trip: save then load yields a byte-identical seed and the same address.
TEST(Wallet, SaveLoadRoundTrip) {
  const auto dir = fresh_dir();
  const auto path = txchain::wallet::wallet_path_for_datadir(dir);
  const Wallet w = txchain::wallet::wallet_create();
  ASSERT_TRUE(txchain::wallet::wallet_save(path, w));

  const LoadResult r = txchain::wallet::wallet_load(path);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.wallet.seed, w.seed);      // byte-identical persisted seed
  EXPECT_EQ(r.wallet.pubkey, w.pubkey);
  EXPECT_EQ(r.wallet.address, w.address);
  EXPECT_FALSE(r.world_readable);        // wallet_save forces 0600
}

// wallet_save writes exactly mode 0600 (private key material).
TEST(Wallet, SaveIsMode0600) {
  const auto dir = fresh_dir();
  const auto path = txchain::wallet::wallet_path_for_datadir(dir);
  ASSERT_TRUE(txchain::wallet::wallet_save(path, txchain::wallet::wallet_create()));
  EXPECT_EQ(file_mode(path), 0600u);
}

// A seed that is not exactly 32 bytes (here 31 bytes / 62 hex) is a hard reject.
TEST(Wallet, RejectsWrongLengthSeed) {
  const auto dir = fresh_dir();
  const auto path = txchain::wallet::wallet_path_for_datadir(dir);
  write_file(path, make_json(std::string(62, '0'), kPubHex, kAddrHex));  // 31-byte seed
  const LoadResult r = txchain::wallet::wallet_load(path);
  EXPECT_FALSE(r.ok);
}

// A stored address that disagrees with SHA-256(derive_pubkey(seed))[:20] refuses
// to start — recompute-and-assert, exactly like txid/blockHash on chain load.
TEST(Wallet, RejectsAddressMismatch) {
  const auto dir = fresh_dir();
  const auto path = txchain::wallet::wallet_path_for_datadir(dir);
  std::string bad_addr = kAddrHex;
  bad_addr[0] = (bad_addr[0] == '0') ? '1' : '0';  // flip one hex digit
  write_file(path, make_json(kSeedHex, kPubHex, bad_addr));
  const LoadResult r = txchain::wallet::wallet_load(path);
  EXPECT_FALSE(r.ok);
}

// A stored pubkey that disagrees with derive_pubkey(seed) refuses to start.
TEST(Wallet, RejectsPubMismatch) {
  const auto dir = fresh_dir();
  const auto path = txchain::wallet::wallet_path_for_datadir(dir);
  std::string bad_pub = kPubHex;
  bad_pub[0] = (bad_pub[0] == '0') ? '1' : '0';
  write_file(path, make_json(kSeedHex, bad_pub, kAddrHex));
  const LoadResult r = txchain::wallet::wallet_load(path);
  EXPECT_FALSE(r.ok);
}

// A minimal wallet.key carrying only the seed still loads — pubkey/address are
// derived (the stored copies are optional redundancy).
TEST(Wallet, RawSeedOnlyLoadsAndDerives) {
  const auto dir = fresh_dir();
  const auto path = txchain::wallet::wallet_path_for_datadir(dir);
  write_file(path, std::string("{\"version\":1,\"priv\":\"") + kSeedHex + "\"}\n");
  const LoadResult r = txchain::wallet::wallet_load(path);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(to_hex(r.wallet.address), kAddrHex);
}

// A world-readable wallet.key loads (warn, not fail) with the soft flag set.
TEST(Wallet, WorldReadableWarnsNotFails) {
  const auto dir = fresh_dir();
  const auto path = txchain::wallet::wallet_path_for_datadir(dir);
  ASSERT_TRUE(txchain::wallet::wallet_save(path, txchain::wallet::wallet_create()));
  ASSERT_EQ(::chmod(path.c_str(), 0644), 0);  // group/other readable

  const LoadResult r = txchain::wallet::wallet_load(path);
  EXPECT_TRUE(r.ok) << r.error;
  EXPECT_TRUE(r.world_readable);
}

// load_or_create_wallet initialises a fresh datadir, then loads the same key.
TEST(Wallet, LoadOrCreateInitialisesThenLoads) {
  const auto dir = fresh_dir();
  std::error_code ec;
  fs::remove_all(dir, ec);  // truly empty (no wallet.key yet)

  const LoadResult a = txchain::wallet::load_or_create_wallet(dir);
  ASSERT_TRUE(a.ok) << a.error;
  EXPECT_TRUE(fs::exists(txchain::wallet::wallet_path_for_datadir(dir)));

  const LoadResult b = txchain::wallet::load_or_create_wallet(dir);
  ASSERT_TRUE(b.ok) << b.error;
  EXPECT_EQ(a.wallet.seed, b.wallet.seed);        // second call loads, not re-creates
  EXPECT_EQ(a.wallet.address, b.wallet.address);
}
