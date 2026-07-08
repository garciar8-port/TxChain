#pragma once
// SHA-256 C++ facade over the vendored public-domain implementation
// (Architecture Cryptography §4). Output is always 32 RAW bytes (Digest32),
// never hex. Two forms: a one-shot for the common case and a streaming form for
// concatenations (txnsHash over many txns, the PoW inner loop) that absorbs
// chunks without heap-concatenating.

#include <cstddef>
#include <vector>

#include "txchain/crypto/fixedbytes.hpp"

namespace txchain::crypto {

// One-shot: SHA-256 over a contiguous byte range -> 32 raw bytes.
Digest32 sha256(ByteView data) noexcept;

// Convenience overload for callers that already hold a byte vector.
Digest32 sha256(const std::vector<Byte>& data) noexcept;

// Streaming SHA-256. Absorb chunks via update(); read the digest once via
// final(). The object is single-use after final().
class Sha256 {
public:
  Sha256() noexcept;

  Sha256& update(ByteView chunk) noexcept;  // chainable; absorbs a chunk
  Digest32 final() noexcept;                // 32 raw bytes

private:
  // Opaque inline storage for the vendored SHA256_CTX — keeps the third-party C
  // header off the public include path. Size/alignment are static_assert-checked
  // against sizeof(SHA256_CTX) in sha256.cpp. No heap, so the API stays noexcept.
  alignas(8) unsigned char ctx_[128];
};

// Double SHA-256. Reserved for experiments only — the frozen schema uses single
// SHA-256 everywhere, so this must never appear on a hashed/signed path.
Digest32 sha256d(ByteView data) noexcept;

}  // namespace txchain::crypto
