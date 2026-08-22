# AEROLAB RESILIENCE - test suite (VNV-001..VNV-020).
#
# GoogleTest is fetched at configure time as frozen in section 9. Set
# AEROLAB_USE_SYSTEM_GTEST=ON to link an installed copy instead, which is what
# an offline or distribution build should do.

include(FetchContent)

option(AEROLAB_USE_SYSTEM_GTEST "Link an installed GoogleTest instead of fetching it" OFF)

if(AEROLAB_USE_SYSTEM_GTEST)
  find_package(GTest REQUIRED)
else()
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

set(AEROLAB_TEST_SOURCES
    unit/test_types.cpp
    unit/test_json.cpp
    unit/test_scenario_loading.cpp
    unit/test_math.cpp
    unit/test_rng.cpp
    unit/test_truth.cpp
    unit/test_sensors.cpp
    unit/test_faults.cpp
    unit/test_estimators.cpp
    unit/test_integrity.cpp
    unit/test_io.cpp
    unit/test_metrics.cpp
    integration/test_pipeline.cpp
    regression/test_acceptance.cpp
)

add_executable(aerolab_tests ${AEROLAB_TEST_SOURCES})
target_link_libraries(aerolab_tests PRIVATE aerolab_core aerolab_flags GTest::gtest_main)
target_compile_definitions(aerolab_tests PRIVATE
    AEROLAB_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(aerolab_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# GoogleTest headers trip -Wconversion and friends; the project code does not.
if(NOT MSVC)
  target_compile_options(aerolab_tests PRIVATE -Wno-conversion -Wno-sign-conversion)
endif()

# The per-test timeout scales with the instrumentation, because the tests do
# not get any smaller when the binary gets slower.
#
# AT003 runs the nominal false-alert budget over its full seed set. Uninstrumented
# it fits inside 300 s; under AddressSanitizer it does not, and the run failed on
# a TIMEOUT with no memory error reported - its neighbours were already taking
# 218 s and 243 s in the same job. Cutting the seed count to fit would have
# meant a weaker false-alert measurement on precisely the build where the
# measurement is hardest to make, so the budget moves instead of the test.
set(AEROLAB_TEST_TIMEOUT 300)
if(AEROLAB_ENABLE_ASAN OR AEROLAB_ENABLE_UBSAN OR AEROLAB_ENABLE_COVERAGE)
  set(AEROLAB_TEST_TIMEOUT 1200)
endif()

include(GoogleTest)
gtest_discover_tests(aerolab_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_TIMEOUT 120
    PROPERTIES TIMEOUT ${AEROLAB_TEST_TIMEOUT})
