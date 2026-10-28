# Stage two: compact combat state

## Start here

The design discussion is settled and the user explicitly authorized implementation
("Alright do that"). Stage one is complete. Stage two is implemented and verified for supported content; see the current
implementation and verification below. The user wants to prevent combat history, completed plays and expired powers from
making simulation state grow with combat duration, while retaining complete
gameplay parity. They proposed mechanic-specific counters, self-contained suspended
effects and reclaiming expired powers.

The user wants to avoid slowing simulation. First explain the likely speed
implications and trade-offs. The user explicitly
does **not** want a performance-validation workstream, new benchmark requirements
or a measured no-regression gate. Discuss expected effects on transition cost,
clone size, allocation and reclamation; distinguish reasoning from measured facts.
Do not promise a speed improvement or constant-size state without evidence.

The design interview uses the user-invoked `grill-with-docs` workflow: apply the
grilling and domain-modeling skills, ask unresolved decisions in rounds, capture
settled decisions, and confirm shared understanding before implementation. Look
up implementation facts rather than asking the user to supply them.

## Settled scope and boundaries

- Module name settled: rename `integration` to `json_io` (source/header paths,
  CMake target `sts2_json_io` and option `STS2_BUILD_JSON_IO`). It handles JSON
  actions, replay sessions and comparison export; persistence remains separate.
  The mechanical rename is implemented and verified by the Debug configure/build
  and all 173 Debug tests. Logs: `/tmp/sts2-json-io-{configure,build,tests}.log`.
  Engine history compaction and exporter-owned tracking are now implemented;
  stage-two verification results are recorded separately below.

- Resumed design decisions: replace history with mechanic-specific counters,
  flags or per-instance metadata where equivalent future gameplay is established;
  retain richer state wherever needed. This supersedes the direction to retain
  the original history representation, not the requirement for full parity.
- Latest decision supersedes the earlier schema-removal proposal: do not change
  the comparison JSON schema or its historical fields. The C++ engine should use
  equivalent counters rather than accumulated histories; the exporter module
  should track the histories needed to emit the existing schema. This is the implemented engine/exporter ownership boundary. Existing recordings
  retain their comparison contract; no fixture regeneration is required merely
  to remove history fields, because those fields remain.
- Mechanic-specific replacement counters are internal simulator state, not new
  fields in the reference-comparison JSON schema. Do not require the reference
  engine to expose matching counters or derive synthetic reference fields for
  them. Verify equivalence through resulting gameplay and observations, including
  history-dependent behavior. Internal clone/restoration must still preserve the
  counters; this comparison-schema decision does not exclude them from complete
  restorable checkpoints.
- Full gameplay parity remains mandatory. Reference storage layout is not itself
  a gameplay requirement, but removed information must have no effect on future
  gameplay or player-visible results.
- Explore replacing event history with sufficient counters, flags and per-instance
  metadata; capture values needed by suspended effects in the continuations;
  reclaim completed plays and expired powers only when no remaining references
  require them.
- Names now follow responsibilities: transient `CombatEvent`, exporter-owned
  `RecordedCardPlay` and runtime `PowerInstance`. Engine event/completed-play
  collections are removed; active power state is not called history.
- Historical comparison coverage remains intact, including play/power identities
  and relationships. Resolve exporter-owned object lifetimes and reference closure
  independently from gameplay ownership. Keep targeted regressions for
  history-dependent gameplay even when historical comparisons pass.
- Exporter lifecycle decision: attach the exporter when creating a run; the C++
  JSON exporter tracks diagnostic histories itself from that point. Recording a
  run from a pre-made state may remain unsupported. Seeding exporter history from
  a JSON save that contains it is a possible later capability, not required scope.
  Do not require arbitrary late attachment or reconstruction of discarded history
  from counters. Exporter branching is resolved by Q4 below; engine clones do not
  carry diagnostic history.
- Q3 settled: the exporter is optional. Ordinary simulation and MCTS run without
  it and without diagnostic-history storage or allocation.
- Q4 settled: recording supports one linear execution. Engine clones preserve
  complete gameplay state but do not inherit exporter history; recording cloned
  branches is unsupported in this slice.
- Q5 settled: internal state changes may invalidate older engine checkpoints.
  Reject incompatible versions explicitly. Keep the existing checkpoint encoding
  for this work; the JSON checkpoint migration remains separate. Parity schemas
  and action recordings are unaffected by checkpoint-version changes.
- Preserve live powers, pending effects, relevant identities, RNG, clocks, public
  observations and deterministic continuation. Narrow current content coverage
  does not establish full-game correctness.
- Do not conflate this work with unrelated checkpoint-format migration or further
  module/API cleanup.

## Stage-one baseline

See [the refactor plan](engine-api-refactor.md) for confirmed changes and checks.

