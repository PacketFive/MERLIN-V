# SPDX-License-Identifier: Apache-2.0
#
# MERLIN-V Zephyr module — CMake glue.

if(CONFIG_MERLIN)
  zephyr_include_directories(include)

  zephyr_library_named(merlin)
  zephyr_library_sources(
    src/decode.c
    src/verifier.c
    src/loader.c
    src/runtime.c
    src/dispatch.c
  )

  if(CONFIG_MERLIN_SAMPLE_HELPERS)
    zephyr_library_sources(src/helpers_sample.c)
  endif()
endif()
