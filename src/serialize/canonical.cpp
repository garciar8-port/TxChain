#include "txchain/serialize/canonical.hpp"

#include <cstddef>

#include "txchain/crypto/sha256.hpp"

namespace txchain::serialize {

namespace {

void put_u8(Byte* p, std::size_t& off, std::uint8_t v) noexcept { p[off++] = v; }

// Big-endian (network byte order, most-significant byte first) — R2.
void put_u64(Byte* p, std::size_t& off, std::uint64_t v) noexcept {
  for (int i = 7; i >= 0; --i) p[off++] = static_cast<Byte>((v >> (i * 8)) & 0xFF);
}

template <std::size_t N>
void put_arr(Byte* p, std::size_t& off, const std::array<Byte, N>& a) noexcept {
  for (std::size_t i = 0; i < N; ++i) p[off++] = a[i];
}

std::uint64_t get_u64(const Byte* p, std::size_t& off) noexcept {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<std::uint64_t>(p[off++]);
  return v;
}

template <std::size_t N>
void get_arr(const Byte* p, std::size_t& off, std::array<Byte, N>& a) noexcept {
  for (std::size_t i = 0; i < N; ++i) a[i] = p[off++];
}

}  // namespace

std::array<Byte, kTxnSize> serialize(const Txn& tx) noexcept {
  std::array<Byte, kTxnSize> out{};
  std::size_t off = 0;
  put_u8(out.data(), off, tx.ver);      // @0
  put_arr(out.data(), off, tx.from);    // @1
  put_arr(out.data(), off, tx.to);      // @21
  put_u64(out.data(), off, tx.amount);  // @41
  put_u64(out.data(), off, tx.nonce);   // @49
  put_arr(out.data(), off, tx.pubkey);  // @57
  put_arr(out.data(), off, tx.sig);     // @89 .. @153
  return out;
}

std::array<Byte, kHeaderSize> serialize(const BlockHeader& h) noexcept {
  std::array<Byte, kHeaderSize> out{};
  std::size_t off = 0;
  put_u64(out.data(), off, h.index);      // @0
  put_u64(out.data(), off, h.timestamp);  // @8
  put_arr(out.data(), off, h.prevHash);   // @16
  put_arr(out.data(), off, h.txnsHash);   // @48
  put_u64(out.data(), off, h.nonce);      // @80 .. @88
  return out;
}

std::optional<Txn> deserialize_txn(ByteView bytes) noexcept {
  if (bytes.size() != kTxnSize) return std::nullopt;
  const Byte* p = bytes.data();
  if (p[0] != kTxnVersion) return std::nullopt;
  Txn tx;
  std::size_t off = 0;
  tx.ver = p[off++];
  get_arr(p, off, tx.from);
  get_arr(p, off, tx.to);
  tx.amount = get_u64(p, off);
  tx.nonce = get_u64(p, off);
  get_arr(p, off, tx.pubkey);
  get_arr(p, off, tx.sig);
  return tx;
}

std::optional<BlockHeader> deserialize_header(ByteView bytes) noexcept {
  if (bytes.size() != kHeaderSize) return std::nullopt;
  const Byte* p = bytes.data();
  BlockHeader h;
  std::size_t off = 0;
  h.index = get_u64(p, off);
  h.timestamp = get_u64(p, off);
  get_arr(p, off, h.prevHash);
  get_arr(p, off, h.txnsHash);
  h.nonce = get_u64(p, off);
  return h;
}

std::array<Byte, kSignedPayloadSize> signed_payload(const Txn& tx) noexcept {
  const auto full = serialize(tx);
  std::array<Byte, kSignedPayloadSize> sp{};
  for (std::size_t i = 0; i < kSignedPayloadSize; ++i) sp[i] = full[i];
  return sp;
}

crypto::Digest32 txid(const Txn& tx) noexcept {
  const auto b = serialize(tx);
  return crypto::sha256(ByteView(b.data(), b.size()));
}

crypto::Digest32 txns_hash(const std::vector<Txn>& txns) noexcept {
  crypto::Sha256 h;
  for (const auto& t : txns) {
    const auto b = serialize(t);
    h.update(ByteView(b.data(), b.size()));
  }
  return h.final();  // empty list -> SHA-256("")
}

crypto::Digest32 block_hash(const BlockHeader& h) noexcept {
  const auto b = serialize(h);
  return crypto::sha256(ByteView(b.data(), b.size()));
}

}  // namespace txchain::serialize