- `kernel/` became `engine/`; CMake target is `sts2_engine`; namespace remains
  `sts2::engine`.
- Combat entry points are free functions in `include/sts2/engine/combat.hpp`.
  Configurable rules and initialization limits retain their existing defaults.
- `CombatState` owns `CombatData` and combat overflow. `RunState` owns flattened
  `RunData`, one run overflow owner and one active-combat overflow owner.
- `SetupState` and the nested setup representation are gone. Initialization and
  rewards use `RunView` for persistent run fields.
- `apply_input(RunState&, const PlayerInput&, Scratch&)` executes one historical
  transition. `apply_player` additionally advances forced transitions. Preserve
  that distinction for recording/checkpoint correspondence.
- History, plays, powers, diagnostic comparison fields and checkpoint encoding
  were left unchanged in stage one.

Recorded verification: full Debug build; optional benchmark consumers compiled
without running speed measurements; 85 engine checks and all 173 Debug tests
passed; pre-refactor initial run/combat checkpoints restored and re-encoded to
identical bytes. These are previous results, not certification of future changes.
Logs are `/tmp/sts2-stage1-*.log`; checkpoint samples and helper sources are under
`/tmp/sts2-stage1-baseline/`. Temporary artifacts may not survive another environment.

The working tree already contains substantial user and earlier-session changes,
including deleted legacy apps/scaffolding and untracked docs/fixtures. Inspect the
current diff and preserve unrelated work; do not reset to HEAD as a starting point.

## Current implementation

| Location | Relevant behavior |
| --- | --- |
| `include/sts2/state/combat_types.hpp` | Transient diagnostic events, hit results and live power instances; no history collections |
| `include/sts2/state/combat.hpp` | Compact counters, per-card turn/per-creature hit metadata, four self-contained frames and fixed power-type slots with application order |
| `src/engine/combat.cpp` | Counter updates, optional synchronous trace, safe supported-power retirement, execution and validation |
| `src/json_io/native_run.cpp`, `native_combat.cpp` | Exporter-owned event/completed-play/removed-power history and unchanged privileged comparison output |
| `src/persistence/checkpoint.cpp` | Binary revision 6 includes counters, per-instance metadata, frames and live powers; old revisions rejected |
| `src/persistence/run_checkpoint.cpp` | Binary revision 4 embeds active combat; no exporter history |
| `tests/unit/engine_test.cpp` | Combat behavior, reference snapshots and suspended choices |
| `tests/unit/state_clone_test.cpp` | Overflow, independent futures, failure guarantees and detached source references |
| `tests/unit/run_test.cpp` | Run lifecycle, restoration, identity reuse and public decisions |

Engine state no longer contains event/completed-play history or inactive powers.
The optional `CombatTrace` lives in caller-owned execution scratch (with explicit
initialization/scenario arguments), never cloned state. `Session` owns the exporter
and rebinds trace context when moved. Run-level entry synchronizes exporter boundary
metadata after rebasing combat to the run clock/boundary. Cloned/restored gameplay
branches use independent unrecorded scratch; recording branches is unsupported.
Supported powers have no pending callbacks or other gameplay references after
expiration, so final diagnostic updates precede stable ordered removal.

The fixed summaries represent the audited candidate queries under the supported
single-player turn model. Unsupported mechanics, mutable card types, extra turns,
multiplayer identities, richer ordered queries and detached model lifetimes still
need source verification alongside implementation. Full-game sufficiency is not
claimed. Schema files, comparators, reference adapters and fixture bytes are unchanged.

## Consumer audit already performed

The following are candidate sufficient representations, **not** a completed
full-game proof. Most listed mechanics remain unsupported in the current engine.
Recheck their source and interaction requirements before selecting replacements.

| Consumer | Candidate retained information |
| --- | --- |
| Normality | Owner's started-play count this turn; started and finished are distinct |
| Ringing | Whether the owner started a play this turn |
| Juggling | Owner's Attack-start count this turn, including plays before application |
| Feral | Owner's zero-energy Attack-start count this turn, including plays before application |
| DeathMarch | Owner's non-hand-draw count this turn |
| GoldAxe | Combat-wide finished-play count |
| TearAsunder | Positive-unblocked hit count per receiving creature |
| BansheesCry | Owner's combat-wide finished-ethereal count, capturing ethereal at finish |
| Bolas | The exact card instance's last finished player-turn identity |

Important qualifications:

- Feral/Juggling inspect card type through a live card reference. A counter updated
  only at play start assumes type cannot later change; prove that assumption or
  preserve equivalent behavior when properties change.
- Turn queries distinguish round, side and player-turn identity. A single round
  counter is insufficient.
- A removed power may still receive a callback from an already captured dispatch
  list. Expiration/removal alone does not establish safe reclamation. Preserve
  pending callback/effect/modifier references and observable iteration order.
