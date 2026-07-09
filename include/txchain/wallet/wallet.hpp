#pragma once
// wallet.key key material at rest (Architecture Cryptography §10, Node/CLI §3.1).
//
// The single shared owner of the on-disk key format: both the `txchain wallet`
// CLI verbs and `txnode` startup call wallet_create()/wallet_load() here, so the
// format lives in one place. What is PERSISTED is the raw 32-byte Ed25519 seed
// (Seed32); the pubkey and address are always DERIVED — and, on load, the stored
// address is recomputed from the pubkey and asserted, exactly as txid/blockHash
// are recomputed on chain load (never trust a digest read from disk).
//
// Non-goals (Cryptography §1): no at-rest encryption, passphrases, HD wallets,
// rotation, or multisig. wallet.key is plaintext, mode 0600 by design.

#include <cstdint>
#include <string>

#include "txchain/crypto/fixedbytes.hpp"
#include "txchain/serialize/types.hpp"

namespace txchain::wallet {

// The wallet.key schema version this build writes and accepts.
inline constexpr int kWalletVersion = 1;

// An in-memory wallet: the persisted seed plus the derived pubkey/address. The
// address here is ALWAYS the recomputed SHA-256(pubkey)[:20], never a value read
// verbatim from disk.
struct Wallet {
  crypto::Seed32 seed{};
  crypto::PubKey32 pubkey{};
  crypto::Address20 address{};
};

// Outcome of wallet_load / load_or_create_wallet. On !ok the caller refuses to
// start and surfaces `error`. world_readable is a soft signal: the wallet loaded
// fine, but its file mode exposes the key to group/other — warn once, do not fail
// (a deliberate learning-artifact affordance, Cryptography §10).
struct LoadResult {
  bool ok = false;
  Wallet wallet{};
  std::string error;
  bool world_readable = false;
};

// <datadir>/wallet.key.
std::string wallet_path_for_datadir(const std::string& datadir);

// Generate a fresh wallet from the OS CSPRNG (crypto::keygen — throws
// std::runtime_error if the CSPRNG is unavailable, never a silent zero key). The
// derivation is real in every build, including NO_CRYPTO.
Wallet wallet_create();

// Serialize `w` to the wallet.key JSON form { version, priv, pub, address } and
// write it to `path` with file mode 0600 (forced via fchmod, independent of
// umask). Returns false on any I/O error.
[[nodiscard]] bool wallet_save(const std::string& path, const Wallet& w);

// Parse + validate the wallet.key at `path`. Rejects (ok=false): missing/unreadable
// file, malformed JSON, a `priv` seed that is not exactly 32 bytes, or a stored
// `address` that disagrees with SHA-256(derive_pubkey(seed))[:20]. On success the
// returned Wallet's pubkey/address are the recomputed values; world_readable is
// set when the file grants any group/other permission bit.
LoadResult wallet_load(const std::string& path);

// Node-startup helper (Node/CLI §1.3): load <datadir>/wallet.key if present, else
// create one and persist it. Mirrors the chain.jsonl load-or-init pattern.
LoadResult load_or_create_wallet(const std::string& datadir);

// Build a fully-signed canonical transaction from a wallet seed (Data Flow §A):
// pubkey = derive_pubkey(seed); from = SHA-256(pubkey)[:20]; sign the 89-byte
// payload (ver‖from‖to‖amount‖nonce‖pubkey) with RFC 8032 Ed25519 (deterministic).
// The single canonical serializer produces the bytes — never a CLI re-implementation.
// The result admits + connects iff amount/nonce are valid against the payer's account.
serialize::Txn sign_txn(const crypto::Seed32& seed, const crypto::Address20& to,
                        std::uint64_t amount, std::uint64_t nonce);

}  // namespace txchain::wallet
