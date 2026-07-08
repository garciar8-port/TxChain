// Ed25519 known-answer tests against the canonical djb/RFC-8032 vectors
// (independently cross-verified vs OpenSSL at generation time — see
// ed25519_vectors.hpp). Runs against whichever backend the facade was compiled
// with (ref10 or sodium); passing on both legs proves byte-identity. Label: crypto.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "ed25519_vectors.hpp"
#include "txchain/crypto/ed25519.hpp"
#include "txchain/crypto/fixedbytes.hpp"

namespace {

using txchain::crypto::Byte;
using txchain::crypto::ByteView;
using txchain::crypto::PubKey32;
using txchain::crypto::Seed32;
using txchain::crypto::Sig64;

int nib(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

std::vector<Byte> unhex(std::string_view h) {
  std::vector<Byte> o;
  o.reserve(h.size() / 2);
  for (std::size_t i = 0; i + 1 < h.size(); i += 2)
    o.push_back(static_cast<Byte>((nib(h[i]) << 4) | nib(h[i + 1])));
  return o;
}

template <std::size_t N>
std::array<Byte, N> unhexN(std::string_view h) {
  std::array<Byte, N> a{};
  const auto b = unhex(h);
  for (std::size_t i = 0; i < N && i < b.size(); ++i) a[i] = b[i];
  return a;
}

template <std::size_t N>
std::string hexN(const std::array<Byte, N>& a) {
  static const char* H = "0123456789abcdef";
  std::string s;
  s.reserve(2 * N);
  for (Byte b : a) {
    s.push_back(H[b >> 4]);
    s.push_back(H[b & 0x0F]);
  }
  return s;
}

ByteView msgview(const std::vector<Byte>& m) { return ByteView(m.data(), m.size()); }

}  // namespace

// Every canonical vector: derive_pubkey == expected pub, sign == expected sig
// (deterministic), and verify() accepts.
TEST(Ed25519Rfc8032, CanonicalVectorsMatch) {
  ASSERT_FALSE(txchain::crypto::test::kEd25519Vectors.empty());
  for (const auto& v : txchain::crypto::test::kEd25519Vectors) {
    const Seed32 seed = unhexN<32>(v.seed);
    const auto msg = unhex(v.msg);

    const PubKey32 pub = txchain::crypto::derive_pubkey(seed);
    EXPECT_EQ(hexN(pub), std::string(v.pubkey)) << "pubkey for seed " << v.seed;

    const Sig64 sig = txchain::crypto::sign(seed, msgview(msg));
    EXPECT_EQ(hexN(sig), std::string(v.sig)) << "sig for seed " << v.seed;

    EXPECT_TRUE(txchain::crypto::verify(pub, sig, msgview(msg)));
  }
}

TEST(Ed25519Rfc8032, TamperedMessageRejected) {
  const auto& v = txchain::crypto::test::kEd25519Vectors[1];  // 1-byte message
  const Seed32 seed = unhexN<32>(v.seed);
  const PubKey32 pub = txchain::crypto::derive_pubkey(seed);
  auto msg = unhex(v.msg);
  const Sig64 sig = txchain::crypto::sign(seed, msgview(msg));
  msg[0] ^= 0x01;  // flip a message bit
  EXPECT_FALSE(txchain::crypto::verify(pub, sig, msgview(msg)));
}

TEST(Ed25519Rfc8032, TamperedSignatureRejected) {
  const auto& v = txchain::crypto::test::kEd25519Vectors[2];
  const Seed32 seed = unhexN<32>(v.seed);
  const PubKey32 pub = txchain::crypto::derive_pubkey(seed);
  const auto msg = unhex(v.msg);
  Sig64 sig = txchain::crypto::sign(seed, msgview(msg));
  sig[0] ^= 0x01;  // flip a signature bit
  EXPECT_FALSE(txchain::crypto::verify(pub, sig, msgview(msg)));
}

TEST(Ed25519Rfc8032, WrongKeyRejected) {
  const auto& v0 = txchain::crypto::test::kEd25519Vectors[0];
  const auto& v1 = txchain::crypto::test::kEd25519Vectors[1];
  const Seed32 seed0 = unhexN<32>(v0.seed);
  const PubKey32 wrongPub = txchain::crypto::derive_pubkey(unhexN<32>(v1.seed));
  const auto msg = unhex(v0.msg);
  const Sig64 sig = txchain::crypto::sign(seed0, msgview(msg));
  EXPECT_FALSE(txchain::crypto::verify(wrongPub, sig, msgview(msg)));
}

TEST(Ed25519Rfc8032, KeygenFromSeedIsDeterministic) {
  const auto& v = txchain::crypto::test::kEd25519Vectors[0];
  const Seed32 seed = unhexN<32>(v.seed);
  const auto a = txchain::crypto::keygen_from_seed(seed);
  const auto b = txchain::crypto::keygen_from_seed(seed);
  EXPECT_EQ(hexN(a.pubkey), hexN(b.pubkey));
  EXPECT_EQ(hexN(a.pubkey), std::string(v.pubkey));
}

TEST(Ed25519Rfc8032, KeygenRandomRoundTrips) {
  const auto kp = txchain::crypto::keygen();  // OS CSPRNG
  const std::string m = "hello txchain v2";
  const ByteView mv(reinterpret_cast<const Byte*>(m.data()), m.size());
  const Sig64 sig = txchain::crypto::sign(kp.seed, mv);
  EXPECT_TRUE(txchain::crypto::verify(kp.pubkey, sig, mv));
  EXPECT_EQ(hexN(txchain::crypto::derive_pubkey(kp.seed)), hexN(kp.pubkey));
}
