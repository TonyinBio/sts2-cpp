# Full-game data-oriented engine architecture

Published specification: [GitHub issue #56](https://github.com/TonyinBio/sts2-cpp/issues/56),
labelled `ready-for-agent`.

Source: [architecture conversation](https://chatgpt.com/share/6ae309c7-03e8-83e8-9f6f-ec79dbee927f).
Decision record: [ADR 0006](../adr/0006-use-a-data-oriented-engine-with-selective-continuations.md).

This specifies the intended architecture and migration, not current full-game
support. The conversation's final agreed decisions take precedence over earlier
proposals in that conversation and conflicting repository design decisions. The
user explicitly confirmed that precedence during synthesis. This specification
does not establish reference verification for the conversation's gameplay examples.

## Problem Statement

The simulator needs to grow from its supported mechanics into the complete
single-player game without repeatedly redesigning ownership, execution and source
organization. MCTS needs cheap independent branches that include permanent combat
consequences, while gameplay needs nested choices, ordered reactions, many creature
types and room interactions. Duplicated run/combat attributes, growing central
implementations and general runtime frameworks would make that expansion harder
to verify and more expensive to simulate.

## Solution

Use a compact, deterministic, data-oriented simulation kernel with a small public
decision interface. Complete run state owns canonical mutable gameplay data;
immutable definitions are shared. Procedural mechanics, typed hooks and compiled
content implement gameplay. Lightweight instruction programs sequence simple
effects, with a cloneable stack recording only unfinished resumable work. Combat
and specialized rooms publish choices through the same public interface. MCTS
remains a separate consumer.

The architecture must accommodate the full single-player game, while migration
preserves the explicitly supported subset and adds mechanics incrementally.

## User Stories

1. As a simulator maintainer, I want an architecture covering the full single-player game, so that incremental content does not force repeated structural redesigns.
2. As a search developer, I want independent complete-run clones, so that branch evaluation includes permanent gameplay consequences.
3. As a search developer, I want common-state memcpy cloning without heap allocation, so that branching remains inexpensive.
4. As a player, I want unusually large legal decks and collections to work, so that inline capacities do not impose artificial gameplay limits.
5. As a search developer, I want exceptional overflow copied independently, so that one branch cannot mutate another.
6. As a mechanics author, I want one canonical creature representation, so that damage, block, powers and targeting use shared rules.
7. As a mechanics author, I want persistent player HP and maximum HP in one place, so that permanent combat changes survive naturally.
8. As a mechanics author, I want specialized entities and player resources represented separately where needed, so that ordinary creatures do not carry unrelated state.
9. As a mechanics author, I want stable generational identities, so that slot reuse cannot silently redirect captured references.
10. As a mechanics author, I want deferred creature reclamation, so that death reactions and suspended operations can still resolve their operands.
11. As a player, I want temporary card changes isolated from the permanent deck, so that combat cleanup preserves the intended run state.
12. As a player, I want permanent card changes explicitly applied to the original, so that growth effects persist correctly across encounters.
13. As a mechanics author, I want card zones to move instance handles, so that movement preserves identity without duplicating card state.
14. As a player, I want layered cost changes to expire in the correct order, so that later costs remain correct.
15. As a content author, I want compact common card fields and typed exceptional state, so that unusual cards do not require a universal modifier framework.
16. As a content author, I want reusable compiled mechanics, so that cards, relics, potions, powers and monsters share rule implementations.
17. As a content author, I want typed hook entry points, so that reactions occur at explicit gameplay stages in native order.
18. As a player, I want choices nested inside draws or hooks to pause correctly, so that unfinished effects resume exactly once in the right order.
19. As a search developer, I want to clone or checkpoint at a nested prompt, so that every branch resumes from the same complete execution state.
20. As a performance engineer, I want guaranteed synchronous mechanics to execute directly, so that they do not pay for unnecessary continuation frames.
21. As a mechanics author, I want explicit combat phases and compiled enemy AI, so that turn progression, spawning and move history remain deterministic.
22. As a player, I want visible intents separated from hidden selected moves, so that observations reveal only permitted information.
23. As a player, I want events, shops, rest sites, treasure and ancient choices through a consistent decision interface, so that every room remains playable headlessly.
24. As a player, I want an event to resume after its nested combat and rewards, so that the enclosing interaction is not lost.
25. As a player, I want independent and linked rewards, skips and rerolls, so that reward handling follows each offer's rules.
26. As a search developer, I want allocation-free action traversal and direct validation, so that expansion does not require action vectors or speculative clones.
27. As a search developer, I want inspection and validation to preserve RNG and state, so that querying a branch cannot change its future.
28. As a search developer, I want worker-local mutable state and scratch with shared immutable rules, so that independent simulations can run concurrently.
29. As a maintainer, I want source responsibilities separated inside one optimized engine library, so that modularity does not impose runtime object machinery.
30. As a performance engineer, I want storage and dispatch choices measured on representative workloads, so that tuning reflects execution and copying costs together.
31. As a parity author, I want public boundary traces compared with pinned oracle evidence, so that structural changes preserve observable gameplay.
32. As a maintainer, I want explicit unsupported and operational failure results, so that missing mechanics or resource failures cannot masquerade as game outcomes.

## Implementation Decisions

### Authority and scope

- Design for complete single-player gameplay first. Avoid inexpensive-to-prevent
  multiplayer constraints, such as hardcoding the player into generic damage
  mechanics, but do not build multiplayer coordination now.
- The final selective-continuation decision replaces the conversation's earlier
  generic scheduling proposals. The later procedural dispatcher replaces its
  earlier object-oriented reading of a run controller.
- Dense versus sparse powers remain deferred. This explicitly supersedes ADR
  0005's fixed type-slot storage choice; neither the current implementation nor
  older documents settle that choice. Preserve identity, stacking, per-applier
  distinctions and native reaction order whichever representation is selected.
- Preserve complete-state versus observation separation, common-case memcpy with
  correct overflow, and JSON restorable checkpoints from the existing compatible
  ADRs. Full-game support and exact performance are acceptance targets, not facts
  established by this document.

### Module interfaces and dependencies

| Module | Responsibility and interface contract |
| --- | --- |
| Core storage and identity | Compact sequences, pools, generational handles, overflow ownership and clone status; no gameplay rules |
| State | Authoritative run, creature, player, card, combat, room, prompt and continuation records |
| Rules | Immutable model IDs, definitions, instruction programs and shared dispatch metadata |
| Simulation interface | Initialize, enumerate actions, validate, apply and advance to input, inspect terminal status, clone; compose existing contracts |
| Mechanics | Procedural damage, block, healing, drawing, movement, powers and resource changes; own their rule sequencing |
| Hooks | Typed modifier and reaction entry points with explicit ordering and resumable traversal when required |
| Execution | Narrow instruction sequencing, continuation push/resume and prompt result routing |
| Combat | Encounter setup, turn phases, enemy order, move execution and combat ending |
| Run and rooms | Map and act progression plus specialized shop, rest, event, treasure and ancient handlers |
| Rewards | Generate offers once, then expose claims, skips, alternatives, linked groups and rerolls |
| Content | Compiled card, power, relic, potion, monster and event behavior using shared mechanics |
| Observation | Player-visible structured information and presentation, without mutating state |
| Persistence and integration | Complete JSON checkpoint encode/restore and separate diagnostic/oracle adapters |

- Compile gameplay source modules together in one optimized engine library. Do not
  require a separate compiled library per subsystem or a strict one-way gameplay
  call graph: mechanics invoke hooks, content handlers invoke mechanics, and
  nested reactions may re-enter them through explicit internal interfaces.
- Keep public headers limited to consumer needs. Internal gameplay interfaces
  stay internal. Source grouping does not mandate one file per card or mechanic.
- Core transitions remain independent of JSON, filesystems, the oracle and search
  algorithms. Integration depends on the kernel; kernel execution does not depend
  on integration.
- State records use flat data, compact contiguous storage, packed flags where
  appropriate and no runtime-polymorphic gameplay objects. Choose AoS or SoA from
  access patterns; DOD does not prescribe SoA for small creature populations.

### Ownership, identity and cloning

- Run state owns the canonical creature pool. Player state references its creature
  and owns or references persistent deck, inventory and player-specific resources.
  Combat owns phases, enemy ordering and AI state, card instances and zones,
  counters, pending choices and unfinished combat execution, not duplicate HP.
- Keep persistent and temporary creature attributes together. Reset temporary
  block, powers and counters at the game-defined lifecycle points; the player
  creature persists across encounters. Specialized state uses composition rather
  than inheritance. Player-specific resources such as energy, stars and orbs do
  not inflate every enemy record. Character-associated mechanics may coexist on
  one player; do not make them mutually exclusive character-state alternatives.
  Entities outside normal targeting/damage/power semantics,
  such as the conversation's Osty example, use specialized state and mechanics.
- Use stable generational handles with recyclable slots. Separate ordered party
  membership from pool identity. A dead or departing creature can remain stored
  while outstanding reactions or continuations reference it; reclaim only when
  those references and native lifecycle rules permit. Reused slots reject old
  generations; preserve existing identity-exhaustion safety.
- All authoritative mutable state belongs to the branch, including permanent
  effects, RNG positions, boundaries, clocks where required, captured hook
  candidates and pending operands. Share immutable definitions only. Lightweight
  execution contexts are non-owning transient views and are never stored in
  cloneable records.
- Initially clone the full run. Keep logical ownership boundaries suitable for
  future measured copying reductions without changing gameplay semantics. Do not
  implement partial snapshots, copy-on-write or undo logging in this migration.
- Continue the existing explicit clone operation: byte-copy wholly inline state;
  independently duplicate overflow otherwise. No self-pointers in byte-copyable
  records, no arena dependency and no shared mutable overflow. Overflow remains
  overflow until explicit reset. Preserve transactional clone failure behavior.
- Avoid sizing every instance for extreme states. Do not truncate legal gameplay
  at inline capacities. Completed encounter graphs and diagnostic histories do
  not become mandatory clone payloads merely because the entire run is cloned;
  retain the gameplay information required by audited mechanics under ADR 0003.

### Cards and content

- Separate immutable card definitions, persistent deck cards and combat instances
  with distinct handle types. Combat instances carry an optional persistent origin;
  generated cards ordinarily have none. Duplication and transformation follow the
  specific mechanic's origin semantics.
- Move combat handles among zones. Temporary upgrades, costs and transformations
  affect combat instances; permanent effects explicitly update the corresponding
  persistent card and combat state as required. Never synchronize every combat
  mutation to the deck automatically.
- Use fixed common mutable card fields, dedicated ordered cost-modifier sequences
  with expiration semantics, and typed card-specific state. Derive values from
  definitions and gameplay counters when no independent per-card state is needed.
  Do not replace ordered absolute/relative cost records with one override value.
- Inline versus sparse card-specific payloads and exact payload size remain open.
  The conversation's suggested sixteen bytes is not a verified maximum. Enchantment
  and affliction fields describe the chosen compact model; catalogue counts and
  pinned-build slot constraints need evidence before becoming hard limits.
- Model IDs dispatch compiled content behavior. Use direct functions and enum
  switches by default where practical. An immutable shared function table remains
  a benchmarkable option; never add virtual gameplay instances or per-instance
  function objects. Common instruction programs and unusual compiled handlers use
  the same mechanics and suspension contract.

### Hooks, execution and suspension

- Typed hook entry points express specific game stages. Modifier hooks that only
  compute values execute synchronously. Reaction hooks invoke compiled behavior in
  native order and use resumable traversal whenever a listener may require input.
  No generic publish/subscribe event bus or scheduler represents every primitive.
- The continuation stack records unfinished work rather than queued independent
  effects. Simple instruction programs retain a program ID, position and required
  operands; resumable operations retain stages, locals and progress. Instructions
  remain immutable shared rules data.
- Create frames for resumable sequencing, not for every arithmetic or synchronous
  mechanic. A guaranteed synchronous function must never suspend. Any caller that
  can reach a suspending hook must have an explicit resumable path; it cannot yield
  and expect its ordinary C++ stack or worker scratch to survive.
- At a prompt, all required parents, operands, traversal progress and result
  destinations live in complete state. One active player prompt is exposed at a
  time. Accepted input completes that prompt step and resumes the innermost work,
  then its parents, exactly once in native order. Preserve selection, cancellation,
  skipping and confirmation behavior from the existing decision contract.
- Hook traversal preserves native ordering and native rules for additions,
  removals and captured identities during dispatch, including across suspension.
  Storage order or enum order must not silently determine reaction semantics.
- Advance automatic work until input, completion or explicit operational failure.
  Repeated advance while input is pending is idempotent. Validation rejects stale
  boundaries and handles without mutation. Preserve execution budgets and explicit
  invalidation after operational failure; a resource failure is not player death.
- The conversation's nested-draw scenario is an acceptance shape: a card starts
  drawing, shuffle invokes a choice-producing hook, a choice resolves, hook
  traversal finishes, remaining draws finish, and the parent card continues.
  Establish exact stages and RNG behavior against the pinned oracle before using
  a named card or power as a reference fixture.

### Combat, rooms and rewards

- Combat uses explicit initialization, turn-start, player-input, turn-end,
  enemy-turn and end-resolution stages, refined by pinned native ordering. Enemy
  AI is compiled per monster with shared helpers for weighted choices, history and
  repetition restrictions. Preserve selected moves and required AI state; derive
  the permitted visible intent separately.
- Enemy order controls turn order and positional targeting independently of stable
  creature identity. Spawn, death, escape and transformation follow native stages
  and the deferred-reclamation contract.
- Run progression is a small procedural dispatcher, not a polymorphic controller.
  Specialized rooms own compact state and compiled behavior through the shared
  action interface. Room handlers report transitions through defined operations
  rather than independently changing unrelated map or run state.
- Use tagged alternatives for mutually exclusive active room state. Do not assume
  all room and combat state are mutually exclusive: an event's suspended context
  may coexist with its nested combat and rewards. Store explicit return context;
  do not create a second general scheduler or reserve all room payloads at once.
- Rewards are reusable beyond combat. Generate offers at the native generation
  point once, preserve them in state and mutate them through claims, skips or
  permitted rerolls. Linked groups and independent offers retain their distinct
  behavior. Inspection never regenerates stock, rewards, moves or room outcomes.

### Public API, RNG and persistence

- Reuse the existing initialize, apply-action, advance-to-decision, validation,
  observation, clone and checkpoint contracts. Public names need not change just
  to match sketches in the conversation. The stepping interface resolves forced
  work and returns a real decision, terminal result or operational failure.
- Provide a non-owning action view, iterator, indexed traversal or caller storage
  for allocation-free enumeration across the run, extending the existing combat
  action-view precedent. Do not force every caller to allocate an action vector.
  Validate directly rather than cloning and executing speculative actions. Preserve
  offered restricted actions and reasons required by the decision contract.
- Read-only action and observation views are valid for the state/boundary from
  which they were obtained; reacquire after mutation. Enumeration, validation,
  observations and text rendering neither consume RNG nor mutate complete state.
- Give each RNG stream one authoritative state location. Consume randomness only
  at native mutation points and preserve stream identity and positions in clones
  and checkpoints. Observation filtering must prevent hidden state from changing
  visible choices or leaking through identifiers and support diagnostics.
- MCTS owns policies, tree nodes, evaluation and any sampling of hidden state.
  Privileged complete state is distinct from player observations. Do not add search
  nodes, rollout rules, heuristics or determinization policy to the engine.
- Each worker owns mutable simulation state and scratch. Shared definitions and
  dispatch metadata are immutable. No global mutable gameplay caches or RNG.
- JSON checkpoints preserve complete state including continuations, origins,
  generations and RNG, with independent restoration and transactional validation.
  Diagnostic snapshots remain a separate format. Version incompatible checkpoint
  changes explicitly; no binary checkpoint compatibility loader is required.

### Migration and completion

1. Reconcile architecture documentation against this specification. Mark conflicting
   earlier proposals as historical, especially duplicate run/combat attributes,
   generalized trigger scheduling and fixed power storage. Preserve evidence and
   current implementation-status distinctions.
2. Introduce canonical run-owned creatures and typed identity links; remove HP,
   power and RNG synchronization only when their single authoritative owner is
   established. Preserve permanent mutations and encounter reset behavior.
3. Extract shared mechanics, typed hook interfaces and compiled content from the
   current central implementations without changing supported gameplay. Keep one
   engine library and reuse existing storage/identity infrastructure.
4. Generalize existing instruction suspension to selective nested continuations
   and resumable hook traversal. Prove nested prompt clone/restore behavior before
   migrating further suspending mechanics.
5. Separate combat orchestration and enemy AI, then procedural run progression,
   specialized rooms and reusable rewards. Preserve enclosing event context.
6. Extend allocation-free actions to run decisions and migrate observation,
   checkpoint and diagnostic adapters to the new ownership model.
7. Run supported parity and branching acceptance throughout. Add new game content
   under the existing content work rather than claim this restructuring implements
   the full game. Document remaining support and layout decisions explicitly.

Completion requires the ownership, execution, public-query and independent-branch
invariants above to hold for the supported subset, with migration evidence. Future
content must use these interfaces. A new card or power using existing mechanics
should not require changes to the central combat controller or unrelated mechanics;
genuinely new mechanics may extend the relevant typed interfaces. No unmeasured
throughput promise, fixed payload
size or dense/sparse winner is an acceptance requirement.

## Testing Decisions

The user confirmed the public run interface as the acceptance seam, including
clone/restore at nested prompts and targeted allocation checks.

- Use the existing public run decision interface as the primary acceptance seam:
  initialize a supported run, observe and enumerate, submit actions, advance, and
  compare resulting boundaries and future behavior. Clone and JSON checkpoint
  restoration are alternate starting points through that same gameplay interface.
  Avoid adding per-module mock seams or testing the shape of private frames.
- Test behavior through the simulation interface across mechanics, hooks, execution,
  combat, run/rooms, rewards and content. Good tests assert native ordering,
  permitted observations, persistent consequences and independent futures; they do
  not freeze dispatcher classes, exact frame counts, enum values or file layout.
- At an ordinary prompt and a nested hook prompt, fork the original, a clone and a
  restored checkpoint. The same accepted action suffix must produce equivalent
  gameplay and RNG positions; different suffixes must evolve independently, even
  when state or continuations overflow inline capacity. Unchanged source branches
  must remain unchanged.
- Cover ordered costs and expiration, temporary versus permanent card mutations,
  deck reordering with origin links, creature slot reuse after deferred cleanup,
  additions/removals during suspended hook traversal, and event-to-combat-to-reward
  return. Select pinned-build supported fixtures as each mechanic is implemented.
- Preserve rejection purity, stale-boundary handling, pending-advance idempotence,
  query purity, hidden-information filtering, operational failure invalidation and
  transactional restore. Exact complete-state comparisons belong to privileged
  tests, while public observations remain filtered.
- Reuse the public-run, complete-state clone, run-checkpoint and engine suspended
  choice tests as prior art. Existing Survivor choice clone/restore tests establish
  a starting precedent, not proof of nested hook support. Extend recording replay
  comparisons with pinned oracle provenance; a schema-valid recording alone does
  not establish gameplay parity.
- Supplement public behavioral tests with existing storage allocation/failure
  instrumentation where needed to prove memcpy-path allocation freedom and safe
  overflow. Add allocation checks for action traversal. Do not infer byte-copy
  safety merely from a container staying inline.
- Measure release-mode end-to-end stepping and clone workloads over representative
  encounters, card counts, powers, creatures and triggers. Report state bytes,
  allocations, clone cost and execution throughput together. Dense/sparse powers,
  inline/sparse card payloads, switch/table dispatch and specialized instructions
  remain experiments until results justify a choice. Representative benchmarking
  need not wait for every game mechanic to be implemented.
- Run relevant engine and replay-parity checks for each implementation slice, then
  the full registered suite for integration. This documentation task itself does
  not require engine code changes or new runtime tests.

## Out of Scope

- Implementing all content or claiming full-game parity as part of the architecture
  migration; existing content and victory milestones remain separate work.
- Multiplayer protocol, UI timing, animations and the native game's asynchronous
  infrastructure.
- A generic event bus, universal modifier framework, effect scheduler, scripting
  language, room interpreter or virtual gameplay object hierarchy.
- MCTS algorithms, hidden-state policy, evaluation functions and search-tree storage.
- Premature partial-run cloning, copy-on-write, undo systems, fixed universal
  capacities or unverified claims about maximum nesting/card payloads.
- Selecting dense/sparse power storage or a dispatch winner without representative
  measurements; replacing native semantic order to suit a storage layout.
- Changing comparison schemas, native save formats or diagnostic history retention
  merely to match a new internal layout.

## Further Notes

The source conversation contains superseded alternatives, conceptual C++ sketches,
unverified performance expectations and game-mechanics investigations. Preserve its
final architecture decisions without promoting those sketches or investigations
to proof about the pinned build. In particular, the conversation does not establish
an exhaustive mutable-card inventory, a maximum continuation depth, or a measured
advantage for switch dispatch or any power layout.

The existing full-game design, decision contract, state inventory, matching target,
implementation status and ADRs remain useful background. Where they contradict
this accepted architecture, this specification and ADR 0006 govern the target;
their audited evidence and explicitly bounded current support remain valid within
their original scope. The Ironclad victory and content tickets remain acceptance
work, not evidence that this architecture is already implemented.
