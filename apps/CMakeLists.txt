add_executable(aerolab_cli    aerolab_cli/main.cpp)
add_executable(aerolab_bench  aerolab_bench/main.cpp)
add_executable(aerolab_replay aerolab_replay/main.cpp)

find_package(Threads REQUIRED)

foreach(app aerolab_cli aerolab_bench aerolab_replay)
  target_link_libraries(${app} PRIVATE aerolab_core aerolab_flags)
  set_target_properties(${app} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endforeach()

target_link_libraries(aerolab_bench PRIVATE Threads::Threads)
