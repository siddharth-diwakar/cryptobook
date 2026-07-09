# Warning flags as an INTERFACE target. Linked into first-party targets ONLY,
# never into FetchContent dependencies — their headers/sources must not fail our
# -Werror build. Flags match CLAUDE.md: warnings are errors.
add_library(asmm_warnings INTERFACE)

target_compile_options(asmm_warnings INTERFACE
  -Wall
  -Wextra
  -Werror)
