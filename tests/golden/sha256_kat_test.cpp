// SHA-256 known-answer tests (NIST/FIPS-180) — the trust root's integrity
// primitive is validated here before any golden vector is trusted (Architecture
// Cryptography §4, §11). CTest label: crypto.

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/crypto/sha256.hpp"

namespace {

using txchain::crypto::Byte;
using txchain::crypto::ByteView;
using txchain::crypto::Digest32;
using txchain::crypto::Sha256;
using txchain::crypto::sha256;

std::string to_hex(const Digest32& d) {
  static const char* kHex = "0123456789abcdef";
  std::string s;
  s.reserve(64);
  for (Byte b : d) {
    s.push_back(kHex[b >> 4]);
    s.push_back(kHex[b & 0x0F]);
  }
  return s;
}

ByteView view(const std::string& s) {
  return ByteView(reinterpret_cast<const Byte*>(s.data()), s.size());
}

}  // namespace

// The frozen empty-block constant (anchor §3.2), asserted as a KAT-checked value.
TEST(Sha256Kat, EmptyString) {
  EXPECT_EQ(to_hex(sha256(view(""))),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256Kat, Abc) {
  EXPECT_EQ(to_hex(sha256(view("abc"))),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

// FIPS 180-4 448-bit two-block message.
TEST(Sha256Kat, TwoBlock448) {
  EXPECT_EQ(to_hex(sha256(view("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

// FIPS 180-4 one-million-'a' long message (exercises multi-block + padding).
TEST(Sha256Kat, MillionA) {
  const std::string m(1000000, 'a');
  EXPECT_EQ(to_hex(sha256(view(m))),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// Streaming (arbitrary chunk splits) must equal the one-shot digest.
TEST(Sha256Kat, StreamingEqualsOneShot) {
  const std::string m = "The quick brown fox jumps over the lazy dog";
  const Digest32 oneshot = sha256(view(m));

  const Byte* p = reinterpret_cast<const Byte*>(m.data());
  Sha256 s;
  s.update(ByteView(p, 4))
      .update(ByteView(p + 4, 10))
      .update(ByteView(p + 14, m.size() - 14));
  const Digest32 streamed = s.final();

  EXPECT_EQ(to_hex(streamed), to_hex(oneshot));
  EXPECT_EQ(to_hex(oneshot),
            "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

// An empty update between chunks is a no-op; digest is unchanged.
TEST(Sha256Kat, EmptyChunkIsNoOp) {
  const std::string m = "abc";
  const Byte* p = reinterpret_cast<const Byte*>(m.data());
  Sha256 s;
  s.update(ByteView(p, 1)).update(ByteView()).update(ByteView(p + 1, 2));
  EXPECT_EQ(to_hex(s.final()),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
