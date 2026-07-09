# GoogleTest via FetchContent (dev/CI only — never vendored into the ledger,
# Architecture §7.4).
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
)
# Keep GoogleTest from overriding parent compiler/linker settings on Windows.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# The unit/golden/integration test binary. It links txchain_core to prove the
# library boundary, plus GTest. The strict warnings-as-errors interface is
# intentionally NOT linked here (GoogleTest headers/macros are not clean under
# it, and per the Architecture the -Werror surface is the library + apps).
add_executable(txchain_tests
  unit/sentinel_test.cpp)

target_link_libraries(txchain_tests
  PRIVATE
    txchain_core
    txchain_sanitize
    GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(txchain_tests)

# Crypto-labeled tests (SHA-256 KAT, Ed25519 RFC 8032 + cross-backend).
add_executable(txchain_crypto_tests
  golden/sha256_kat_test.cpp
  golden/ed25519_rfc8032_test.cpp
  golden/ed25519_crossbackend_test.cpp
  golden/address_test.cpp
  unit/hex_roundtrip_test.cpp
  unit/pow_target_test.cpp)
# tests/golden is on the include path for the generated ed25519_vectors.hpp.
target_include_directories(txchain_crypto_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/golden)
# vendored_ed25519 is linked directly so the cross-backend test can call ref10
# raw (independent of which backend the facade compiled).
target_link_libraries(txchain_crypto_tests
  PRIVATE txchain_core txchain_sanitize vendored_ed25519 GTest::gtest_main)
if(TXCHAIN_HAVE_SODIUM)
  target_compile_definitions(txchain_crypto_tests PRIVATE TXCHAIN_HAVE_SODIUM=1)
  target_link_libraries(txchain_crypto_tests PRIVATE PkgConfig::SODIUM)
else()
  target_compile_definitions(txchain_crypto_tests PRIVATE TXCHAIN_HAVE_SODIUM=0)
endif()
gtest_discover_tests(txchain_crypto_tests PROPERTIES LABELS crypto)

# Serialize-labeled tests (canonical serializer, golden vectors, round-trip).
add_executable(txchain_serialize_tests
  golden/golden_txid_test.cpp
  golden/golden_blockhash_test.cpp
  unit/serialize_roundtrip_test.cpp)
target_include_directories(txchain_serialize_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/golden)
target_link_libraries(txchain_serialize_tests
  PRIVATE txchain_core txchain_sanitize GTest::gtest_main)
gtest_discover_tests(txchain_serialize_tests PROPERTIES LABELS serialize)

# Chain/state tests (M1 core types + deterministic genesis).
add_executable(txchain_chain_tests
  golden/genesis_test.cpp
  unit/chain_state_test.cpp
  unit/connect_block_test.cpp
  unit/replay_test.cpp
  unit/chain_jsonl_test.cpp
  unit/verify_gate_test.cpp)
target_link_libraries(txchain_chain_tests
  PRIVATE txchain_core txchain_sanitize GTest::gtest_main)
gtest_discover_tests(txchain_chain_tests PROPERTIES LABELS chain)

# Node lifecycle tests (boot self-validation + M1 cadence sealer + config
# precedence). The sealer is driven with explicit timestamps — no real sleeping.
add_executable(txchain_node_tests
  unit/node_test.cpp)
target_link_libraries(txchain_node_tests
  PRIVATE txchain_core txchain_sanitize GTest::gtest_main)
gtest_discover_tests(txchain_node_tests PROPERTIES LABELS node)

# Mempool admission tests (admit() ordering, single-pending-nonce, dedup/cap/
# eviction, AdmitResult↔Reason token parity). Reuses the shared consensus gate.
add_executable(txchain_mempool_tests
  unit/mempool_test.cpp)
target_link_libraries(txchain_mempool_tests
  PRIVATE txchain_core txchain_sanitize GTest::gtest_main)
gtest_discover_tests(txchain_mempool_tests PROPERTIES LABELS mempool)

# Wallet key-material tests (wallet.key format, keygen, load/validate). Golden
# derivation is backend-independent (RFC 8032) — same on sodium and ref10.
add_executable(txchain_wallet_tests
  unit/wallet_test.cpp)
target_link_libraries(txchain_wallet_tests
  PRIVATE txchain_core txchain_sanitize GTest::gtest_main)
gtest_discover_tests(txchain_wallet_tests PROPERTIES LABELS wallet)

# Frame-codec test (socket round-trip; uses a writer thread to avoid deadlock).
find_package(Threads REQUIRED)
add_executable(txchain_frame_tests
  golden/frame_codec_test.cpp)
target_link_libraries(txchain_frame_tests
  PRIVATE txchain_core txchain_sanitize GTest::gtest_main Threads::Threads)
gtest_discover_tests(txchain_frame_tests PROPERTIES LABELS frame)

# NO_CRYPTO honesty guard (Architecture Crypto §9): assert no `return true` is
# ever guarded by a crypto NO_CRYPTO #ifdef (the banned fake-verify pattern).
# A pure source scan — runs on every ctest invocation, every matrix leg.
add_test(NAME no_crypto_guard
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_no_fake_verify.sh ${CMAKE_SOURCE_DIR})
set_tests_properties(no_crypto_guard PROPERTIES LABELS guard)
