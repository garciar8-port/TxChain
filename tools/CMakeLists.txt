# Dev/DevEx tools (not part of the shipped binaries). mine_bench times the nonce
# search across difficulties to calibrate the DIFFICULTY_BITS default (CRE-209).
find_package(Threads REQUIRED)

add_executable(mine_bench mine_bench.cpp)
target_link_libraries(mine_bench PRIVATE txchain_core txchain_warnings Threads::Threads)
