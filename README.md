# sts2-simulator

A headless C++23 Slay the Spire 2 simulator for high-throughput AI search and simulation.

The C++23 engine implements native seeded setup for all five characters, standard
map generation and a native Neow → map → Shrinker combat → rewards → map loop,
including natural death. It also supports isolated Survivor choices, clones and
binary combat/run checkpoints. Full-state
reference replays cover those supported paths, including clocks, RNG and history.
Complete-run gameplay remains unfinished; see [engine status](docs/specs/engine/implementation-status.md).

The older `sts2_cli` demo remains separate during migration. Its fixed hand and simple
enemy behavior do not represent the new kernel or faithful game rules.

## Build and run

Requires a C++23 compiler, CMake 3.20+, Make, and Git.
With testing enabled (the default), the first configure fetches GoogleTest v1.17.0
from GitHub and requires network access. The shared combat state tests also fetch
nlohmann/json v3.12.0. JSON also links into the optional native integration tools.
The rules/kernel and combat CLI require neither dependency; disable testing and
`STS2_BUILD_INTEGRATION` to build only those targets.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/sts2_combat --auto

cmake --preset release
cmake --build --preset release
ctest --preset release
./build/release/kernel_benchmark
```

The benchmark measures isolated combat execution with warmed storage and clone resets;
it reports allocation counts and state footprint. It does not establish full-game
throughput or a speedup over the reference. Kernel transitions depend on immutable
compiled rules, with no runtime dependency on `sts2-cli`, JSON, files or game assemblies.

## Tests

Tests use GoogleTest and are individually registered with CTest under the `engine`,
`unit` and `combat_state` labels.
The test framework links only into test executables.

```sh
ctest --preset debug -L engine
ctest --preset debug -L unit
./build/debug/simulator_test --gtest_filter='RngTest.*'
```

For an offline build, provide a local GoogleTest v1.17.0 checkout during configure:
`cmake --preset debug -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest`.
For offline combat state tests also provide
`-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/path/to/json`.
To build without tests or downloading either dependency, configure with `-DBUILD_TESTING=OFF`.

## Correctness reference

`sts2-cli/` is a pinned submodule wrapping the game engine. Initialize it with:

```sh
git submodule update --init --recursive
```

Running the reference requires a local game installation and its own setup; see
[sts2-cli setup](sts2-cli/README.md). It is not needed to build the C++ engine.

Planned differential tests will restore complete combat snapshots, including RNG state,
replay prerecorded player actions and selection decisions in both engines, and compare
state at matching decision points. Seeds reproduce game randomness; they do not select
player actions. An unavailable recorded action is a test failure. Pin the reference
revision and game version in captured fixtures.

`rng.hpp` provides a reproducible local SplitMix64 generator for experiments. It is
not used by the current deterministic demo and does not claim game RNG compatibility.

The installed-DLL state-access check and its verified scope are documented in
[Oracle state access](docs/specs/oracle-state-access-spec.md).

The [run parity state format](docs/specs/run-parity-spec.md) defines the shared run and
combat snapshot schema, validation, identity/order rules, and exact comparison behavior.
Run its offline tests with `ctest --preset debug -L combat_state`. The reference exporter
uses shared, audited combat model storage and a run backend; suspended combat effects
still need coverage. Partial simulator snapshot replay is described below.

The [action-recording format](docs/specs/action-recording-spec.md) defines versioned inputs
using the run-parity state schema. The [oracle recorder](docs/specs/oracle-recording-spec.md)
executes supported sequences against local game DLLs and writes validated state transitions.

Play through the sts2-cli interface while recording gameplay decisions and full state checkpoints:

```sh
python3 tools/oracle/play.py --game-dir '/path/to/game' \
  --output-dir build/recordings/my-session --checkpoint-every 5
```

Use the displayed zero-based indices, `e` to end your turn, and the usual sts2-cli
inspection and save commands. Add `--character Silent` to choose another character.
See [interactive recording](docs/specs/interactive-recording-spec.md) for controls,
output files, and coverage limits.

## Replay parity harness

`replay_parity_test` exercises the full-state replay comparator with actions-v1 and
states-v1 inputs. Run `ctest --preset debug -L replay_parity`. Tests require OpenSSL
Crypto development headers for SHA-256 verification.

`tests/parity/support/replay.hpp` provides a backend interface for restoring the initial
snapshot, action execution (including stable identity resolution), and settled-boundary
snapshot export. The runner validates both recordings, verifies the exact action-file digest and
independently supplied reference provenance, compares initialization and each recorded
checkpoint, and stops at the first divergence. It executes all actions even when
checkpoints are sparse. Differences include the action ID, JSON Pointer, expected value,
and actual value. Exact decimal values and document-local reference normalization use
the existing snapshot comparator.

`sts2_replay` connects that runner to the new engine's canonical wire session.
It initializes from action setup, binds recording identities from its own generated
pile order, and exports every native field for supported scenarios:

```sh
./build/debug/sts2_replay \
  --actions tests/parity/fixtures/reference/combat-victory-dense/actions.json \
  --states tests/parity/fixtures/reference/combat-victory-dense/states.json \
  --reference tests/parity/fixtures/reference/combat-victory-dense/reference.json
```

The reference metadata is captured independently from the running backend. Replay
compares all clocks, RNG, history, powers and monster AI. Unsupported content returns
an explicit error; expected checkpoints never supply initialization or output.
`SimulatorSnapshot` remains a diagnostic adapter for the older scaffold and continues
to reject full-state export.

Verify the canonical recordings independently against the reference with
[reference self-replay](docs/specs/interactive-recording-spec.md#canonical-recording-verification).
The [reference corpus](tests/parity/fixtures/reference/README.md) documents verified
simulator paths and the remaining lifecycle scenarios.
