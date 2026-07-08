cmake_minimum_required(VERSION 3.20)

project(txchain
  VERSION 2.0.0
  DESCRIPTION "TxChain v2 — a hash-linked, signed, PoW-secured account ledger"
  LANGUAGES CXX C)

# ---- Language standard: C++17, no compiler extensions ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Enforce a 64-bit toolchain (Brief: v2 is 64-bit; drops v1's 32-bit VM) ----
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR
    "TxChain v2 requires a 64-bit toolchain (found ${CMAKE_SIZEOF_VOID_P}-byte pointers).")
endif()

# Default build type for single-config generators.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(warnings)

# ---- Options (some are no-ops at this scaffold stage; real behavior lands in later tickets) ----
option(TXCHAIN_BUILD_TESTS "Build the unit/golden/integration tests" ON)
option(TXCHAIN_NO_CRYPTO
  "Smoke build: force the vendored ref10 Ed25519 backend, no libsodium (behavior lands in CRE-190/CRE-194)" OFF)
set(TXCHAIN_SANITIZE "" CACHE STRING "Sanitizer to enable: address | thread | undefined (empty = none)")
set_property(CACHE TXCHAIN_SANITIZE PROPERTY STRINGS "" address thread undefined)

# TXCHAIN_NO_CRYPTO is a no-op today; reference it so the option is not "unused"
# and the no_crypto CI leg configures cleanly.
if(TXCHAIN_NO_CRYPTO)
  message(STATUS "TXCHAIN_NO_CRYPTO=ON (scaffold: no crypto behavior differs yet; see CRE-190 / CRE-194)")
endif()

# Interface target carrying optional sanitizer flags. Applied to first-party
# targets only (propagated via txchain_core); third_party code is untouched.
add_library(txchain_sanitize INTERFACE)
if(TXCHAIN_SANITIZE)
  message(STATUS "Sanitizer enabled: ${TXCHAIN_SANITIZE}")
  target_compile_options(txchain_sanitize INTERFACE
    -fsanitize=${TXCHAIN_SANITIZE} -fno-omit-frame-pointer -g)
  target_link_options(txchain_sanitize INTERFACE -fsanitize=${TXCHAIN_SANITIZE})
endif()

add_subdirectory(third_party/sha256)  # vendored SHA-256 (built without -Werror)

add_subdirectory(src)     # txchain_core static library
add_subdirectory(apps)    # txnode + txchain executables (thin shims)

if(TXCHAIN_BUILD_TESTS)
  enable_testing()
  include(CTest)
  add_subdirectory(tests)
endif()
