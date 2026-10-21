# sts2-simulator

A headless C++20 Slay the Spire 2 simulator for high-throughput AI search and simulation.

This repository currently contains a buildable scaffold, not a faithful game implementation.
The demo supports one enemy, attack/block cards, energy, hand removal, turn progression,
and victory/defeat. Each turn refills a fixed hand; draw/discard piles, enemy AI,
relics, powers, and game-compatible RNG are not implemented.

## Build and run

Requires a C++20 compiler, CMake 3.20+, Make, and Git.
With testing enabled (the default), the first configure fetches GoogleTest v1.17.0
from GitHub and requires network access. The shared combat state tests also fetch
nlohmann/json v3.12.0; neither dependency links into the production simulator.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/sts2_cli

cmake --preset release
cmake --build --preset release
ctest --preset release
./build/release/simulation_benchmark
```

The benchmark measures the tiny scaffold combat only, not full-game throughput.
Combat state is trivially copyable and transitions use fixed-capacity storage without
heap allocation. The simulator library has no runtime dependency on sts2-cli.

## Tests

Tests use GoogleTest and are individually registered with CTest under the `unit`
and `combat_state` labels.
The test framework links only into test executables.

```sh
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
[sts2-cli setup](sts2-cli/README.md). It is not needed to build the C++ scaffold.

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

`SimulatorSnapshot` in `tests/parity/support/simulator_snapshot.hpp` is the incremental
C++ adapter. Replay initializes it from the action setup without copying expected
snapshots. `load(initial)` is available only for fixture tests of the modeled HP,
block, energy, turn and Strike/Defend hand cards. `produced_state()` exposes those
modeled fields for diagnostics.

Full parity compares every field in the shared state schema, including clocks, RNG,
history, powers and monster AI. The current C++ engine cannot independently produce
all of these fields, so `snapshot()` reports missing capabilities and replay fails
at the initial checkpoint. It does not preserve expected fields or pass a partial
state as a complete snapshot. Tests verify that changing expected data cannot alter
simulator initialization or supply unmodeled output.
