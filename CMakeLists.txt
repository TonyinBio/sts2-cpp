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

  add_executable(simulator_test tests/unit/simulator_test.cpp tests/unit/rng_test.cpp)
  target_link_libraries(simulator_test PRIVATE sts2_simulator GTest::gtest_main)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(nlohmann_json)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/specs/schemas/run-parity-v1.schema.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/specs/schemas/serializable-run-v20.schema.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/schema/actions-v1.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/schema/states-v1.json")
  file(READ docs/specs/schemas/run-parity-v1.schema.json RUN_PARITY_SCHEMA)
  file(READ docs/specs/schemas/serializable-run-v20.schema.json NATIVE_RUN_SCHEMA)
  file(READ tests/parity/schema/actions-v1.json ACTIONS_SCHEMA)
  file(READ tests/parity/schema/states-v1.json STATES_SCHEMA)
  configure_file(tests/parity/support/schema.hpp.in generated/parity/schema.hpp @ONLY)
  add_library(combat_state STATIC tests/parity/support/snapshot.cpp
    tests/parity/support/schema_validation.cpp tests/parity/support/recording.cpp tests/parity/support/state_recording.cpp)
  target_compile_features(combat_state PUBLIC cxx_std_20)
  target_include_directories(combat_state PUBLIC tests/parity/support
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
  add_executable(parity_validate tools/parity/validate_main.cpp)
  target_link_libraries(parity_validate PRIVATE combat_state)
  add_executable(state_recording_test tests/parity/state_recording_test.cpp)
  target_link_libraries(state_recording_test PRIVATE combat_state GTest::gtest_main)
  target_compile_definitions(state_recording_test PRIVATE
    PARITY_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/examples/starter.json"
    ACTION_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/examples/starter.actions.json")
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  add_library(parity_replay_support STATIC tests/parity/support/replay.cpp
    tests/parity/support/simulator_snapshot.cpp)
  target_link_libraries(parity_replay_support PUBLIC combat_state sts2_simulator PRIVATE OpenSSL::Crypto)
  add_executable(replay_parity_test tests/parity/replay_parity_test.cpp)
  target_link_libraries(replay_parity_test PRIVATE GTest::gtest_main
    parity_replay_support sts2_simulator OpenSSL::Crypto)
  target_compile_definitions(replay_parity_test PRIVATE
    ACTION_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/examples/starter.actions.json"
    STATE_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/fixtures/starter.states.json")
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME run_parity_schema COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracle/test_run_parity_schema.py")
    add_test(NAME native_run_unit COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracle/test_native_run.py")
    set_tests_properties(native_run_unit PROPERTIES LABELS "native_run")
    add_test(NAME interactive_recorder_unit COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracle/test_play.py" "$<TARGET_FILE:parity_validate>")
    set_tests_properties(interactive_recorder_unit PROPERTIES LABELS "recorder")
  endif()
  include(GoogleTest)
  gtest_discover_tests(replay_parity_test PROPERTIES LABELS replay_parity)
  gtest_discover_tests(state_recording_test PROPERTIES LABELS state_recording)
  gtest_discover_tests(action_recording_test PROPERTIES LABELS action_recording)
  gtest_discover_tests(combat_state_test PROPERTIES LABELS combat_state)
  gtest_discover_tests(simulator_test PROPERTIES LABELS unit)
endif()

option(STS2_BUILD_BENCHMARKS "Build simulation throughput benchmark" OFF)
if(STS2_BUILD_BENCHMARKS)
  add_executable(simulation_benchmark benchmarks/simulation_benchmark.cpp)
  target_link_libraries(simulation_benchmark PRIVATE sts2_simulator)
endif()
