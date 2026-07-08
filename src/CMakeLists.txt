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
  serialize/serialize.cpp
  chain/chain.cpp
  state/state.cpp
  mempool/mempool.cpp
  pow/pow.cpp
  p2p/p2p.cpp
  rpc/rpc.cpp
  metrics/metrics.cpp
  log/log.cpp
  config/config.cpp)

target_include_directories(txchain_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Warnings are a private, first-party policy; the sanitizer interface is public
# so linkers of txchain_core (apps, tests) inherit consistent instrumentation.
target_link_libraries(txchain_core
  PRIVATE txchain_warnings
  PUBLIC  txchain_sanitize)
