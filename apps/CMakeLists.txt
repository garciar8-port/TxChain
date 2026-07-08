# Thin executable shims. Each contains only argument parsing, process wiring,
# and I/O glue, and links ONLY txchain_core (which carries all ledger logic).
# No ledger logic, byte layouts, or crypto calls live here (Architecture §7.1).

add_executable(txnode txnode/main.cpp)
target_link_libraries(txnode PRIVATE txchain_core txchain_warnings)

add_executable(txchain txchain/main.cpp)
target_link_libraries(txchain PRIVATE txchain_core txchain_warnings)
