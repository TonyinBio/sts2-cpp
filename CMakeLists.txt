cmake_minimum_required(VERSION 3.20)
project(sts2_simulator VERSION 0.1.0 LANGUAGES CXX)

add_library(sts2_simulator src/simulator.cpp)
target_include_directories(sts2_simulator PUBLIC include)
target_compile_features(sts2_simulator PUBLIC cxx_std_23)
set_target_properties(sts2_simulator PROPERTIES CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sts2_simulator PRIVATE -Wall -Wextra -Wpedantic)
endif()

# New engine layers. No JSON, game assemblies, or filesystem dependencies enter
# the rules/kernel targets. The original demo remains isolated during migration.
add_library(sts2_rules src/rules/ruleset.cpp src/rules/setup.g.cpp)
target_include_directories(sts2_rules PUBLIC include)
target_compile_features(sts2_rules PUBLIC cxx_std_23)
add_library(sts2_kernel src/state/rng.cpp src/kernel/simulator.cpp src/kernel/systems/map.cpp src/kernel/systems/setup.cpp src/kernel/systems/rewards.cpp src/kernel/run_simulator.cpp src/kernel/run_validation.cpp)
target_link_libraries(sts2_kernel PUBLIC sts2_rules)
add_library(sts2_observation src/observation/observe.cpp)
target_link_libraries(sts2_observation PUBLIC sts2_kernel)
add_library(sts2_persistence src/persistence/checkpoint.cpp src/persistence/run_checkpoint.cpp)
target_link_libraries(sts2_persistence PUBLIC sts2_kernel)
foreach(engine_target sts2_rules sts2_kernel sts2_observation sts2_persistence)
  set_target_properties(${engine_target} PROPERTIES CXX_EXTENSIONS OFF)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${engine_target} PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
  endif()
endforeach()
add_executable(sts2_combat apps/combat/main.cpp)
target_link_libraries(sts2_combat PRIVATE sts2_observation sts2_persistence)

add_executable(sts2_cli src/main.cpp)
target_link_libraries(sts2_cli PRIVATE sts2_simulator)

include(CTest)
option(STS2_BUILD_INTEGRATION "Build native JSON export and seeded run CLI" ON)
if(BUILD_TESTING OR STS2_BUILD_INTEGRATION)
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(nlohmann_json)
  add_library(sts2_integration src/integration/native_run.cpp src/integration/native_combat.cpp src/integration/session.cpp)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sts2_integration PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
  endif()
  target_link_libraries(sts2_integration PUBLIC sts2_observation sts2_persistence nlohmann_json::nlohmann_json)
  add_executable(sts2_run apps/run/main.cpp)
  target_link_libraries(sts2_run PRIVATE sts2_integration)
endif()
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

  add_executable(simulator_test tests/unit/simulator_test.cpp tests/unit/rng_test.cpp tests/unit/clock_test.cpp)
  target_link_libraries(simulator_test PRIVATE sts2_simulator GTest::gtest_main)
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
  add_executable(engine_test tests/unit/engine_test.cpp tests/unit/map_test.cpp tests/unit/setup_test.cpp tests/unit/run_test.cpp tests/parity/seeded_run_test.cpp)
  target_link_libraries(engine_test PRIVATE sts2_integration combat_state nlohmann_json::nlohmann_json GTest::gtest_main)
  target_compile_definitions(engine_test PRIVATE
    KERNEL_SEEDED_SETUP_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/seeded-setup-v1.json"
    KERNEL_RUN_PREFIX_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/fixtures/reference/neow-map/states.json"
    KERNEL_RUN_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/fixtures/reference/run-death/states.json"
    KERNEL_MAP_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/maps-v1.json"
    KERNEL_RNG_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kernel-rng-v1.json"
    KERNEL_SURVIVOR_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kernel-survivor-v1.json"
    KERNEL_COMBAT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/fixtures/reference/combat-victory/states.json"
    KERNEL_COMBAT_ACTIONS="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/fixtures/reference/combat-victory/actions.json")
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
  add_executable(sts2_replay tools/parity/replay_main.cpp)
  target_link_libraries(sts2_replay PRIVATE parity_replay_support sts2_integration)
  add_executable(replay_parity_test tests/parity/replay_parity_test.cpp)
  target_link_libraries(replay_parity_test PRIVATE GTest::gtest_main
    parity_replay_support sts2_simulator OpenSSL::Crypto)
  target_compile_definitions(replay_parity_test PRIVATE
    ACTION_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/examples/starter.actions.json"
    STATE_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/fixtures/starter.states.json")
  foreach(scenario combat-victory combat-victory-dense combat-block-death neow-map clock-zero clock-step clock-limit run-shrinker-victory run-shrinker-death)
    add_test(NAME kernel_replay_${scenario} COMMAND sts2_replay
      --actions "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/fixtures/reference/${scenario}/actions.json"
      --states "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/fixtures/reference/${scenario}/states.json"
      --reference "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/fixtures/reference/${scenario}/reference.json")
    set_tests_properties(kernel_replay_${scenario} PROPERTIES LABELS "engine;replay_parity")
  endforeach()
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME setup_rules_generated COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/rules_compiler/setup.py" --check)
    set_tests_properties(setup_rules_generated PROPERTIES LABELS "engine")
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
  gtest_discover_tests(engine_test PROPERTIES LABELS "engine")
  gtest_discover_tests(replay_parity_test PROPERTIES LABELS replay_parity)
  gtest_discover_tests(state_recording_test PROPERTIES LABELS state_recording)
  gtest_discover_tests(action_recording_test PROPERTIES LABELS action_recording)
  gtest_discover_tests(combat_state_test PROPERTIES LABELS combat_state)
  gtest_discover_tests(simulator_test PROPERTIES LABELS unit)
endif()

option(STS2_BUILD_BENCHMARKS "Build simulation throughput benchmark" OFF)
if(STS2_BUILD_BENCHMARKS)
  add_executable(kernel_benchmark benchmarks/kernel_benchmark.cpp)
  target_link_libraries(kernel_benchmark PRIVATE sts2_observation)
  add_executable(simulation_benchmark benchmarks/simulation_benchmark.cpp)
  target_link_libraries(simulation_benchmark PRIVATE sts2_simulator)
endif()
