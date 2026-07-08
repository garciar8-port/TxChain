#include "txchain/crypto/address.hpp"

#include <cstddef>

#include "txchain/crypto/sha256.hpp"

namespace txchain::crypto {

Address20 address(const PubKey32& pubkey) noexcept {
  const Digest32 digest = sha256(ByteView(pubkey.data(), pubkey.size()));
  Address20 addr{};
  for (std::size_t i = 0; i < addr.size(); ++i) addr[i] = digest[i];
  return addr;
}

bool address_matches(const Address20& from, const PubKey32& pubkey) noexcept {
  return from == address(pubkey);
}

}  // namespace txchain::crypto
