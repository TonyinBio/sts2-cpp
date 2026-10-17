# sts2-simulator

A headless C++20 Slay the Spire 2 simulator for high-throughput AI search and simulation.

The combat engine implements the audited v0.111.0 Ironclad ascension-0 starter deck
against Shrinker Beetle: Strike, Defend, Bash, ordered piles and reshuffling, energy,
block, Shrink/Vulnerable powers, enemy turns, and victory/defeat with Burning Blood.
Reference action recordings are replayed in C++ and every resulting combat state is
compared exactly against sts2-cli, including RNG words and counters.

This is one supported combat profile, not the full card pool or run progression.
See [simulator design and coverage](docs/simulator.md) for scope and performance details.

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

The benchmark measures supported combat rollouts from 256 varied initial hands,
including branch copying and action enumeration. Setup, reference processes, and JSON
are outside the timing. Combat state is trivially copyable (168 bytes on the tested
x86_64 GCC build), with allocation-free setup/transitions. The simulator library has no
runtime dependency on sts2-cli or JSON.

## Tests

Tests use GoogleTest and are individually registered with CTest under the `unit`,
`combat_state`, and `parity` labels.
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
[sts2-cli setup](sts2-cli/README.md). It is not needed to build the C++ engine or run recorded parity tests.

Differential tests import the initial combat snapshot and RNG state, replay recorded
player decisions once, and compare every resulting state. An unavailable action or
state mismatch fails immediately. Reference revision, game hash, and action-file digest
are checked before replay. No expected intermediate state is used to repair the simulation.

`rng.hpp` implements the inspected game's xoshiro256** generator, SplitMix64 seed
initialization, and bounded-draw arithmetic used by shuffling.

The installed-DLL state-access check and its verified scope are documented in
[Oracle state access](docs/oracle-state-access.md).

The [combat state format](docs/combat-state-format.md) defines the shared combat-state
schema, validation, identity/order rules, and exact comparison behavior. Run its offline
tests with `ctest --preset debug -L combat_state`.

The [action-recording format](docs/action-recording-format.md) defines versioned inputs
using the existing combat-state schema. The [oracle recorder](docs/oracle-recording.md)
executes supported sequences against local game DLLs and writes validated state transitions.

Replay recorded reference states without a game installation:

```sh
./build/debug/parity_replay tests/parity/examples/starter.actions.json \
  tests/parity/fixtures/starter.states.json
ctest --preset debug -L parity
```

Generate new reference traces and compare every C++ transition against the installed DLL:

```sh
python3 tools/oracle/check_simulator.py \
  --game-dir '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2' \
  --episodes 32 --policy-seed 17
```
