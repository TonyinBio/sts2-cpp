# Full-game simulator file and class architecture

**Status:** proposed greenfield architecture in C++23. This sketch implements the
[full-game design](full-game-simulator-design.md); it makes no compatibility commitments
to the current scaffold, file layout, or interfaces. Names below are proposed names,
not implemented classes. The design's parity requirements take precedence over layout
and performance targets.

Build a small deterministic kernel around compact state records, a resumable effect
machine, and shared immutable rules. Content is data and indexed handlers rather than
an inheritance tree of cards, enemies, or rooms. Keep observations, text formatting,
serialization, and reference-game tooling outside the transition path.

The implemented setup, map and combat slices are described in [implementation status](implementation-status.md);
full-game acceptance remains pending.

## 1. Proposed files and dependency boundaries

Public headers live under `include/sts2/`; implementation files under `src/` mirror
their module paths. Prefer one header/source pair per responsibility below, rather
than one file per small record. Header-only templates and plain records need no `.cpp`.

```text
CMakeLists.txt
include/sts2/
  core/
    ids.hpp                 Typed definition IDs, handles, boundary IDs
    result.hpp              Status codes and diagnostic references
    storage.hpp             Indexed pools, offset vectors, arena interfaces
    numeric.hpp             Explicit game arithmetic and conversion helpers
  rules/
    ruleset.hpp             Immutable ruleset facade and identity
    definitions.hpp         Card/entity/relic/potion/power definitions
    programs.hpp            Instructions, program tables, handler IDs
    generation.hpp          Eligibility, weighted pools, generation tables
    text.hpp                English templates and token definitions
    coverage.hpp            Supported content and configuration manifest
  state/
    state.hpp               State owner and complete-state root
    run.hpp                 Player, deck, progression, history, modifiers
    map.hpp                 Map topology, assignments, traversal, visibility
    room.hpp                Tagged room-local state and transactions
    combat.hpp              Entities, zones, powers, AI, encounter counters
    instances.hpp           Mutable instance records and ordered memberships
    rng.hpp                 Exact stream states and game sampling operations
    execution.hpp           Frames, pending operations, dispatch cursors
    decision.hpp            Phase, prompt state, selection constraints
    clone.hpp               Deep clone and capacity reuse
  engine/
    combat.hpp              Combat initialization, validation, application and advance
    run.hpp                 Run initialization and unified apply_input dispatch
    action.hpp              Tagged input payloads and borrowed selection spans
    validation.hpp          Boundary and legality checks, prepared action
    effects.hpp             Interpreter and native-handler execution context
    triggers.hpp            Ordered event subscriptions and dispatch
    mutations.hpp           Shared state mutation primitives
    scratch.hpp             Per-simulation temporary storage and limits
    trace.hpp               Optional effect and RNG diagnostic sinks
    systems/
      setup.hpp             Run creation and starting choices
      travel.hpp            Maps, acts, room entry and exit
      combat.hpp            Combat phases, card play, turns, victory/defeat
      generation.hpp        Game-compatible generation and RNG consumption
      rewards.hpp           Offers, claims, skips, replacement prompts
      shop.hpp              Inventory, prices, purchases, removal, restock
      events.hpp            Event eligibility, branches and persistent effects
      services.hpp          Rest, upgrade, transform, remove and treasure
      lifecycle.hpp         Completion, loss, score and current-run history
  observation/
    schema.hpp              Versioned public records and restriction codes
    observe.hpp             CombatObservationView and visibility projection
    actions.hpp             ActionSpaceView and lazy selection iterator
    text.hpp                English rendering and dynamic substitutions
    events.hpp              Optional ordered public gameplay event stream
    debug.hpp               Separate privileged complete-state projection
  persistence/
    checkpoint.hpp          Owning Checkpoint and restore interfaces
    codec.hpp               Explicit binary encoding and validation
    canonical.hpp           Field-wise comparison, digest and debug export
  integration/
    replay.hpp              Recorded actions, provenance, replay runner
    import.hpp              Complete-state import and completeness report
    identity.hpp            Reference object IDs to stable handles
    json.hpp                Public observation/replay/diagnostic encoding
    c_api.h                 Optional exception-safe C interface
src/
  ...                       Implementations matching the header modules
  content/
    cards/                  Content programs and exceptional native handlers
    enemies/                AI programs and exceptional native handlers
    powers/                 Status and trigger semantics
    relics/                 Relic semantics
    potions/                Potion semantics
    events/                 Event programs and text bindings
    characters/             Character-specific mechanics and configuration
    manifest.cpp            Registration and complete content inventory
apps/
  cli/main.cpp              CLI transport and English output
  replay/main.cpp           Offline replay and first-divergence reports
tools/
  rules_compiler/           Offline validation and ruleset generation
  reference_bridge/         Real-game recorder/extractor, outside kernel
  corpus/                   Normalization and failing-replay reduction
data/
  builds/<build-id>/        Versioned definitions, English text, RNG metadata
tests/
  unit/                     Storage, arithmetic and interpreter invariants
  parity/                   Seed/checkpoint/observation/text comparisons
  fixtures/                 Raw and normalized reference recordings
  fuzz/                     Actions, decoding, ownership and determinism
benchmarks/
  kernel.cpp                Resolved transitions and effect throughput
  replay.cpp                Equivalent complete-run end-to-end workloads
  storage.cpp               Clone/restore, footprint and overflow
  observation.cpp           Projection, English rendering and encoding
```

