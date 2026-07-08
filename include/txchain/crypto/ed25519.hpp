#pragma once
// Ed25519 — the trust root's authenticity primitive (Architecture Cryptography
// §5). ONE facade over a compile-time-selected backend (vendored ref10 by
// default, libsodium when detected). Both implement RFC 8032, so signatures and
// public keys are byte-for-byte identical across backends — that is what lets a
// single golden-vector set govern every build.
//
// Callers NEVER #include libsodium or the vendored ref10 header directly; this
// facade is the only surface. `verify` is real in every configuration — no
// build stubs it out.

#include <stdexcept>

#include "txchain/crypto/fixedbytes.hpp"

namespace txchain::crypto {

// Deterministically derive the Ed25519 public key from a 32-byte seed.
PubKey32 derive_pubkey(const Seed32& seed) noexcept;

// Sign `msg` with the key identified by `seed`. Deterministic (RFC 8032): the
// same (seed, msg) yields the same 64-byte R‖S signature on every backend.
Sig64 sign(const Seed32& seed, ByteView msg) noexcept;

// Verify `sig` over `msg` under `pubkey`. The ONLY function the Pillar-2 gate
// calls; returns a plain bool (true == valid). Real in every build.
[[nodiscard]] bool verify(const PubKey32& pubkey, const Sig64& sig, ByteView msg) noexcept;

struct KeyPair {
  Seed32 seed;      // the persisted private form (32 bytes)
  PubKey32 pubkey;
};

// Generate a fresh keypair from the OS CSPRNG. Throws std::runtime_error if the
// CSPRNG is unavailable — NEVER returns a silent zero key.
KeyPair keygen();

// Deterministic keypair from a fixed seed — genesis/demo funding only, NOT for
// real key material. Same seed -> same key on every OS/backend.
KeyPair keygen_from_seed(const Seed32& seed) noexcept;

// The backend compiled into this build: "ref10" or "sodium". For logs/tests.
const char* ed25519_backend() noexcept;

}  // namespace txchain::crypto
