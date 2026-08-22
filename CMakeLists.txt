# AEROLAB RESILIENCE - root build
# Requirement coverage: NFR-001, NFR-006, NFR-014, SYS-006, SYS-018, SYS-019
cmake_minimum_required(VERSION 3.24)
project(aerolab_resilience
        VERSION 1.0.0
        DESCRIPTION "Navigation Integrity & Resilience Validation Platform"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)            # D-003: C++17 frozen
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AEROLAB_BUILD_TESTS   "Build the test suite"        ON)
option(AEROLAB_BUILD_APPS    "Build the CLI applications"  ON)
option(AEROLAB_ENABLE_ASAN   "Enable AddressSanitizer"     OFF)
option(AEROLAB_ENABLE_UBSAN  "Enable UndefinedBehaviorSanitizer" OFF)
option(AEROLAB_ENABLE_COVERAGE "Enable gcov coverage"      OFF)
option(AEROLAB_WASM          "Build the WebAssembly target" OFF)

# ---------------------------------------------------------------------------
# Determinism flags.
# Deviation DEV-003 (see docs/deviations.md): bit-exact replay is an INTRA-BUILD
# contract only. Cross-toolchain parity (native vs wasm) is tolerance based.
# -ffp-contract=off forbids the compiler from fusing a*b+c into an FMA, which is
# the single largest source of native/wasm divergence on x86-64.
# ---------------------------------------------------------------------------
add_library(aerolab_flags INTERFACE)
target_compile_features(aerolab_flags INTERFACE cxx_std_17)

if(MSVC)
  target_compile_options(aerolab_flags INTERFACE /W4 /permissive- /fp:precise /Zc:__cplusplus)
else()
  target_compile_options(aerolab_flags INTERFACE
      -Wall -Wextra -Wpedantic
      -Wshadow -Wconversion -Wsign-conversion
      -Wnon-virtual-dtor -Woverloaded-virtual
      -Wnull-dereference -Wdouble-promotion -Wformat=2
      -ffp-contract=off)
endif()

if(AEROLAB_ENABLE_ASAN)
  target_compile_options(aerolab_flags INTERFACE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(aerolab_flags INTERFACE -fsanitize=address)
endif()
if(AEROLAB_ENABLE_UBSAN)
  target_compile_options(aerolab_flags INTERFACE -fsanitize=undefined -fno-sanitize-recover=undefined)
  target_link_options(aerolab_flags INTERFACE -fsanitize=undefined)
endif()
if(AEROLAB_ENABLE_COVERAGE)
  target_compile_options(aerolab_flags INTERFACE --coverage -O0 -g)
  target_link_options(aerolab_flags INTERFACE --coverage)
endif()

# Git provenance -> baked into every manifest (SYS-008, DATA-005)
find_package(Git QUIET)
set(AEROLAB_GIT_COMMIT "unknown")
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  OUTPUT_VARIABLE AEROLAB_GIT_COMMIT
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
  if(AEROLAB_GIT_COMMIT STREQUAL "")
    set(AEROLAB_GIT_COMMIT "uncommitted")
  endif()
endif()
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_info.hpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/aerolab/build_info.hpp @ONLY)

add_subdirectory(core)
if(AEROLAB_BUILD_APPS AND NOT AEROLAB_WASM)
  add_subdirectory(apps)
endif()
if(AEROLAB_WASM)
  add_subdirectory(web/wasm)
endif()
if(AEROLAB_BUILD_TESTS AND NOT AEROLAB_WASM)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "AEROLAB ${PROJECT_VERSION} | commit ${AEROLAB_GIT_COMMIT} | build ${CMAKE_BUILD_TYPE}")