Build targets follow these boundaries:

| Target | Depends on | Responsibility |
| --- | --- | --- |
| `sts2_rules` | core | Load and expose validated immutable content |
| `sts2_engine` | core, rules | Own state and execute deterministic transitions |
| `sts2_observation` | kernel, rules | Project visible state and describe choices |
| `sts2_persistence` | kernel, rules | Save, restore and compare complete state |
| `sts2_integration` | observation, persistence | Import, replay, JSON and external interfaces |
| CLI and optional bindings | integration | Transport and user-facing output |

Content handlers compile into the kernel; generated definitions and programs belong
to the ruleset. Kernel code does not depend on integration, JSON, filesystem access,
or English formatting. The reference bridge is a separate tool/process with no runtime
link from the simulator to game assemblies. Test fixtures are not runtime content.

## 2. Principal types and ownership

| Type | Owns or describes | Lifetime and access |
| --- | --- | --- |
| `Ruleset` | Definitions, programs, dispatch tables, English templates, build/content digests | Loaded once; shared read-only across simulations |
| `CombatState` | Root records and its mutable backing storage | One simulation; no pointers into another state's mutable storage |
| `StateArena` | Reusable contiguous slabs for pools and variable-length state | Owned by `CombatState`; capacity grows through explicit checked operations |
| `Scratch` | Validation workspace, projection buffers, temporary targets and diagnostics | One simulation/worker; never needed to resume gameplay |
| Combat and run functions | Authoritative transition entry points | Receive state/views and scratch explicitly; combat functions accept rules and initialization limits |
| `ExecutionContext` | References to state, rules, scratch and current operation | Exists during execution only; native handlers cannot retain it |
| `CombatObservationView` | Public records/spans produced by projection | Borrowed; invalid after state mutation or reuse of its observation workspace |
| `ActionSpaceView` | Visible choices, constraints and lazy iterators | Borrowed with the same invalidation rules |
| `Checkpoint` | Complete state snapshot and compatibility metadata | Owns independent storage; never borrows scratch |
| `ImportReport` | Imported/missing/unsupported fields and identity mappings | Adapter-owned; exact replay requires complete import |

Use plain structs for records and small classes for ownership, storage and algorithms.
Combat/run functions and the proposed effect, trigger, observation, checkpoint and
replay modules coordinate work; they do not become owners of hidden gameplay state.
No card/entity class hierarchy, per-instance vtable, RTTI, or per-effect closure is
needed. Native handler tables use stable handler IDs and function pointers.

The public facade follows the design's `initialize`, `advance`, `apply`, `observe`,
`actions`, `checkpoint`, `restore` and `clone_into` contract. `StepResult` carries
status and boundary ID; detailed diagnostics live outside hot state records. C adapters
translate errors and catch exceptions before returning across the ABI.

## 3. State layout and storage

`CombatState` has a compact root plus independently sized storage regions:

```text
State
  identity/configuration/schema, boundary counter, phase, validity
  RunState             player, persistent instances, progression and history
  MapState             topology, assignments, visited/revealed information
  RoomState            tag + active room payload, including unresolved transactions
  CombatState          encounter entities, zones, powers, AI and counters
  RngBank              every real-game stream, full internal state, draw counters
  DecisionState        outstanding prompt, constraints and partial selection
  ExecutionState       continuation stack, pending effects and trigger dispatch state
  StateArena           backing slabs for variable-length records
```

