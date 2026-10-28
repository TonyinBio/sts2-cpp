# Full-game C++ simulator specification

This design describes a headless C++ simulator for complete single-player runs. It must
match the selected game build, including visible choices, hidden state, and English
text, while using less time and memory than `sts2-cli`.

See the [shared terms](README.md#terms-used-in-these-sheets) for definitions of parity, snapshot, boundary, and model ID.

## On this page

- [1. Goals and matching requirements](#1-goals-and-matching-requirements)
- [2. Supported gameplay](#2-supported-gameplay)
- [3. Architecture](#3-architecture)
- [4. Complete state accuracy to the real game](#4-complete-state-accuracy-to-the-real-game)
- [5. State storage and memory layout](#5-state-storage-and-memory-layout)
- [6. Decisions and observation boundaries](#6-decisions-and-observation-boundaries)
- [7. Transition and effect semantics](#7-transition-and-effect-semantics)
- [8. Randomness and generation parity](#8-randomness-and-generation-parity)
- [9. C++ API contract](#9-c-api-contract)
- [10. State copying and independent simulations](#10-state-copying-and-independent-simulations)
- [11. Save files, replay and real-game integration](#11-save-files-replay-and-real-game-integration)
- [12. Verification and coverage](#12-verification-and-coverage)
- [13. Performance requirements and measurements](#13-performance-requirements-and-measurements)
- [14. Delivery and acceptance](#14-delivery-and-acceptance)

**Status:** design for a new implementation. Existing implementation and requirements are not constraints.

## 1. Goals and matching requirements

Build a headless C++ simulator of the entire single-player Slay the Spire 2 game that
reproduces the real game's observations, available decisions, transitions, and outcomes
while maximizing simulation throughput and minimizing memory per simulation instance.

Match the real game first. Optimize speed and memory use without changing its behavior.

Use `sts2-cli` as the performance baseline. The simulator must execute equivalent
gameplay faster and use memory more efficiently than `sts2-cli`.

### What must match

“1-to-1 observations” means matching the gameplay information the real game exposes at
the same point in play: numeric values, ordering, available options, disabled-action
reasons, revealed information, English descriptions and intermediate prompts. Expose
structured data alongside English text for LLM agents and CLI use. The simulator is
headless: do not implement graphical rendering or store screen layouts, graphics,
animation state or graphical display buffers.

Declare the exact game build, content set, configuration, difficulty and mod set the
simulator supports. Matching that version does not establish correctness for other
versions. Return explicit errors for unsupported content or versions.

Verify both forms of matching behavior:

* **Seed parity:** the same initial configuration, seed, and actions produce matching observations and outcomes throughout a run.
* **Checkpoint parity:** an imported complete game state produces matching future observations and outcomes. A visible observation alone generally cannot reconstruct hidden state or random-generator positions.

### Language

English is the only supported language. Match gameplay-visible English names,
descriptions, prompts and formatting in the targeted build. Store shared English text
definitions in the ruleset and format dynamic text when observations or CLI output
request it. Do not duplicate static text in each simulation state. Translation catalogs,
locale selection and non-English text parity are out of scope.

Support both seed parity and checkpoint parity for complete runs. Combat-only simulation
does not satisfy this specification.

## 2. Supported gameplay

Model all single-player gameplay reachable in the pinned build, including all supported
characters and difficulty modifiers. Assume all single-player content and difficulty
levels are unlocked. Do not store a persistent player profile, track unlocks or update
progression across runs. Compare real-game behavior using the same fully unlocked
configuration. Multiplayer and multiplayer-exclusive content are out of scope and must
not be implemented. List all single-player content and mechanics in the targeted game
version. Verify their behavior against that version rather than assuming they work as
they did in Slay the Spire 1.

| Area | Required behavior |
| --- | --- |
| Setup | Character selection, run configuration, seeds, difficulty, initial modifiers and starting choices |
| Map and acts | Exact map generation, node visibility, route legality, room assignment, act changes, boss selection, travel effects |
| Combat | Player and enemy turns, cards, powers, relics, potions, summons and other entities, targeting, generated objects, enemy AI, interrupts, defeat and victory |
| Rewards | Reward generation and ordering, card offers, currencies, relics, potions, replacement prompts, skips and delayed rewards |
| Shops | Inventory generation, prices, discounts, purchases, removal, restocking and purchase-triggered effects |
| Events | Eligibility, weighted selection, text variants affecting decisions, option conditions, costs, randomness, follow-up choices and persistent consequences |
| Rest and services | All offered actions, upgrades, transformations, removal, previews, eligibility and cancel behavior |
| Other rooms | Treasure, transitions, special encounters and build-specific room types |
| Run lifecycle | Run save/load, completion, losses, score and history within the current run |

Exclude audio, animation, platform achievements and cosmetic menus from the simulation
kernel. Implement any effects they have on single-player gameplay. Do not implement
networking or multiplayer.

## 3. Architecture

Use C++23, keep external dependencies minimal and separate the implementation into four
layers:

1. **Ruleset:** immutable definitions, compiled effect programs, dispatch tables, English text definitions, generation tables and version metadata.
2. **Simulation kernel:** mutable complete game state, deterministic transition engine, randomness, suspended effects and legality checks.
3. **Observations:** expose exactly the information visible to the player through a versioned schema.
4. **JSON I/O:** translate setup/actions, record and replay sessions, and export comparison state with diagnostic histories; restorable checkpoints remain in persistence.

The ruleset is shared read-only across workers. A simulation owns its state and mutable
scratch space. No shared global values that can change, cross-worker allocator locks, filesystem reads
or network calls occur during transitions.

Target a specific game version and track all its single-player content. Verify exact
mechanics against that version of the game.

## 4. Complete state accuracy to the real game

The complete game state includes both player-visible state and hidden state.
Player-visible state includes health, cards in hand, revealed map nodes and current
choices. Hidden state includes draw-pile order, unrevealed room assignments, internal
enemy AI data and RNG state. Together they contain all data needed to determine future
gameplay and observations. An observation is the information shown to the player. It exposes only the information the real game
currently reveals to the player.

Preserve all visible and hidden state needed to reproduce the real game's future
behavior and observations. Cloning and checkpoint restoration must preserve that
behavior.

### Required state

The state must include:

* Ruleset identity, schema version and run configuration.
* Act, map, current room, traversal history, room-local state and generation history used by future rules.
* The player's health, resources, deck, relics, consumables, counters, modifiers and persistent run effects.
* Combat entities, stable identities, ordered zones, instance modifications, AI history, intents, statuses and phase counters.
* Rewards, inventories, event variables, unresolved transactions and all outstanding selections.
* Every random stream's complete state and relevant draw counters.
* Suspended execution frames, pending effects, trigger ordering, active prompts and visibility state.

## 5. State storage and memory layout

Separate immutable content, run state, encounter state and temporary execution state.
Compute inexpensive derived values when needed.

Use typed integer IDs for definitions and stable generation-tagged handles for mutable
instances. A card instance is distinct from its definition and from its location.
References survive zone moves and fail explicitly after object destruction. Preserve
creation order wherever the game can observe it.

Prefer contiguous arrays, compact records and indexed pools. Use array-of-structs for
small records consumed together; split fields only when profiling demonstrates a
benefit. Avoid per-object allocation, strings, RTTI, virtual entity hierarchies and
general-purpose maps in hot paths.

Use 32-bit arithmetic by default for counters and values, with explicit wider
intermediates where needed. Use narrower IDs only after validating that all supported
values fit. Match observed rounding, clamping and operation order. Never depend on
signed overflow, native padding or implementation-defined serialization.

Store typical collections inline and provide additional storage when they outgrow that
capacity. Never truncate decks, effects, options, entities or histories. Return a
resource-limit error if storage is exhausted; do not treat it as a game outcome.

## 6. Decisions and observation boundaries

Use a state machine that produces the same results for the same state and input. Execute automatic effects until the game requests
input, the run ends or the simulator encounters an error. Represent each input boundary
with a phase enum and structured choice data. Provide English descriptions and prompts
through observations without deriving or storing a graphical screen.

Inputs cover setup selections, map travel, card plays, potion use, turn ending, targets,
ordered and unordered multi-selections, reward choices, purchases, service use, event
options, confirmation, cancellation and continuation. Do not compress a sequence of real
prompts into one simulator action.

### What the player can see

An observation contains:

* Game build, observation-schema version and a unique ID for the current observation boundary.
* Current phase, public run/map/room state and player-visible entity data.
* Exact visible ordering of cards, enemies, rewards, inventory and choices.
* Gameplay numeric values and structured preview results when available to the player in the real game.
* Prompt constraints: allowed counts, ordering, eligibility, confirmation and cancellation.
* Enabled actions and player-visible disabled options, with restriction codes and matching English reasons.
* Structured results and English feedback from actions and events, including player-visible intermediate outcomes.

Expose draw order, room assignments and enemy state only when the real game reveals
them. Provide hidden state through a separate debug API.

Define when the simulator stops for player input. Provide an optional ordered stream of
gameplay effects between decisions. Preserve gameplay information, English text and
effect order without presentation delays or graphical display buffers. Compare
structured observations and English text against the real game in separate test modes.
Do not compare rendered pixels.

For choices with many combinations, expose selection constraints and generate actions
through an iterator. Do not allocate every possible subset in advance. Validate
submitted actions directly without enumerating the action space.

## 7. Transition and effect semantics

Use an interpreter that can pause and resume, with compact records identifying each pending operation. Compiled effect
programs handle common primitives; specialized native handlers cover complex rules.
Avoid heap-allocated closures and coroutine allocations per effect.

Specify and verify the exact order of validation, payment, movement, targeting,
replacement effects, modifiers, damage, death checks, triggers, cleanup and prompts for
each applicable mechanic. Use a FIFO queue only where recorded real-game tests confirm
that resolution order. Support nested resolution, interruption, source snapshots and
suspended selections.

Maintain subscriptions indexed by event type so unrelated powers and relics are not
scanned for every event. Dispatch in the real game's order. Match how it handles ties,
newly registered effects, removal during dispatch and triggers raised by other triggers.
Preserve this order when optimizing.

Implement simultaneous operations in the order and grouping used by the real game.
Specify when each effect captures its targets, amounts and source properties, and when
it reads them again.

### Invalid input and failed transitions

Invalid input leaves complete game state and RNG unchanged. External input validation
occurs before mutation. Internally generated actions follow the same rules. Skip
redundant checks only when their preconditions are guaranteed and documented.
Operational failure restores a checkpoint or invalidates the simulation instance; it
must never expose a partially applied state as valid.

Limit the execution steps allowed in one transition to detect infinite loops. If the
limit is reached, return a simulator error and diagnostic trace. Implement any real-game
loop-breaking rules separately.

## 8. Randomness and generation parity

Reproduce the pinned game's RNG algorithms, initialization, stream partitioning, bounded
sampling, shuffle implementation, when draws occur and which conditions consume draws.
Matching a PRNG family or initial seed alone is insufficient.

List every source of randomness, including setup, maps, encounters, rewards, shops,
events, card generation and AI. Do not introduce convenient independent streams unless
they match the game. Do not consume randomness for formatting, logging, worker
scheduling or action enumeration.

Replay diagnostics record the RNG stream, draw number, call site, raw random value and
sampled result. Production builds may disable this instrumentation without changing
behavior.

Exact replay uses the real game's RNG state. Matching outcome distributions alone does
not establish deterministic replay correctness.

## 9. C++ API contract

The following is an interface sketch, not a required ABI:

```cpp
struct Ruleset;
struct State;
struct Scratch;
struct Action;
struct ObservationView;
struct ActionSpaceView;
struct Checkpoint;

enum class StepStatus {
    awaiting_input, run_complete, invalid_action,
    unsupported_content, resource_limit, engine_error
};

struct StepResult {
    StepStatus status;
    std::uint64_t boundary_id;
};

StepResult initialize(State&, const Ruleset&, const RunConfig&, Scratch&);
StepResult advance(State&, const Ruleset&, Scratch&);
StepResult apply(State&, const Ruleset&, const Action&, Scratch&);
ObservationView observe(const State&, const Ruleset&, Scratch&);
ActionSpaceView actions(const State&, const Ruleset&, Scratch&);
Checkpoint checkpoint(const State&, CheckpointArena&);
void restore(State&, const Checkpoint&, Scratch&);
void clone_into(State& destination, const State& source, StateArena&);
```

`initialize`, `advance` and `apply` resolve automatic execution until an observation
boundary or terminal result. `apply` consumes one input at the current boundary.
`advance` cannot skip pending required input. Actions carry the expected boundary ID so
stale actions are rejected.

Views reference existing storage. Document which state changes or scratch-space reuse
invalidate them. Checkpoints own or retain all necessary storage and do not borrow
mutable scratch. Cloning preserves all data that can affect future behavior and updates
internal references to the cloned storage. Restoring clears derived caches and scratch
as needed without changing complete game state.

Public adapters may allocate memory and convert data to file or wire formats. Core transitions and common compact
observation views should require no general-purpose heap allocation after capacity
warmup. Exceptions must not cross a C ABI; return structured error codes and store
detailed diagnostics outside frequently accessed state records.

## 10. State copying and independent simulations

First implement cloning by copying contiguous state storage. Measure bytes copied and
clone time before adding copy-on-write. Share immutable definitions rather than copying
them.

Store large map data separately from frequently accessed state when it remains
unchanged. Shared storage must prevent one simulation from changing another. A cloned
state must not reference another simulation's mutable memory.

Compare and serialize state field by field. Include all data that can affect future
behavior, including history, RNG and pending execution. Do not compare raw struct bytes
because padding and memory addresses may differ.

Run independent simulations on separate workers. Give each worker its own state storage
and scratch space; share immutable rules. Execute each simulation's transitions
sequentially to preserve effect order. Worker scheduling must not change simulation
results.

## 11. Save files, replay and real-game integration

Use a compact binary checkpoint format with explicit endianness, lengths, checksums,
schema version, ruleset digest and content digest. Supply a human-readable diagnostic
export outside the hot path. Reject incompatible saves unless an explicit tested
migration exists.

A real-game bridge records initial configuration, complete checkpoints where available,
submitted actions, public observations, gameplay event boundaries and optional RNG
diagnostics. The checkpoint importer must report whether it imported every field needed
for exact replay. Missing hidden state prevents claiming exact future replay; it must
not be filled with guesses under an exact-parity label.

Maintain a mapping from real-game object IDs to simulator handles. Keep each object
linked to the same handle across observations. Stable simulator handles may be
normalized for comparison only if identity relationships and ordering are preserved.

Checkpoints contain only the current simulation state. Do not import, save or update
player profiles or cross-run progression.

## 12. Verification and coverage

Use recordings from the targeted game build as the reference for correctness. Verify
behavior against the game rather than relying on documentation or assumptions from Slay
the Spire 1. Store recordings, test fixtures and validation reports as development
artifacts, separate from the runtime ruleset; they demonstrate that implemented behavior
matches the real game.

Replay the same actions in the real game and simulator. Compare every player-visible
observation, legal action, visible event sequence and final outcome. On divergence,
report the first mismatching boundary and field, prior action, ruleset identity and
relevant RNG/effect trace. Reduce failing replays to the shortest sequence that
reproduces the mismatch and save it as a test fixture.

Track test coverage by content item, mechanic, phase, trigger interaction, difficulty
and character. Distinguish implemented, fixture-verified, interaction-verified and
unsupported content. An untested handler is not evidence of full-game parity.

### Required tests

Required tests include:

* Seeded complete runs through every room type and ending, with all single-player content and difficulty levels unlocked.
* Directed fixtures for nested prompts, cancellation, replacement, overflow, generated content and order-sensitive interactions.
* Generation comparisons for maps, inventories, rewards and events across a reproducible seed corpus.
* Single-player hidden-information boundaries and state restoration across all phases.
* Save/load and clone/restore producing identical subsequent observations and RNG draws.
* Invariants for instance ownership, zone membership, handles and internally consistent prompt constraints; game-specific numeric limits require evidence.
* Fuzzed legal action sequences, malformed boundary inputs, sanitizer runs and canonical-state determinism across supported compilers and platforms.

Test observation extraction and checkpoint import with independent fixtures so
extraction bugs do not hide simulator errors. Keep raw real-game recordings alongside
normalized records. Publish which content and interactions the passing replay tests
cover, and list gaps. Passing those tests does not prove that untested interactions
match.

## 13. Performance requirements and measurements

Do not implement real-time presentation waits or graphical rendering. Keep English text
formatting in observation and CLI adapters, outside simulation transitions. Do not
perform network I/O during simulation transitions. Precompile content and generation
tables. Use release optimization and evaluate LTO/PGO against representative workloads.
Do not use arithmetic transformations that change game semantics.

“Faster than the real game” means faster than `sts2-cli`. Pin the `sts2-cli` revision
and game build used for each comparison. Run both implementations on the same hardware
with the same configuration, seeds, action sequences and observation requirements,
including English text when requested. Compare equivalent combat, non-combat and
complete-run workloads.

Measure end-to-end wall-clock time, throughput, CPU time and peak memory for both
implementations. Include action submission, observation generation and serialization in
end-to-end comparisons. Report startup and warm execution separately, and use the same
worker count. Also report kernel-only execution separately where measurable; do not
compare simulator kernel time against `sts2-cli` end-to-end time.

Benchmark fixed recorded action sequences and complete-run replays. Report CPU,
compiler, flags, ruleset, corpus digest, sample count, median and tail latency,
allocation counts, bytes copied, peak memory and scaling efficiency.

| Benchmark | Primary metrics |
| --- | --- |
| Combat decisions and nested selections | Decisions/s, effects/s, p50/p95 latency |
| Map/event/shop/reward transitions | Transitions/s and latency by phase |
| Complete runs | Replayed runs/s and CPU time/run, with action-loading cost separate |
| State copying | Clone/restore ns, bytes copied, memory per simulation instance |
| Observations | Projection ns, serialized bytes, allocation count |
| Parallel workload | Throughput and memory at 1/2/4/8/... workers |

### Initial targets

Initial performance targets; revise them after measuring the first correct
implementation:

* Zero general-purpose heap allocations for warmed common transitions within declared capacities.
* Target at most 16 KiB of hot mutable combat state for a typical single-player fixture, excluding immutable rules and cold full-run data. Report state sizes across the benchmark fixtures. Preserve correct behavior even when it requires more memory.
* Target at least 100,000 simple resolved decisions/s/core and 1,000 complete-run replays/s/core on a named modern desktop CPU. These are unvalidated stretch targets, not promised throughput; publish action counts, content mix and action-loading cost.
* Once stable measurements exist, flag a statistically repeatable throughput regression greater than 5% on designated reference hardware. Correctness failures always block a release regardless of speed.

Do not optimize only trivial combats or omit observations from an advertised end-to-end
benchmark. Publish kernel-only numbers separately. Include high-trigger combats, large
decks, unusual events and overflow paths.

## 14. Delivery and acceptance

1. **Reference recordings and interfaces:** select a game build, list its single-player content, define observation schemas and implement the real-game recorder. Verify representative checkpoints and RNG behavior.
2. **First complete run:** implement setup through an ending with combat, travel, events, shops, rewards, rest, save/load and replay. List the content supported at this stage.
3. **Complete supported content:** implement every listed single-player rule and in-run path with all content unlocked. Test them against real-game fixtures and complete-run replays.
4. **Optimization:** profile the verified implementation; improve representation, dispatch, cloning and batching while rerunning parity tests.
5. **Release:** publish supported build/configurations, content coverage, replay corpus results, known limitations and reproducible performance measurements.

Release only when there are no known observation or transition mismatches for supported
content. Verify that unsupported inputs return errors, checkpoints restore all state,
seeded replays are reproducible and observations reveal only player-visible information.
Publish performance comparisons against `sts2-cli` for combat, non-combat and complete
runs, demonstrating lower execution time and memory use on the declared benchmark
workloads.

Start by defining the observation schema and implementing real-game recording. Use those
recordings to verify behavior and compare simulation speed.
