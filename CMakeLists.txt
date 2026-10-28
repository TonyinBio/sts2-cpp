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
# the rules/engine targets. The original demo remains isolated during migration.
add_library(sts2_rules src/rules/ruleset.cpp src/rules/setup.g.cpp)
target_include_directories(sts2_rules PUBLIC include)
target_compile_features(sts2_rules PUBLIC cxx_std_23)
add_library(sts2_engine src/engine/action.cpp src/state/rng.cpp src/engine/combat.cpp src/engine/systems/map.cpp src/engine/systems/setup.cpp src/engine/systems/rewards.cpp src/engine/run.cpp src/engine/run_validation.cpp)
target_link_libraries(sts2_engine PUBLIC sts2_rules)
add_library(sts2_observation src/observation/observe.cpp src/observation/run.cpp)
target_link_libraries(sts2_observation PUBLIC sts2_engine)
add_library(sts2_persistence src/persistence/checkpoint.cpp src/persistence/run_checkpoint.cpp)
target_link_libraries(sts2_persistence PUBLIC sts2_engine)
foreach(engine_target sts2_rules sts2_engine sts2_observation sts2_persistence)
  set_target_properties(${engine_target} PROPERTIES CXX_EXTENSIONS OFF)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${engine_target} PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
  endif()
endforeach()
add_executable(sts2_play apps/play/main.cpp)
target_link_libraries(sts2_play PRIVATE sts2_observation)

add_executable(sts2_cli src/main.cpp)
target_link_libraries(sts2_cli PRIVATE sts2_simulator)

include(CTest)
option(STS2_BUILD_JSON_IO "Build JSON input/output and scenario tooling" ON)
option(STS2_BUILD_PARITY_TOOLS "Build recording validator and simulator replay CLI" ON)
if(BUILD_TESTING OR STS2_BUILD_JSON_IO OR STS2_BUILD_PARITY_TOOLS)
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(nlohmann_json)
  add_library(sts2_json_io src/json_io/native_run.cpp src/json_io/native_combat.cpp src/json_io/session.cpp)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sts2_json_io PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
  endif()
  target_link_libraries(sts2_json_io PUBLIC sts2_observation sts2_persistence nlohmann_json::nlohmann_json)
  add_executable(sts2_run_scenario tools/parity/run_scenario_main.cpp)
  target_link_libraries(sts2_run_scenario PRIVATE sts2_json_io)
