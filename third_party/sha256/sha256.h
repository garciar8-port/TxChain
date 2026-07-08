/*
 * Vendored SHA-256 — public domain (Brad Conte lineage,
 * github.com/B-Con/crypto-algorithms, released into the public domain).
 * Adapted to fixed-width <stdint.h> types.
 *
 * Used in EVERY TxChain build (no external SHA on the hot path). Correctness is
 * guaranteed by the NIST/FIPS-180 known-answer test in
 * tests/golden/sha256_kat_test.cpp, which gates the build before any golden
 * vector is trusted (Architecture Cryptography §4, §11).
 */
#ifndef TXCHAIN_THIRD_PARTY_SHA256_H
#define TXCHAIN_THIRD_PARTY_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TXCHAIN_SHA256_DIGEST_SIZE 32
#define TXCHAIN_SHA256_BLOCK_SIZE  64

typedef struct {
    uint8_t  data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} SHA256_CTX;

void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len);
void sha256_final(SHA256_CTX *ctx, uint8_t hash[TXCHAIN_SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* TXCHAIN_THIRD_PARTY_SHA256_H */
