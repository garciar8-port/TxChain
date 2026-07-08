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
