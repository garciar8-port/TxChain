# TxChain — EE 450 Final Project

**Name (as in class list):** Rodrigo Garcia
**USC Student ID:** 2105066875
**Extra credit (Phase 4):** Not attempted.

A simplified blockchain transaction service. A Main Server (`serverM`) coordinates
three backend servers (`serverA/B/C`), each owning a block file, plus a `client`
and a `monitor`. The Main Server talks to the client and monitor over **TCP** and
to the backend servers over **UDP**.

> Status: Phases 1, 2, and 3 implemented (boot/TCP setup; CHECK WALLET & TXCOINS;
> TXLIST). Phase 4 (extra credit) not attempted.

## Ports (USC ID last 3 digits = 875)

| Process | Port |
| --- | --- |
| Server A (UDP) | 21875 |
| Server B (UDP) | 22875 |
| Server C (UDP) | 23875 |
| Server M (UDP, to backends) | 24875 |
| Server M (TCP, client) | 25875 |
| Server M (TCP, monitor) | 26875 |
| client / monitor | dynamic (OS-assigned) |

Host is hard-coded as `127.0.0.1` everywhere.

## Build & run

```
make all            # compile all six executables
make clean          # remove executables
./serverM           # then ./serverA ./serverB ./serverC (separate terminals)
./client <username>                      # CHECK WALLET
./client <sender> <receiver> <amount>    # TXCOINS
./monitor TXLIST                         # (Phase 3)
```

Start order: `serverM`, `serverA`, `serverB`, `serverC`. Servers run until killed
with Ctrl+C; the client and monitor exit after each request.

## Code files

- **serverM.cpp** — Main Server. Owns all encryption/decryption (Caesar +3).
  Binds the UDP socket (24875) plus two TCP listeners (client 25875, monitor 26875)
  and uses `select()` to serve requests. For CHECK WALLET / TXCOINS it queries the
  three backends over UDP, computes the result, and replies to the client.
- **serverA.cpp / serverB.cpp / serverC.cpp** — Backend servers owning
  `block1/2/3.txt`. They store, search, and append transactions entirely in
  **ciphertext** (they never encrypt or decrypt). Each binds its static UDP port.
- **client.cpp** — Connects to Server-M over TCP; sends a CHECK WALLET or TXCOINS
  request and prints the result.
- **monitor.cpp** — Connects to Server-M over TCP; sends a TXLIST request (Phase 3).

## Encryption scheme (Caesar +3)

Applied by the Main Server before sending transaction data to the backends, and
reversed before replying to the client/monitor:

- Letters shift +3 within their case (case-sensitive): `Martin` → `Pduwlq`.
- Digits shift +3 mod 10: `0.27` → `3.50`, `100` → `433`.
- Spaces, the decimal point, and other special characters are unchanged.
- **Serial numbers are never encrypted.**

Block files therefore store rows as `<serial> <encSender> <encReceiver> <encAmount>`,
e.g. `5 Fklqpdb Rolyhu 452` (serial 5, Chinmay → Oliver, 129).

## Messages exchanged

### client ↔ Server-M (TCP)
- client → M: `CHECK_WALLET <username>` or `TXCOINS <sender> <receiver> <amount>`
- M → client: `FOUND <balance>` / `NOTFOUND`
  / `SUCCESS <newBalance>` / `INSUFFICIENT <balance>`
  / `MISSINGSENDER` / `MISSINGRECEIVER` / `MISSINGBOTH`

### monitor ↔ Server-M (TCP)
- monitor → M: `TXLIST`
- M → monitor: `OK` (confirmation that `txchain.txt` was generated)

### Server-M ↔ backends (UDP)
- M → backend: `QUERY <encName1> <encName2>`  (`<encName2>` is `*` when only one
  name is queried) — backend replies with `<maxSerial>` on the first line followed
  by each matching ciphertext row.
- M → backend: `ALL` — backend replies with `<maxSerial>` followed by every
  ciphertext row (used to build the TXLIST statement).
- M → backend: `NEW <serial> <encSender> <encReceiver> <encAmount>` — backend
  appends the row to its block file (with a trailing `\n`) and replies `OK`.

## TXLIST (txchain.txt)

On a `TXLIST` request, Server-M pulls every transaction from A/B/C (via `ALL`,
including rows appended since boot), sorts them ascending by serial number, and
writes `txchain.txt` in the source directory. Each line is **decrypted**:
`<serial> <sender> <receiver> <amount>` terminated by `\n`. The file is
regenerated (overwritten) on each request.

## Balance and transaction rules

- Every user starts with **1000** txcoins.
  `Current Balance = 1000 + Σ received − Σ sent`.
- A user is "part of the network" iff they appear in at least one transaction
  (as sender or receiver).
- TXCOINS succeeds when both parties are in the network and the sender's balance
  ≥ the amount. On success the Main Server assigns the next serial number
  (global max + 1), encrypts the entry, sends it to a **randomly chosen** backend
  to record, and replies with the sender's updated balance.

## Design choices / idiosyncrasies

- **Crypto is centralized in Server-M only.** Backends operate purely on
  ciphertext, which keeps the encryption scheme in one place.
- **TXCOINS append is silent on Server-M's terminal.** Server-M prints exactly
  three "sent a request / received the feedback" pairs per operation (the
  validation round to A, B, C). The subsequent append to the randomly chosen
  backend is not separately announced by M, though that backend still prints its
  own "received a request / finished sending the response" lines.
- **TXLIST backend queries are silent on Server-M's terminal.** The project
  tables define no on-screen message for M querying the backends during TXLIST,
  so M only prints the "received a sorted list request from the monitor" line;
  the backends still print their own received/finished lines.
- **Amounts are treated as integers.**
- **Message-format judgment calls** (the project doc's tables and example output
  disagree in places; the example was followed where one exists):
  - Boot line uses `The ServerA ...` (no space), per the example output; Table 4's
    text says `The Server A ...` (with a space).
  - Usernames are quoted in CHECK-WALLET-related messages (per the example) and
    unquoted in TXCOINS-related messages (per Table 8, which shows no example).
- **Failure conditions:** the servers assume the start order and the localhost-only
  setup described above. UDP is used over loopback without retransmission, which is
  reliable for this single-host configuration.

## Reused code

Socket setup (TCP stream sockets and UDP datagram sockets) is adapted from
**Beej's Guide to Network Programming** (http://www.beej.us/guide/bgnet/). The
relevant blocks are marked with comments in each source file. No other external
code was used.
