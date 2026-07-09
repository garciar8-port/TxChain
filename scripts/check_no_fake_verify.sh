#!/usr/bin/env bash
#
# check_no_fake_verify.sh — the NO_CRYPTO honesty guard (Architecture Crypto §9).
#
# NO_CRYPTO is a build-smoke flag: it swaps the Ed25519 backend to the vendored
# ref10 (no libsodium needed) and NOTHING ELSE. It must NEVER fake verification.
# crypto::verify under NO_CRYPTO is the real ref10 verify — a bad signature still
# returns false and still maps to BAD_SIG.
#
# The ONE documented allowance (Architecture §9): under NO_CRYPTO, keygen MAY use
# a fixed non-CSPRNG seed so smoke runs need no entropy source. Signing/verifying
# with that key remain real. (In this codebase keygen keeps the real getentropy
# seed on every build, so even that allowance is unused — verify is always real.)
#
# This guard FAILS if a `return true` / `return 1;` ever appears inside a
# preprocessor block guarded by a crypto NO_CRYPTO conditional — the banned
# `#ifdef NO_CRYPTO ... return true; ... #endif` fake-verify pattern. It scans
# first-party sources only (src/, include/); vendored third_party code is out of
# scope (it contains no verify gate).
#
# Portable to bash 3.2 (macOS) and bash 5 (Linux CI): no mapfile/readarray.
#
# Usage: check_no_fake_verify.sh [repo_root]   (default: current directory)

set -uo pipefail

ROOT="${1:-.}"

FIND_ARGS=( "${ROOT}/src" "${ROOT}/include" -type f
  '(' -name '*.cpp' -o -name '*.cc' -o -name '*.hpp' -o -name '*.h' ')' )

count=$(find "${FIND_ARGS[@]}" 2>/dev/null | wc -l | tr -d ' ')
if [ "${count}" = "0" ]; then
  echo "no_crypto_guard: no source files found under ${ROOT}/{src,include}" >&2
  exit 2
fi

# awk state machine: track a stack over #if/#ifdef/#ifndef nesting. A level is
# "active" (guarded) if it or any ancestor mentions NO_CRYPTO. Inside an active
# level, a `return true` / `return 1;` is the banned fake-verify pattern. The
# program only PRINTS offending lines (file:line: code); any output => a failure.
violations=$(find "${FIND_ARGS[@]}" -print0 2>/dev/null | xargs -0 awk '
  FNR == 1 { top = 0; active = 0 }
  /^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef)([[:space:]]|\(|$)/ {
    top++
    guard = ($0 ~ /NO_CRYPTO/) ? 1 : 0
    stack[top] = (active || guard)
    active = stack[top]
    next
  }
  /^[[:space:]]*#[[:space:]]*endif/ {
    if (top > 0) top--
    active = (top > 0) ? stack[top] : 0
    next
  }
  /^[[:space:]]*#[[:space:]]*(else|elif)/ { next }
  {
    if (active && ($0 ~ /return[[:space:]]+true/ || $0 ~ /return[[:space:]]+1[[:space:]]*;/)) {
      printf("  %s:%d:%s\n", FILENAME, FNR, $0)
    }
  }
')

if [ -n "${violations}" ]; then
  echo "no_crypto_guard: FAILED — a 'return true'/'return 1;' is guarded by a crypto NO_CRYPTO #ifdef:" >&2
  echo "${violations}" >&2
  echo "NO_CRYPTO must never fake verification (Architecture Crypto §9)." >&2
  exit 1
fi

echo "no_crypto_guard: OK — no NO_CRYPTO-guarded fake-verify pattern found (${count} files scanned)"
exit 0
