# TxChain

EE 450 final project — a simplified blockchain transaction service.

A main server (`serverM`) coordinates three backend servers (`serverA/B/C`), each
owning a block file, plus a `client` and `monitor`. The main server talks to the
client and monitor over **TCP** and to the backend servers over **UDP**.

## Operations

- **CHECK WALLET** — `./client <username>` — compute a user's balance.
- **TXCOINS** — `./client <sender> <receiver> <amount>` — transfer coins.
- **TXLIST** — `./monitor TXLIST` — write a sorted, decrypted `txchain.txt`.

## Build & run

```
make all          # compile all executables
./serverM         # then serverA, serverB, serverC in separate terminals
./client ...      # client / monitor requests
make clean        # remove executables
```

## Layout

- `serverM.*`, `serverA.*`, `serverB.*`, `serverC.*` — servers
- `client.*`, `monitor.*` — request programs
- `block1.txt`, `block2.txt`, `block3.txt` — backend block files (grader-provided)
- `Makefile`, `Readme` — required for grading

Due **2026-06-29 @ 11:59 PM** (Brightspace).
