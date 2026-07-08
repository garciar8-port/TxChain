// Cross-backend equivalence: the vendored ref10 and libsodium must produce
// byte-identical pubkeys and signatures for the same (seed, msg), and must
// cross-verify each other's signatures (Architecture §5, §9). This compiles the
// comparison only when libsodium is available (TXCHAIN_HAVE_SODIUM); otherwise
// it self-skips. It calls BOTH raw implementations directly (not the facade,
// which only carries one). Label: crypto.

#include <gtest/gtest.h>

#if defined(TXCHAIN_HAVE_SODIUM) && TXCHAIN_HAVE_SODIUM

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <sodium.h>

extern "C" {
#include "ed25519.h"  // vendored orlp ref10
}

namespace {

std::string hx(const unsigned char* p, std::size_t n) {
  static const char* H = "0123456789abcdef";
  std::string s;
  s.reserve(2 * n);
  for (std::size_t i = 0; i < n; ++i) {
    s.push_back(H[p[i] >> 4]);
    s.push_back(H[p[i] & 0x0F]);
  }
  return s;
}

}  // namespace

TEST(Ed25519CrossBackend, ByteIdenticalAndCrossVerify) {
  ASSERT_GE(sodium_init(), 0);

  const std::vector<std::string> msgs = {
      "", "a", "abc", "The quick brown fox jumps over the lazy dog",
      std::string(1000, 'z')};

  // A spread of deterministic seeds.
  for (int s = 0; s < 16; ++s) {
    std::array<unsigned char, 32> seed{};
    for (int i = 0; i < 32; ++i)
      seed[static_cast<std::size_t>(i)] = static_cast<unsigned char>((s * 31 + i * 7 + 1) & 0xFF);

    // ref10 (orlp): private_key is the 64-byte expanded key.
    unsigned char r_pub[32];
    unsigned char r_priv[64];
    ed25519_create_keypair(r_pub, r_priv, seed.data());

    // sodium: sk64 = seed‖pk.
    unsigned char s_pub[32];
    unsigned char s_sk[64];
    crypto_sign_seed_keypair(s_pub, s_sk, seed.data());

    EXPECT_EQ(hx(r_pub, 32), hx(s_pub, 32)) << "pubkey mismatch, seed idx " << s;

    for (const auto& m : msgs) {
      const auto* mp = reinterpret_cast<const unsigned char*>(m.data());
      const std::size_t ml = m.size();

      unsigned char r_sig[64];
      ed25519_sign(r_sig, mp, ml, r_pub, r_priv);

      unsigned char s_sig[64];
      crypto_sign_detached(s_sig, nullptr, mp, static_cast<unsigned long long>(ml), s_sk);

      EXPECT_EQ(hx(r_sig, 64), hx(s_sig, 64))
          << "sig mismatch, seed idx " << s << ", msg len " << ml;

      // cross-verify: each backend accepts the other's signature.
      EXPECT_EQ(ed25519_verify(s_sig, mp, ml, s_pub), 1) << "ref10 rejected sodium sig";
      EXPECT_EQ(crypto_sign_verify_detached(s_sig, mp, static_cast<unsigned long long>(ml), r_pub), 0)
          << "sodium rejected ref10 pub/sodium sig";
      EXPECT_EQ(crypto_sign_verify_detached(r_sig, mp, static_cast<unsigned long long>(ml), r_pub), 0)
          << "sodium rejected ref10 sig";
    }
  }
}

#else  // no libsodium in this build

TEST(Ed25519CrossBackend, SkippedNoSodium) {
  GTEST_SKIP() << "libsodium not available in this build (ref10 backend still "
                  "validated against canonical vectors by ed25519_rfc8032_test)";
}

#endif
