// Ed25519 facade — vendored ref10 backend (orlp/ed25519, ref10-derived).
// Selected when TXCHAIN_CRYPTO=ref10 (the default when libsodium is absent, and
// the NO_CRYPTO smoke backend). Produces standard RFC 8032 signatures identical
// to libsodium.

#include "txchain/crypto/ed25519.hpp"

#include <sys/random.h>  // getentropy (macOS 10.12+, glibc 2.25+)

#include <cstddef>
#include <cstdint>

extern "C" {
#include "ed25519.h"  // vendored orlp header, on vendored_ed25519's include path
}

namespace txchain::crypto {

namespace {
// Fill `out` with `n` CSPRNG bytes, or throw. getentropy caps at 256 bytes,
// which comfortably covers a 32-byte seed.
void csprng_fill(std::uint8_t* out, std::size_t n) {
  if (getentropy(out, n) != 0)
    throw std::runtime_error("CSPRNG unavailable (getentropy failed)");
}
}  // namespace

PubKey32 derive_pubkey(const Seed32& seed) noexcept {
  PubKey32 pub{};
  unsigned char priv[64];
  ed25519_create_keypair(pub.data(), priv, seed.data());
  return pub;
}

Sig64 sign(const Seed32& seed, ByteView msg) noexcept {
  unsigned char pub[32];
  unsigned char priv[64];
  ed25519_create_keypair(pub, priv, seed.data());
  Sig64 sig{};
  ed25519_sign(sig.data(), msg.data(), msg.size(), pub, priv);
  return sig;
}

bool verify(const PubKey32& pubkey, const Sig64& sig, ByteView msg) noexcept {
  return ed25519_verify(sig.data(), msg.data(), msg.size(), pubkey.data()) == 1;
}

KeyPair keygen_from_seed(const Seed32& seed) noexcept {
  KeyPair kp;
  kp.seed = seed;
  kp.pubkey = derive_pubkey(seed);
  return kp;
}

KeyPair keygen() {
  Seed32 seed{};
  csprng_fill(seed.data(), seed.size());
  return keygen_from_seed(seed);
}

const char* ed25519_backend() noexcept { return "ref10"; }

}  // namespace txchain::crypto
