# AEROLAB RESILIENCE - WebAssembly demo core (SYS-006, NFR-011, NFR-012).
#
# Build:  emcmake cmake --preset wasm && cmake --build build/wasm
# Output: web/public/wasm/aerolab.js + aerolab.wasm
#
# -sMODULARIZE keeps the module out of the global scope so Vite can import it.
# ALLOW_MEMORY_GROWTH is on because a 15 state filter times five channels plus
# the rollback buffer is small but not fixed; INITIAL_MEMORY stays modest so the
# page does not reserve 128 MB it will never touch (NFR-011).

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "web/wasm must be configured with emcmake (Emscripten toolchain)")
endif()

add_executable(aerolab_wasm binding.cpp)
target_link_libraries(aerolab_wasm PRIVATE aerolab_core aerolab_flags)

set_target_properties(aerolab_wasm PROPERTIES
    OUTPUT_NAME "aerolab"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/web/public/wasm
    SUFFIX ".js")

target_link_options(aerolab_wasm PRIVATE
    "--bind"
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=createAerolabModule"
    "-sEXPORT_ES6=1"
    # web,node rather than web alone: the parity check (AT-009) must exercise the
    # SAME artefact the browser loads, not a second build nobody deploys. The
    # extra Node glue costs a few kilobytes.
    "-sENVIRONMENT=web,node"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sINITIAL_MEMORY=33554432"
    "-sSTACK_SIZE=1048576"
    "-sFILESYSTEM=0"
    "-sASSERTIONS=0"
    "-O3")

# Determinism: no fast-math, no FMA contraction. Cross-toolchain parity with the
# native build is tolerance based (DEV-003), but within the wasm build the run
# must still be bit-reproducible.
target_compile_options(aerolab_wasm PRIVATE -O3 -ffp-contract=off)
