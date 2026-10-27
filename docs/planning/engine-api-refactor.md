# Engine API refactor

Stage one implemented and verified. Stage two remains deferred.

Continue with the [stage-two handoff](stage-two-handoff.md).

## Confirmed

- Rename the `kernel/` source/header layer to `engine/`; keep namespace
  `sts2::engine`.
- Replace the `Simulator` class with combat free functions in `engine/combat.hpp`;
  expose run transitions through `engine/run.hpp`.
- Rename `State` to `CombatState`, state views to `CombatView` and
  `ConstCombatView`, `Input`/`InputKind` to `CombatInput`/`CombatInputKind`,
  `Phase` to `CombatPhase`, and combat `ObservationView` to
  `CombatObservationView`.
- Use the suffix `Data` for structures representing byte-copyable state storage,
  instead of `Record`; examples are `RunData` and `CombatData`.
- Eliminate `SetupState` and the separate setup record/view representation. Flatten
  persistent inventory, RNG, relic bags, act queues and reward odds directly into
  `RunData`, rather than introducing a nested `SetupData`.
- Refactor initialization and reward generation to operate on run state directly;
  remove APIs that take or produce a standalone setup state.
- Initialization and reward generation access flattened fields through `RunView`.
  Keep one run overflow owner and a separate combat overflow owner. Preserve
  rejection purity and initialization/clone failure guarantees; local staging of
  reward RNG/scalars and the initializer's temporary run remain implementation tools.
- Leave `Scratch` and `Limits` names unchanged.
- Rename storage types `PoolRecord`, `RelicBagRecord` and `ActRoomsRecord` to
  `PoolData`, `RelicBagData` and `ActRoomsData`. Rename storage members and
  helpers from record terminology to data terminology.
- Defer naming changes for `CombatHistoryEntry`, `CardPlayRecord` and
  `PowerRecord` to stage two; keep their current names in stage one.
- Replace run-level `apply_run` and `apply_combat` with
  `apply_input(RunState&, const PlayerInput&, Scratch&)`. Move the input variant
  into `engine/run.hpp`. Dispatch run and combat inputs internally, preserving
  one historical transition per call, combat/run synchronization and explicit
  `proceed` boundaries. `apply_player` continues through forced transitions to
  the next player decision using the same authoritative transitions.

- Combat free functions retain optional rules parameters with pinned defaults;
  initialization also accepts `Limits`. Existing run callers retain defaults.
- Rename the CMake target `sts2_kernel` to `sts2_engine` alongside the directories.

## Verification

- Debug configuration and full build passed (exit 0); combat free-function
  callers and the renamed target compile without warnings.
- Optional `kernel_benchmark` and `public_benchmark_worker` consumers compile
  (exit 0); no speed measurement was performed.
- `ctest --preset debug -L engine -j 2 --output-on-failure`: 85/85 passed,
  including 13 replay-parity checks (exit 0).
- Pre-refactor initial run and seeded-combat checkpoints restore successfully;
  fresh initialization and re-encoding produce the same bytes (exit 0).
- Added an exhausted-clock reward regression: no reward RNG, odds or offers are
  published when the transition fails before generation.
- `ctest --preset debug -j 2 --output-on-failure`: 173/173 passed (exit 0).
- `sts2_play` startup/quit smoke check passed (exit 0).

Complete build/test logs are in `/tmp/sts2-stage1-*.log`. Checkpoint compatibility
samples and the standalone verification helper are in `/tmp/sts2-stage1-baseline/`.
Changed code/spec files pass `git diff --check`; the existing trailing whitespace
in the user-edited performance directives in `AGENTS.md` is outside this refactor.

## Staging

1. Complete naming/API changes and setup flattening while preserving behavior and
   existing history/play/power storage and comparison contracts.
2. Separately audit and compact gameplay history, capture necessary suspended-play
   values and reclaim expired powers when no longer referenced. Full gameplay
   parity remains required. Discuss the expected speed impact before implementing
   this stage; the user does not request performance-validation work. Resolve type
   names and diagnostic export/schema changes here. The user considers historical
   exports potentially redundant with recorded state history; verify the exact
   information preserved before removing fields. Stage one leaves them unchanged.

Preserve existing gameplay, overflow ownership, clone behavior, checkpoint formats
and reference comparison semantics. This refactor does not establish broader parity.
