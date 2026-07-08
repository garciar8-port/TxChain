#pragma once
// Fixed-width byte types for the cryptographic trust root (Architecture
// Cryptography §3). Making every crypto value a strong std::array typedef turns
// a wrong-length argument into a COMPILE error rather than a runtime bug, and
// pins exactly the widths frozen in the serialization anchor.

#include <array>
#include <cstddef>
#include <cstdint>

namespace txchain::crypto {

using Byte = std::uint8_t;

using Digest32  = std::array<Byte, 32>;  // SHA-256 output; blockHash, txnsHash, txid, prevHash
using Address20 = std::array<Byte, 20>;  // SHA-256(pubkey)[:20]; Txn.from / Txn.to
using PubKey32  = std::array<Byte, 32>;  // Ed25519 public key
using Sig64     = std::array<Byte, 64>;  // Ed25519 signature R‖S
using Seed32    = std::array<Byte, 32>;  // Ed25519 seed (the persisted private material)
using SecKey64  = std::array<Byte, 64>;  // libsodium expanded secret key (seed‖pubkey); never persisted

// ByteView — a non-owning {const Byte*, size} view, the input type for all
// variable-length hashing (a minimal C++17 stand-in for C++20 std::span<const
// Byte>). No hashing API takes std::string, which avoids the v1 "did we hash the
// hex text or the raw bytes" class of bug (encoding rules R3/R6).
class ByteView {
public:
  constexpr ByteView() noexcept : ptr_(nullptr), len_(0) {}
  constexpr ByteView(const Byte* p, std::size_t n) noexcept : ptr_(p), len_(n) {}

  constexpr const Byte* data() const noexcept { return ptr_; }
  constexpr std::size_t size() const noexcept { return len_; }
  constexpr bool empty() const noexcept { return len_ == 0; }

  constexpr const Byte* begin() const noexcept { return ptr_; }
  constexpr const Byte* end() const noexcept { return ptr_ + len_; }

private:
  const Byte* ptr_;
  std::size_t len_;
};

}  // namespace txchain::crypto
