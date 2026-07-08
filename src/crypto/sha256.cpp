#include "txchain/crypto/sha256.hpp"

extern "C" {
#include "sha256.h"  // vendored; on txchain_core's private include path
}

namespace txchain::crypto {

// The public streaming class hides the vendored context behind an opaque
// aligned buffer; verify the buffer is big enough and not under-aligned.
static_assert(sizeof(::SHA256_CTX) <= 128, "opaque SHA-256 context buffer too small");
static_assert(alignof(::SHA256_CTX) <= 8, "SHA-256 context over-aligned for its buffer");

Digest32 sha256(ByteView data) noexcept {
  ::SHA256_CTX c;
  sha256_init(&c);
  if (!data.empty()) sha256_update(&c, data.data(), data.size());
  Digest32 out{};
  sha256_final(&c, out.data());
  return out;
}

Digest32 sha256(const std::vector<Byte>& data) noexcept {
  return sha256(ByteView(data.data(), data.size()));
}

Sha256::Sha256() noexcept {
  sha256_init(reinterpret_cast<::SHA256_CTX*>(ctx_));
}

Sha256& Sha256::update(ByteView chunk) noexcept {
  if (!chunk.empty())
    sha256_update(reinterpret_cast<::SHA256_CTX*>(ctx_), chunk.data(), chunk.size());
  return *this;
}

Digest32 Sha256::final() noexcept {
  Digest32 out{};
  sha256_final(reinterpret_cast<::SHA256_CTX*>(ctx_), out.data());
  return out;
}

Digest32 sha256d(ByteView data) noexcept {
  const Digest32 first = sha256(data);
  return sha256(ByteView(first.data(), first.size()));
}

}  // namespace txchain::crypto
