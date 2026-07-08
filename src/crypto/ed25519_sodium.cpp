// Ed25519 facade — libsodium backend. Selected when TXCHAIN_CRYPTO=sodium
// (the default when libsodium is detected). Produces standard RFC 8032
// signatures byte-identical to the vendored ref10 backend.

#include "txchain/crypto/ed25519.hpp"

#include <sodium.h>

namespace txchain::crypto {

namespace {
// crypto_sign_seed_keypair expands a 32-byte seed into (pk32, sk64=seed‖pk).
// The 64-byte secret is derived on demand and NEVER persisted — only the 32-byte
// Seed32 is stored, keeping wallets byte-portable across backends.
void expand(const Seed32& seed, unsigned char pk[32], unsigned char sk[64]) noexcept {
  crypto_sign_seed_keypair(pk, sk, seed.data());
}
}  // namespace

PubKey32 derive_pubkey(const Seed32& seed) noexcept {
  PubKey32 pub{};
  unsigned char sk[64];
  expand(seed, pub.data(), sk);
  return pub;
}

Sig64 sign(const Seed32& seed, ByteView msg) noexcept {
  unsigned char pk[32];
  unsigned char sk[64];
  expand(seed, pk, sk);
  Sig64 sig{};
  crypto_sign_detached(sig.data(), nullptr, msg.data(),
                       static_cast<unsigned long long>(msg.size()), sk);
  return sig;
}

bool verify(const PubKey32& pubkey, const Sig64& sig, ByteView msg) noexcept {
  return crypto_sign_verify_detached(
             sig.data(), msg.data(),
             static_cast<unsigned long long>(msg.size()), pubkey.data()) == 0;
}

KeyPair keygen_from_seed(const Seed32& seed) noexcept {
  KeyPair kp;
  kp.seed = seed;
  kp.pubkey = derive_pubkey(seed);
  return kp;
}

KeyPair keygen() {
  if (sodium_init() < 0)
    throw std::runtime_error("CSPRNG unavailable (libsodium init failed)");
  Seed32 seed{};
  randombytes_buf(seed.data(), seed.size());
  return keygen_from_seed(seed);
}

const char* ed25519_backend() noexcept { return "sodium"; }

}  // namespace txchain::crypto
