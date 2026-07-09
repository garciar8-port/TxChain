#!/usr/bin/env python3
"""Regenerate the frozen GENESIS_ALLOC table (Architecture Chain/State §3).

Each demo identity's Ed25519 seed is SHA-256("txchain-genesis-" || name); the
address is SHA-256(pubkey)[:20]. This re-derives all five from the names alone
(no committed secret material) and prints the C++ table body used in
include/txchain/chain/genesis.hpp. The genesis_test recomputes the same values
in C++ and asserts the table byte-for-byte.

Requires: python3 with the `cryptography` package.
Usage: python3 scripts/gen_genesis.py
"""
import hashlib

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

NAMES = ["racheal", "oliver", "matthew", "sophia", "daniel"]
AMOUNT = 1000

_RAW = serialization.Encoding.Raw
_PUB = serialization.PublicFormat.Raw


def address_for(name: str) -> bytes:
    seed = hashlib.sha256(("txchain-genesis-" + name).encode()).digest()
    sk = Ed25519PrivateKey.from_private_bytes(seed)
    pub = sk.public_key().public_bytes(_RAW, _PUB)
    return hashlib.sha256(pub).digest()[:20]


def main() -> None:
    total = 0
    print("// GENESIS_ALLOC body (paste into include/txchain/chain/genesis.hpp):")
    for name in NAMES:
        addr = address_for(name)
        total += AMOUNT
        body = ", ".join(f"0x{b:02x}" for b in addr)
        print(f"    {{/* {name} */ Address{{{{{body}}}}}, {AMOUNT}}},")
    print(f"// GENESIS_COUNT = {len(NAMES)}, GENESIS_SUPPLY = {total}")


if __name__ == "__main__":
    main()