endif()
# Recording contracts are shared by tools and tests, independent of GoogleTest.
if(BUILD_TESTING OR STS2_BUILD_PARITY_TOOLS)
  set(PARITY_SCHEMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data/schemas")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PARITY_SCHEMA_DIR}/run-parity-v1.schema.json"
    "${PARITY_SCHEMA_DIR}/serializable-run-v20.schema.json"
    "${PARITY_SCHEMA_DIR}/actions-v1.json"
    "${PARITY_SCHEMA_DIR}/states-v1.json"
    "${PARITY_SCHEMA_DIR}/journal-v1.json")
  file(READ "${PARITY_SCHEMA_DIR}/run-parity-v1.schema.json" RUN_PARITY_SCHEMA)
  file(READ "${PARITY_SCHEMA_DIR}/run-parity-v2.schema.json" GAMEPLAY_PARITY_SCHEMA)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PARITY_SCHEMA_DIR}/run-parity-v2.schema.json")
  file(READ "${PARITY_SCHEMA_DIR}/serializable-run-v20.schema.json" NATIVE_RUN_SCHEMA)
  file(READ "${PARITY_SCHEMA_DIR}/actions-v1.json" ACTIONS_SCHEMA)
  file(READ "${PARITY_SCHEMA_DIR}/states-v1.json" STATES_SCHEMA)
  file(READ "${PARITY_SCHEMA_DIR}/journal-v1.json" JOURNAL_SCHEMA)
  configure_file(tools/parity/src/schema.hpp.in generated/parity/schema.hpp @ONLY)
  add_library(sts2_parity STATIC tools/parity/src/snapshot.cpp
    tools/parity/src/schema_validation.cpp tools/parity/src/recording.cpp tools/parity/src/state_recording.cpp)
  target_compile_features(sts2_parity PUBLIC cxx_std_23)
  target_include_directories(sts2_parity PUBLIC tools/parity/include
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/parity")
  target_link_libraries(sts2_parity PUBLIC nlohmann_json::nlohmann_json)

  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  add_library(sts2_parity_replay STATIC tools/parity/src/replay.cpp)
  target_link_libraries(sts2_parity_replay PUBLIC sts2_parity PRIVATE OpenSSL::Crypto)
  # The original simulator adapter stays separate from contract validation/replay.
  add_library(sts2_parity_simulator STATIC tools/parity/src/simulator_snapshot.cpp)
  target_link_libraries(sts2_parity_simulator PUBLIC sts2_parity sts2_simulator)
  foreach(parity_target sts2_parity sts2_parity_replay sts2_parity_simulator)
    set_target_properties(${parity_target} PROPERTIES CXX_EXTENSIONS OFF)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(${parity_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endforeach()
  add_executable(parity_validate tools/parity/validate_main.cpp)
  target_link_libraries(parity_validate PRIVATE sts2_parity)
  add_executable(sts2_replay tools/parity/replay_main.cpp)
  target_link_libraries(sts2_replay PRIVATE sts2_parity_replay sts2_json_io)
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
  add_executable(inline_storage_test tests/unit/inline_storage_test.cpp)
  target_link_libraries(inline_storage_test PRIVATE sts2_engine GTest::gtest_main)
  add_executable(engine_test tests/unit/engine_test.cpp tests/unit/map_test.cpp tests/unit/setup_test.cpp tests/unit/run_test.cpp tests/unit/state_clone_test.cpp tests/parity/seeded_run_test.cpp)
  target_link_libraries(engine_test PRIVATE sts2_json_io sts2_parity nlohmann_json::nlohmann_json GTest::gtest_main)
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
  target_link_libraries(combat_state_test PRIVATE sts2_parity GTest::gtest_main)
  target_compile_definitions(combat_state_test PRIVATE
    PARITY_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/examples/starter.json")
  add_executable(action_recording_test tests/parity/recording_test.cpp)
  target_link_libraries(action_recording_test PRIVATE sts2_parity GTest::gtest_main)
  target_compile_definitions(action_recording_test PRIVATE
    ACTION_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/examples/starter.actions.json")
  add_executable(state_recording_test tests/parity/state_recording_test.cpp)
  target_link_libraries(state_recording_test PRIVATE sts2_parity GTest::gtest_main)
  target_compile_definitions(state_recording_test PRIVATE
    PARITY_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/examples/starter.json"
    ACTION_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/examples/starter.actions.json")
  add_executable(replay_parity_test tests/parity/replay_parity_test.cpp)
  target_link_libraries(replay_parity_test PRIVATE GTest::gtest_main
    sts2_parity_replay sts2_parity_simulator sts2_simulator OpenSSL::Crypto)
  target_compile_definitions(replay_parity_test PRIVATE
    ACTION_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/examples/starter.actions.json"
    STATE_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/fixtures/starter.states.json")
  foreach(scenario combat-victory combat-victory-dense combat-block-death neow-map clock-zero clock-step clock-limit run-shrinker-victory run-shrinker-death run-shrinker-victory-public run-shrinker-death-public run-shrinker-interface-skip-public)
    add_test(NAME kernel_replay_${scenario} COMMAND sts2_replay
      --actions "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/fixtures/reference/${scenario}/actions.json"
      --states "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/fixtures/reference/${scenario}/states.json"
      --reference "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/fixtures/reference/${scenario}/reference.json")
    set_tests_properties(kernel_replay_${scenario} PROPERTIES LABELS "engine;replay_parity")
  endforeach()
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME kernel_consecutive_scenarios COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/oracle/check_consecutive_combats.py"
      --recording-dir "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/fixtures/reference/consecutive-shrinker-scenarios"
      --simulator "$<TARGET_FILE:sts2_run_scenario>" --validator "$<TARGET_FILE:parity_validate>")
    set_tests_properties(kernel_consecutive_scenarios PROPERTIES LABELS "engine;replay_parity")
    add_test(NAME setup_rules_generated COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/rules_compiler/setup.py" --check)
    set_tests_properties(setup_rules_generated PROPERTIES LABELS "engine")
    add_test(NAME run_parity_schema COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/test_run_parity_schema.py")
    add_test(NAME native_run_unit COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracle/test_native_run.py")
    set_tests_properties(native_run_unit PROPERTIES LABELS "native_run")
    add_test(NAME interactive_recorder_unit COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracle/test_play.py" "$<TARGET_FILE:parity_validate>")
    set_tests_properties(interactive_recorder_unit PROPERTIES LABELS "recorder")
    add_test(NAME recorder_profiler_unit COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracle/test_profile_play.py")
    set_tests_properties(recorder_profiler_unit PROPERTIES LABELS "recorder")
  endif()
  include(GoogleTest)
  gtest_discover_tests(inline_storage_test PROPERTIES LABELS engine)
  gtest_discover_tests(engine_test PROPERTIES LABELS "engine")
  gtest_discover_tests(replay_parity_test PROPERTIES LABELS replay_parity)
  gtest_discover_tests(state_recording_test PROPERTIES LABELS state_recording)
  gtest_discover_tests(action_recording_test PROPERTIES LABELS action_recording)
  gtest_discover_tests(combat_state_test PROPERTIES LABELS combat_state)
  gtest_discover_tests(simulator_test PROPERTIES LABELS unit)
endif()

option(STS2_BUILD_BENCHMARKS "Build simulation throughput benchmark" OFF)
if(STS2_BUILD_BENCHMARKS)
  if(TARGET sts2_json_io)
    add_executable(public_benchmark_worker benchmarks/public_worker.cpp)
    target_link_libraries(public_benchmark_worker PRIVATE sts2_json_io)
  endif()
  add_executable(kernel_benchmark benchmarks/kernel_benchmark.cpp)
  target_link_libraries(kernel_benchmark PRIVATE sts2_engine)
  add_executable(simulation_benchmark benchmarks/simulation_benchmark.cpp)
  target_link_libraries(simulation_benchmark PRIVATE sts2_simulator)
endif()