- Detached cards may remain required by Nightmare, stolen-card powers or
  continuations independently of event history.
- Other full-game consumers may need ordered results, identities, value flags,
  captured resources or variable-length state. Do not replace all history with
  counters merely because the current supported subset permits it.

Primary starting evidence:

- [History and dispatch lifetimes](instance-lifetimes/history-dispatch.md), especially
  consumer queries, live-reference predicates and captured dispatch lifetimes.
- [Combat runtime audit](../specs/combat-runtime-audit.md), especially history that
  affects gameplay and floating-card ownership.
- [Instance lifetime evidence index](instance-lifetimes/README.md).
- Local decompiled evidence under `build/combat-state-audit/decompiled/`, if present.
  Preserve source/build provenance. Consult `tools/README.md` before any live audit.

## Contracts and verification

ADR 0003 and the retention specification now permit sufficient summaries instead
of original histories, preserve independent memcpy/overflow clones, and distinguish
optional current-combat exporter tracking from completed-encounter archives.
ADR 0004's JSON checkpoint migration remains deferred. Engine checkpoint revisions
are combat 6 and run 4; incompatible old formats are rejected explicitly.

New regressions cover started-versus-finished counts during suspension, captured
operands, independent clone/restore futures, turn reset boundaries, per-card finish
identity and per-receiver hit counts; a 100-turn supported encounter verifies no
growing engine storage or overflow allocations. This is a storage/lifetime check,
not a speed benchmark or proof about all encounters. Exporter tests cover expiration
and reapplication identities, detached power diagnostics, no branch contamination,
invalid-input history purity and move/rebinding behavior. Existing exact historical
comparisons remain enabled.

Stage-two original compaction verification (before the fixed-slot follow-up):

- Debug build passed, including optional benchmark consumers; no speed benchmarks ran.
- Core-only build passed with `BUILD_TESTING=OFF`, `STS2_BUILD_JSON_IO=OFF` and
  `STS2_BUILD_PARITY_TOOLS=OFF`, using `/usr/bin/g++` explicitly. No JSON dependency
  enters the core build.
- Initial focused run: 35/36 passed; scenario-boundary synchronization was fixed.
- Initial engine-label run exposed two adapter/test issues: branch checks shared
  the live exporter trace, and the scenario tool initialized by copying an already
  recorded state. Both now initialize/record a linear execution and use independent
  unrecorded scratch for gameplay clone/restore checks.
- Affected-test recheck passed all 3 checks: old-version rejection, dense/sparse
  public replay and consecutive scenarios.
- Final `ctest --preset debug`: all 178 tests passed, including the unchanged
  historical comparisons, replay/schema contracts and recorder tests.
- Scoped diff whitespace checks and final compiler warning checks passed. Existing
  unrelated changes (including AGENTS.md whitespace) were preserved.

Logs:
- `/tmp/sts2-compact-build.log`
- `/tmp/sts2-compact-core-system-configure.log`, `/tmp/sts2-compact-core-build.log`
- `/tmp/sts2-compact-targeted.log`, `/tmp/sts2-compact-engine-tests.log` (initial runs)
- `/tmp/sts2-compact-recheck.log` (affected checks passed)
- `/tmp/sts2-compact-full-tests.log` (all 178 passed)

The initial core-only configure selected an unrelated FSL/Conda compiler and failed
its compiler link probe; explicitly selecting the same system compiler as Debug
resolved that environment issue. No source workaround was needed.

Remaining work is the separately deferred JSON checkpoint migration and extending
mechanics/content with their corresponding lifetime/source audits. There is no
remaining compaction implementation task for the currently supported slice. Full
runs and all-game sufficiency are not claimed. No new benchmark gate is required.

## Diagram caveats

The root `simulator-*.mmd` files were drawn after stage one. The user identified
presentation inconsistencies: observation is still a separate source layer despite
being grouped under Engine in the diagram, and schemas were omitted from
RecordingContracts even though they still exist. Those were diagram errors, not
refactor changes; do not infer moved validation or deleted schemas from them.
`sts2_run_scenario` predates the refactor and remains used by the consecutive-combat
regression. `sts2_run` and `sts2_combat` were explicitly removed earlier.

## Fixed-slot follow-up

The combined commit also includes ADR 0005: implemented power types use fixed
creature slots with explicit application order; duplicated Vulnerable/Shrink
creature caches are removed. Combat checkpoint revision is now 6. The follow-up
verification logs are `/tmp/sts2-fixed-powers-engine-tests.log` and
`/tmp/sts2-fixed-powers-other-tests.log`; commit-time build and focused checks are
in `/tmp/sts2-commit-build.log` and `/tmp/sts2-commit-tests.log`.
