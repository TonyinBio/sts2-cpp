# Stage two: compact combat state

## Start here

Continue the design discussion before implementing. Stage one is complete. The
user wants to prevent combat history, completed plays and expired powers from
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

- Full gameplay parity remains mandatory. Reference storage layout is not itself
  a gameplay requirement, but removed information must have no effect on future
  gameplay or player-visible results.
- Explore replacing event history with sufficient counters, flags and per-instance
  metadata; capture values needed by suspended effects in the continuations;
  reclaim completed plays and expired powers only when no remaining references
  require them.
- Names for `CombatHistoryEntry`, `CardPlayRecord` and `PowerRecord` are unresolved.
  Earlier proposed `CardPlayHistory`/`PowerHistory` renames were explicitly deferred.
  `PowerRecord` includes active power state, so calling it purely history is misleading.
- The user considers historical comparison fields potentially unnecessary because
  recorded state history may serve the same purpose. This is a direction to
  investigate, not approval to delete all history/play/power comparison coverage.
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
| `include/sts2/state/history.hpp` | Event entries, captured card plays, and active/inactive power instances |
| `include/sts2/state/combat.hpp` | Inline capacities: 64 events, 16 plays, 8 powers; four effect frames; collection validation enumeration |
| `src/engine/combat.cpp` | History append, power application/ticking, play capture, effect execution, terminal cleanup and validation |
| `src/integration/native_combat.cpp` | Privileged export resolves history/play/power relationships through stable indices |
| `src/persistence/checkpoint.cpp` | Combat semantic codec, including suspended execution and referenced objects |
| `src/persistence/run_checkpoint.cpp` | Run codec embeds the active-combat checkpoint |
| `tests/unit/engine_test.cpp` | Combat behavior, reference snapshots and suspended choices |
| `tests/unit/state_clone_test.cpp` | Overflow, independent futures, failure guarantees and detached source references |
| `tests/unit/run_test.cpp` | Run lifecycle, restoration, identity reuse and public decisions |

Current supported combat does not query accumulated events or completed plays to
calculate card effects. They currently support history/export relationships,
validation and object reachability; effect frames carry a play index. Active
powers do participate in current execution. The retention contract also protects
history needed by audited mechanics that are not implemented yet.

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

## Contract changes that need resolution

[ADR 0003](../adr/0003-inline-state-with-correct-overflow.md) and the
[retention specification](../specs/engine/inline-state-retention.md) currently require
retaining represented histories and referenced objects rather than substituting
aggregate counters. Explicitly revise the relevant retention decision when an
equivalent representation is agreed; keep its memcpy and independent-overflow
requirements intact. Preserve [ADR 0001](../adr/0001-separate-complete-state-from-observations.md):
complete state and public observations serve different purposes.

Current privileged snapshots include `/combat/history`, `/combat/card_plays` and
power objects, including retained inactive instances. Compact state cannot
reconstruct arbitrary discarded event logs. For each proposed removal, distinguish:

1. Information required for future execution or player observations.
2. Information retained only to reproduce reference diagnostic object graphs.
3. Information independently preserved by recorded checkpoints/actions.

Clarify what the user means by "state history." Sparse checkpoints and player
actions do not automatically preserve all internal events or suspended boundaries.
Decide exactly which comparison fields can be excluded or replaced, how the schema
and comparison revisions change, and which regressions still verify their gameplay
effects. Do not keep fabricated empty logs while claiming exact historical parity.
Live power coverage must remain even if inactive diagnostic objects disappear.

Inspect [ADR 0004](../adr/0004-use-json-for-restorable-checkpoints.md) and the current
codec before changing checkpoints. Stage one deliberately did not implement its
separate migration. Resolve compatibility/version implications of any stage-two
data change rather than silently bundling that migration into history compaction.

## Remaining design round

After the factual audit, present the expected speed trade-offs: cheap counters
versus repeated scans; smaller clones versus added per-event bookkeeping; inline
continuation size versus referenced play storage; and safe power reuse versus
compaction/index-repair cost. Do not make benchmark work a prerequisite.

Then settle:

- Exact sufficient state for each audited consumer and reset boundary.
- What a suspended play must capture, and when source/play/power objects may be
  reclaimed without invalidating references or changing dispatch order.
- Diagnostic fields/schema/comparator changes and the role of recorded state history.
- Type/collection names after responsibilities are clear.
- Required regression scenarios and checkpoint compatibility behavior.

Only after shared understanding is confirmed, implement the agreed slice and run
appropriate engine/replay checks, then the full suite. Keep verbose output in logs.
Verify counters and identities through meaningful behavior and independent
clone/restore futures; do not treat weakened snapshot comparisons alone as proof
of gameplay parity. Extend coverage alongside mechanics without claiming that
source audit or one bounded encounter certifies complete-game parity.

## Diagram caveats

The root `simulator-*.mmd` files were drawn after stage one. The user identified
presentation inconsistencies: observation is still a separate source layer despite
being grouped under Engine in the diagram, and schemas were omitted from
RecordingContracts even though they still exist. Those were diagram errors, not
refactor changes; do not infer moved validation or deleted schemas from them.
`sts2_run_scenario` predates the refactor and remains used by the consecutive-combat
regression. `sts2_run` and `sts2_combat` were explicitly removed earlier.
