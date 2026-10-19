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
supports the audited starter encounter; simulator fixture replay is not implemented yet.

The [action-recording format](docs/specs/action-recording-spec.md) defines versioned inputs
using the run-parity state schema. The [oracle recorder](docs/specs/oracle-recording-spec.md)
executes supported sequences against local game DLLs and writes validated state transitions.
