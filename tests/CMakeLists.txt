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
