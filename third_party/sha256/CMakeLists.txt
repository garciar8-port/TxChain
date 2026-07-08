# Vendored public-domain SHA-256 (Brad Conte lineage). Compiled into every
# TxChain build (no external SHA on the hot path). Deliberately built WITHOUT the
# project's -Werror warning policy — it is third-party C, and its correctness is
# guaranteed by the NIST KAT (tests/golden/sha256_kat_test.cpp), not by warnings.
add_library(vendored_sha256 STATIC sha256.c)

target_include_directories(vendored_sha256 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(vendored_sha256 PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON)
