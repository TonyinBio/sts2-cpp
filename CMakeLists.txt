cmake_minimum_required(VERSION 3.20)
project(sts2_simulator VERSION 0.1.0 LANGUAGES CXX)

add_library(sts2_simulator src/simulator.cpp)
target_include_directories(sts2_simulator PUBLIC include)
target_compile_features(sts2_simulator PUBLIC cxx_std_20)
set_target_properties(sts2_simulator PROPERTIES CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sts2_simulator PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(sts2_cli src/main.cpp)
target_link_libraries(sts2_cli PRIVATE sts2_simulator)

include(CTest)
if(BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
  )
  set(BUILD_GMOCK OFF CACHE BOOL "Build GoogleMock")
  if(MSVC)
    set(gtest_force_shared_crt ON CACHE BOOL "Use the shared MSVC runtime" FORCE)
  endif()
  FetchContent_MakeAvailable(googletest)

  add_executable(allocation_test tests/allocation_test.cpp)
  target_link_libraries(allocation_test PRIVATE sts2_simulator GTest::gtest_main)
  add_executable(simulator_test tests/simulator_test.cpp tests/rng_test.cpp)
  target_link_libraries(simulator_test PRIVATE sts2_simulator GTest::gtest_main)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(nlohmann_json)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/schema/combat-v1.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/schema/actions-v1.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/schema/states-v1.json")
  file(READ tests/parity/schema/combat-v1.json COMBAT_SCHEMA)
  file(READ tests/parity/schema/actions-v1.json ACTIONS_SCHEMA)
  file(READ tests/parity/schema/states-v1.json STATES_SCHEMA)
  configure_file(tests/parity/schema.hpp.in generated/parity/schema.hpp @ONLY)
  add_library(combat_state STATIC tests/parity/snapshot.cpp
    tests/parity/schema_validation.cpp tests/parity/recording.cpp tests/parity/state_recording.cpp)
  target_compile_features(combat_state PUBLIC cxx_std_20)
  target_include_directories(combat_state PUBLIC tests/parity
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/parity")
  target_link_libraries(combat_state PUBLIC nlohmann_json::nlohmann_json)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(combat_state PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_executable(combat_state_test tests/parity/snapshot_test.cpp)
  target_link_libraries(combat_state_test PRIVATE combat_state GTest::gtest_main)
  target_compile_definitions(combat_state_test PRIVATE
    PARITY_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/examples/starter.json")
  add_executable(action_recording_test tests/parity/recording_test.cpp)
  target_link_libraries(action_recording_test PRIVATE combat_state GTest::gtest_main)
  target_compile_definitions(action_recording_test PRIVATE
    ACTION_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/examples/starter.actions.json")
  add_library(simulator_parity STATIC tests/parity/simulator_adapter.cpp tests/parity/sha256.cpp)
  target_link_libraries(simulator_parity PUBLIC combat_state sts2_simulator)
  add_executable(parity_replay tests/parity/replay_main.cpp)
  target_link_libraries(parity_replay PRIVATE simulator_parity)
  add_executable(simulator_parity_test tests/parity/simulator_parity_test.cpp)
  target_link_libraries(simulator_parity_test PRIVATE simulator_parity GTest::gtest_main)
  target_compile_definitions(simulator_parity_test PRIVATE REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  add_executable(parity_validate tests/parity/validate_main.cpp)
  target_link_libraries(parity_validate PRIVATE combat_state)
  add_executable(state_recording_test tests/parity/state_recording_test.cpp)
  target_link_libraries(state_recording_test PRIVATE combat_state GTest::gtest_main)
  target_compile_definitions(state_recording_test PRIVATE
    PARITY_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/examples/starter.json"
    ACTION_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/examples/starter.actions.json")
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME oracle_recorder_unit COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/oracle/test_record.py" "$<TARGET_FILE:parity_validate>")
    set_tests_properties(oracle_recorder_unit PROPERTIES LABELS "recorder")
  endif()
  include(GoogleTest)
  gtest_discover_tests(simulator_parity_test PROPERTIES LABELS parity)
  gtest_discover_tests(state_recording_test PROPERTIES LABELS state_recording)
  gtest_discover_tests(action_recording_test PROPERTIES LABELS action_recording)
  gtest_discover_tests(combat_state_test PROPERTIES LABELS combat_state)
  gtest_discover_tests(allocation_test PROPERTIES LABELS unit)
  gtest_discover_tests(simulator_test PROPERTIES LABELS unit)
endif()

option(STS2_BUILD_BENCHMARKS "Build simulation throughput benchmark" OFF)
if(STS2_BUILD_BENCHMARKS)
  add_executable(simulation_benchmark benchmarks/simulation_benchmark.cpp)
  target_link_libraries(simulation_benchmark PRIVATE sts2_simulator)
endif()
