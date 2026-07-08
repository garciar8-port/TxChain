# Vendored ref10-derived Ed25519 (orlp/ed25519, zlib license — see LICENSE.txt).
# Standard RFC 8032; byte-identical to libsodium. Built WITHOUT the project
# -Werror policy (third-party C); correctness is gated by the RFC 8032 KAT in
# tests/golden/ed25519_rfc8032_test.cpp.
#
# Only the sign/verify/keypair subset is compiled (add_scalar / key_exchange /
# seed RNG are not used — the facade provides its own OS CSPRNG).
add_library(vendored_ed25519 STATIC
  fe.c
  ge.c
  sc.c
  sha512.c
  keypair.c
  sign.c
  verify.c)

target_include_directories(vendored_ed25519 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(vendored_ed25519 PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON)
