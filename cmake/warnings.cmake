# Interface library carrying the project's warnings-as-errors policy.
#
# Link this PRIVATE into FIRST-PARTY targets (txchain_core, the app shims). Do
# NOT link it into third-party code (GoogleTest, vendored SHA-256/ref10) or the
# test binary — GoogleTest's macros/headers are not clean under this flag set,
# and per the Architecture the -Werror surface is the library + apps, not tests.

add_library(txchain_warnings INTERFACE)

if(MSVC)
  target_compile_options(txchain_warnings INTERFACE /W4 /WX)
else()
  target_compile_options(txchain_warnings INTERFACE
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wnon-virtual-dtor
    -Woverloaded-virtual)
endif()
