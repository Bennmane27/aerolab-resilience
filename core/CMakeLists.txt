# AEROLAB RESILIENCE - core simulation library.
# The core has no UI, no I/O beyond files, and no third party dependency
# (NFR-021, NFR-023, DEV-001).

add_library(aerolab_core STATIC
    src/core/rng.cpp
    src/core/types.cpp
    src/core/hash.cpp
    src/truth/ground_truth.cpp
    src/sensors/sensor_suite.cpp
    src/faults/fault_engine.cpp
    src/navigation/baseline_estimators.cpp
    src/navigation/error_state_ekf.cpp
    src/navigation/solution_separation.cpp
    src/integrity/integrity_manager.cpp
    src/integrity/raim.cpp
    src/metrics/metrics.cpp
    src/io/json.cpp
    src/io/yaml.cpp
    src/io/scenario.cpp
    src/io/runner.cpp
)

target_include_directories(aerolab_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(aerolab_core PUBLIC aerolab_flags)

set_target_properties(aerolab_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_EXTENSIONS OFF
)
