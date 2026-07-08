# third_party/ed25519_ref10

Vendored **Ed25519** — [orlp/ed25519](https://github.com/orlp/ed25519) by Orson
Peters (zlib license, see `LICENSE.txt`), a ref10-derived (SUPERCOP) reference
implementation. Standard RFC 8032, so signatures and public keys are
**byte-for-byte identical to libsodium** — proven by
`tests/golden/ed25519_crossbackend_test.cpp`.

- **Role:** the always-available Ed25519 backend. It is the default when
  libsodium is not detected, and the backend forced by `TXCHAIN_NO_CRYPTO=ON`.
  When libsodium *is* present, `TXCHAIN_CRYPTO=auto` selects it instead (a
  compile-time switch — never runtime); both are byte-equivalent.
- **Correctness:** gated by `tests/golden/ed25519_rfc8032_test.cpp` against the
  canonical djb/RFC-8032 vectors (independently cross-verified vs OpenSSL at
  vector-generation time), plus the cross-backend equivalence test.
- **Compiled subset:** only `fe.c ge.c sc.c sha512.c keypair.c sign.c verify.c`
  (sign/verify/keypair). `add_scalar.c`, `key_exchange.c`, and `seed.c` are kept
  for provenance but not built — the facade provides its own OS CSPRNG.
- **Boundary:** exposed only through `include/txchain/crypto/ed25519.hpp`
  (`crypto::sign` / `verify` / `derive_pubkey` / `keygen`); the raw `ed25519.h`
  is private to `txchain_core`. Built as a separate `vendored_ed25519` target
  *without* the project `-Werror` policy.