`RunState` owns persistent card/relic/potion instances. Combat-only instances live in
encounter pools. Use explicitly distinct handle domains so a persistent card can move
between deck/combat zones without changing identity and a generated card cannot alias
a persistent one. Records refer to definitions through typed integer IDs and to
instances through generation-tagged handles. Destroying an instance advances its slot
generation; generation exhaustion retires the slot or returns a resource error rather
than permitting stale-handle aliasing.

Store object identity separately from zone membership. `OrderedZone` stores handles
in gameplay order; moving a card changes membership, not its handle. Pools preserve
creation sequence metadata when observable order differs from slot order. Never use
swap-removal where it would change gameplay order.

`Pool<T>` and `OffsetVector<T>` use indices/offsets into their owning arena, not stored
process pointers. Typical small collections use measured inline capacities with a
checked overflow path into reusable contiguous storage. The inline and overflow paths
have identical ordering and semantics. Collections, prompts and histories are never
silently truncated. Default gameplay arithmetic is 32-bit with explicit wider
intermediates; choose narrower IDs only after validating the build's supported range.

Keep hot encounter records together and cold map/history data in separate regions.
Use array-of-structs initially. Shared static definitions and English strings never
enter instance records. Store authoritative values and compute cheap derived values;
any cache must be reconstructible without RNG draws or altered behavior. Begin with
deep copying mutable map storage; immutable shared map blocks are a measured later
optimization with explicit protection from cross-simulation mutation.

The typical hot combat footprint target is 16 KiB, not a fixed allocation limit.
Measure it across actual fixtures, including execution/decision storage and persistent
records touched by combat. Report total state and retained arena capacity separately.

## 4. Transitions, effects and choices

`apply_combat` first checks instance validity, expected boundary ID, action shape,
handles, targets and all legality conditions without changing authoritative state or
RNG. `ActionValidator` produces a transient `PreparedAction`; it does not enumerate
every possible selection. Rejected input leaves complete state and RNG unchanged.

After validation, the appropriate system schedules the action's effects and
`EffectMachine` resolves automatic execution until input, completion or failure.
`advance` returns the existing boundary when required input is pending. An accepted
input consumes exactly one real prompt; nested choices create their own boundaries.
Boundary counters are part of complete state, and increment according to a single
documented boundary-publication rule.

`Instruction` contains an opcode and compact operands. `EffectFrame` stores program
ID, instruction position, source, captured values and local slots. `PendingOperation`
stores staged native operations. A native handler that suspends records a handler ID,
resume stage and all required operands in `ExecutionState`; it cannot leave resumable
state on the C++ stack or in scratch. `DecisionState` records prompt ownership and the
resume destination, including cancellation and partial ordered selection state.

`MutationOps` provides shared operations for resources, movement, damage, statuses,
creation and destruction. Systems and content handlers compose these operations in
the order verified against the pinned build. Avoid a universal damage/payment pipeline
that silently forces different mechanics into the same ordering.

`TriggerDispatcher` indexes subscriptions by event type. Subscriptions identify their
owner, handler and ordering metadata. Dispatch frames preserve cursor, active event,
captured sources and the game's rules for addition/removal during dispatch. Nested
resolution uses explicit frames; use a FIFO queue only for mechanics verified to be
FIFO. Checkpoint all pending dispatch information that affects future execution.
Rebuildable subscription indices may be caches only if rebuilding exactly preserves
ordering and active-dispatch behavior.

Every transition has an execution-step budget. Operational failure marks the instance
invalid and emits diagnostics; subsequent gameplay APIs reject it until successful
restore or reinitialization. This avoids mandatory full-state copying per action.
A caller needing rollback can checkpoint first. Decoding and restore validate before
publishing a new valid state. Simulator resource/loop errors are never game losses.

## 5. Rules, RNG and observations

`RulesCompiler` validates references, operand types, handler registration and supported
content, then emits compact tables and programs. `RulesetLoader` verifies build,
content, configuration and schema identities before simulation. Complex mechanics use
native handlers with the same resumable execution and mutation interfaces as programs.
Unknown content fails explicitly; do not fall through to a generic approximation.

`RngBank` contains exactly the pinned game's streams, not a stream per proposed module.
`GameRng` implements initialization, bounded draws and shuffles with explicit call-site
IDs. Generation and AI systems request those operations in verified order. Optional
trace sinks record stream, draw index, call site, raw value and result without consuming
randomness. Observation, text, legality and action iteration never advance RNG.

`Observer` projects state through explicit visibility rules into versioned public
records. It supplies visible ordering, previews, disabled choices and reason codes.
`ActionSpaceView` exposes prompt constraints and lazy combinations rather than storing
all possible subsets. `EnglishRenderer` uses ruleset templates to format requested
descriptions and reasons into caller-owned buffers, outside transitions.

