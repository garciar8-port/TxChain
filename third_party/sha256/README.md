# third_party/sha256

Vendored single-file **SHA-256** — public domain (Brad Conte lineage,
[github.com/B-Con/crypto-algorithms](https://github.com/B-Con/crypto-algorithms),
released into the public domain), adapted to fixed-width `<stdint.h>` types.

- **Why vendored:** SHA-256 is used in *every* TxChain build with no external
  dependency on the hot path (only the Ed25519 backend swaps). It produces every
  `txid`, `txnsHash`, `blockHash`, `prevHash`, the 20-byte address, and backs the
  PoW target.
- **Correctness:** guaranteed by the NIST/FIPS-180 known-answer test in
  `tests/golden/sha256_kat_test.cpp` (empty, `"abc"`, the 448-bit two-block
  message, and the one-million-`'a'` message), which gates the build before any
  golden vector is trusted.
- **Boundary:** exposed to the rest of `txchain_core` only through the C++ facade
  `include/txchain/crypto/sha256.hpp` (`crypto::sha256` / `crypto::Sha256`); this
  raw C header is private to `txchain_core` and never on the public API. Built as
  a separate `vendored_sha256` target *without* the project's `-Werror` policy.
