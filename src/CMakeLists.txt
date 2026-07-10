# txchain_core — the single shared static library.
#
# Everything that hashes, signs, validates, serializes, stores, or talks the
# wire lives here; the app binaries are thin shims that only link this target
# (Architecture §7.1). Sources are listed bottom-up (crypto -> serialize ->
# chain/state -> mempool/pow -> p2p -> rpc/metrics/log/config); at this scaffold
# stage each module is an independent stub with no sibling dependency, so the
# dependency graph is trivially acyclic (Architecture §7.2).

add_library(txchain_core STATIC
  crypto/crypto.cpp
  crypto/sha256.cpp
  crypto/address.cpp
  crypto/hashutil.cpp
  serialize/serialize.cpp
  serialize/canonical.cpp
  serialize/json.cpp
  chain/chain.cpp
  chain/genesis.cpp
  chain/validate.cpp
  chain/gate.cpp
  chain/store.cpp
  node/node.cpp
  wallet/wallet.cpp
  state/state.cpp
  mempool/mempool.cpp
  pow/pow.cpp
  pow/candidate.cpp
  pow/miner.cpp
  pow/forkchoice.cpp
  p2p/p2p.cpp
  p2p/frame.cpp
  rpc/rpc.cpp
  rpc/http_server.cpp
  rpc/http_client.cpp
  metrics/metrics.cpp
  log/log.cpp
  config/config.cpp)

target_include_directories(txchain_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

# picojson (vendored, header-only) for chain.jsonl parsing — SYSTEM so its
# header is exempt from the -Werror policy; INT64 mode so u64 fields decode
# without double-precision loss.
target_include_directories(txchain_core SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/json)
target_compile_definitions(txchain_core PRIVATE PICOJSON_USE_INT64)

# Warnings are a private, first-party policy; the sanitizer interface is public
# so linkers of txchain_core (apps, tests) inherit consistent instrumentation.
target_link_libraries(txchain_core
  PRIVATE txchain_warnings vendored_sha256
  PUBLIC  txchain_sanitize)

# ---- Ed25519 backend (compile-time switch resolved in the root CMakeLists) ----
# Exactly ONE backend .cpp is compiled into txchain_core; both define the same
# facade symbols. The vendored ref10 header/lib are never on the public API.
if(TXCHAIN_CRYPTO_BACKEND STREQUAL "sodium")
  target_sources(txchain_core PRIVATE crypto/ed25519_sodium.cpp)
  target_link_libraries(txchain_core PRIVATE PkgConfig::SODIUM)
else()
  target_sources(txchain_core PRIVATE crypto/ed25519_ref10.cpp)
  target_link_libraries(txchain_core PRIVATE vendored_ed25519)
endif()