The kernel may emit structured public gameplay events through an optional sink.
Visibility filtering occurs before publication; the full diagnostic trace has a
separate privileged interface. Captured public events must contain values needed to
describe the event at that moment, rather than depend on later mutable state. Sink
failures cannot expose a partially applied state as valid; streaming adapters report
truncation/failure explicitly. Observations do not expose hidden draw order, map
assignments or AI state unless revealed by gameplay.

## 6. Cloning, checkpoints and workers

`clone_into` copies root records and live arena regions into destination-owned storage,
reusing capacity where possible. Offsets and handles remain meaningful inside the new
owner; no gameplay reference points back into source storage. Scratch and derived
caches reset. Clone the full run, RNG, prompt and suspended execution state, not just
combat. Measure bytes copied before considering copy-on-write.

`CheckpointCodec` serializes fields explicitly with fixed endianness, lengths, schema
version, ruleset/content digests and checksums. It never dumps native structs or arena
padding. Restore checks compatibility, lengths, references and invariants and rebuilds
only derived indices. Canonical comparison visits every future-affecting field,
including generation/free-slot metadata that can affect future instance identities.
Diagnostic JSON and reference checkpoint imports are separate adapter formats.

Each worker receives its own state, arenas and scratch. Only immutable rules are
shared. Transitions for one simulation remain sequential. No mutable global caches,
shared allocator locks, I/O or scheduling-dependent RNG enters warmed execution.
Growth may allocate outside warmed capacities; resource exhaustion returns an explicit
error under the failure policy above.

## 7. Verification and implementation order

The non-recording step-1 contracts are in [Matching contract](matching-contract.md),
[Decisions and observations](decision-contract.md), and [Complete-state inventory](state-inventory.md).
They pin the target and inventory requirements. Recorder/replay tooling and scoped
full-state fixtures now exist; complete-run certification remains pending.

1. **Define what the simulator must match.** List the exact game build, supported content
   and configurations. Define what the player can see, which actions they can take, and
   when the game waits for input. Inventory every visible and hidden field needed to
   reproduce future behavior. Build the real-game recorder and independent test fixtures
   for recording and checkpoint import, so implementation has a reference to check against.
2. **Make state reliable to store, copy and restore.** Implement compact storage, stable
   object handles and the game's exact random-number behavior. Add cloning, checkpoints
   and field-by-field state comparison. Verify that a restored or copied simulation
   behaves identically, including when paused at a choice inside another effect.
3. **Make one complete run work.** Implement effect execution, trigger ordering, player
   choices and visible observations. Connect setup through an ending, covering every room
   category with a limited content set. State clearly which content works at this stage.
   This checks that the architecture supports the whole game before expanding content.
4. **Add all supported content and verify its interactions.** Implement every required
   single-player mechanic through effect programs or specialized handlers. Keep raw
   real-game recordings as evidence. Track separately what is implemented, what matches
   reference fixtures, and what has also been tested in combination with other mechanics.
5. **Measure and improve speed and memory use.** Profile replays that already match the
   reference. Tune storage capacities, memory layout and effect dispatch where measurements
   show a benefit. Consider shared map storage, link-time/profile-guided optimization
   (LTO/PGO), or copy-on-write only when they improve measured results. Repeat parity checks
   after optimizations to confirm that game behavior still matches.

Acceptance tests cover seed and checkpoint parity, exact English independently from
structured observations, hidden-information boundaries, stale/invalid inputs, overflow,
trigger order, interrupted effects, clone/restore continuations, fuzzing and sanitizers.
Replay diagnostics report the first divergent field/boundary with prior action and
relevant effect/RNG trace. Existing recording formats may be adapter inputs only after
their completeness and version compatibility have been established.

Benchmark kernel execution separately from observation/text/serialization and complete
end-to-end replay. Pin the game build and `sts2-cli` revision and use equivalent seeds,
actions, observation requirements and worker counts. Report startup, warm throughput,
tail latency, allocation counts, live/retained bytes, clone bytes/time and total peak
memory, including shared rules. Include large decks, high-trigger fights, non-combat
rooms and overflow workloads. Performance targets remain unvalidated until measured.

Open decisions requiring reference evidence or measurements are the exact build and
content manifest, RNG stream inventory, trigger ordering, every suspended-state field,
inline capacities and eventual byte budgets. This file fixes their architectural homes;
it does not invent game semantics or claim complete support.
