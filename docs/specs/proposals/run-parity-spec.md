# Native run combat parity state

This proposal adds combat state and pending choices to the native run-save layout so
tests can compare data held by both engines. Exporters are not implemented, and some
paused effects and choice types still need coverage.

See the [shared terms](../README.md#terms-used-in-these-sheets) for definitions of parity, snapshot, boundary, and model ID.

## On this page

- [Added top-level fields](#added-top-level-fields)
- [Combat objects, IDs, and links](#combat-objects-ids-and-links)
- [Gameplay fields to capture](#gameplay-fields-to-capture)
- [Enemy decisions, history, and pending choices](#enemy-decisions-history-and-pending-choices)
- [Capture timing and remaining gaps](#capture-timing-and-remaining-gaps)

The [schema](run-parity-v1.schema.json) replaces the previous run-parity-v1 proposal in
place. It extends [SerializableRun v20](../schemas/serializable-run-v20.schema.json) for
comparing held gameplay data in the real engine and simulator. The native schema and
nested native objects remain unchanged. This is one evolving representation; update both
exporters and affected tests together. Exporters are not implemented here.

The contract is grounded in the installed v0.111.0 implementation inspected in the
[runtime audit](../combat-runtime-audit.md) and [model-member
inventory](../combat-model-field-audit.md). It provides an initial concrete contract,
with the coverage limits below. It does not cover every choice within a paused effect.
JSON is used only for parity testing, never to initialize, resume or restore either
engine, or as the simulator's agent API.

## Added top-level fields

| Property | Meaning |
| --- | --- |
| `boundary_kind` | `awaiting_input` or `run_complete`, established by the test driver |
| `run_ending_reason` | Null while ongoing; otherwise `victory`, `death`, or `abandoned` |
| `current_room` | Current room using native `SerializableRoom`, or null; distinct from `pre_finished_room` |
| `native_model_state` | Additional gameplay fields on existing native objects, keyed by document-local JSON Pointer |
| `combat` | Combat runtime, or null when no relevant combat state remains |
| `pending_choice` | Unresolved card selection, or null during ordinary free play and after run completion |

Game build/hash, recorder version and boundary/action indices belong in the recording
envelope. They are not gameplay state. There are no profiles or compatibility paths for
the previous proposal. Strip these six additions to recover the unchanged native
payload. The extension reuses the native top-level fields because the native schema rejects extra
fields. Tests check that the reused definitions stay in sync with the native schema.

## Combat objects, IDs, and links

`combat.cards`, `creatures`, `powers`, `orbs`, `afflictions`, `card_plays`, `attacks`,
and `retained_inventory_models` are object tables. Their keys are arbitrary local
labels. Every reference is a JSON Pointer, for example `/combat/cards/c0` or
`/players/0/deck/3`. Repeated references mean the same object, not equal copies. A
combat card's `deck_version` references its actual permanent-deck entry; generated cards
can have no deck version. `clone_of` is a separate native relationship. There is no root
instance registry or binding for every native object.

### Which objects to keep

Retain only objects still used or referenced by gameplay: active collections, pending
sources, model-owned references, gameplay history and active card plays. This includes
floating Nightmare clones and cards no longer in piles. Exclude unreferenced previews
and unreachable removed objects. Keep removed potion/relic sources in
`retained_inventory_models` only while still referenced; do not duplicate live native
inventory entries there. Their owner/removal state remains relevant to hook membership.

Creature tables hold player references or monster data, exclusively. Player HP/max HP
remain authoritative in native `players`; monster HP lives in its creature's `monster`.
`hp_is_infinite` captures the gameplay distinction read by Hellraiser, without the
cosmetic distinction between infinite display styles. Side and pet ownership are
independent: allies are not a third native combat side. Power order belongs to each
creature's `powers` list. Detached, still-referenced powers additionally supply
`detached_owner`; omit it when ownership is available from a creature's power list.

### Ordering rules

Preserve order in allies/enemies/escaped lists, piles, powers, orb queues, local cost
modifiers, temporary star costs, history and active card plays. Native sets and
reference-keyed dictionaries are normalized by the comparator: set order and dictionary
entry order are not gameplay differences. Distinct dictionary keys must remain distinct
objects. `spawned_enemy_ids` is the encounter's unique definition list, not a count of
all individual spawned creatures. `players_taking_extra_turn` preserves native list
order; readiness collections are sets. Table insertion order has no meaning.

## Gameplay fields to capture

Each combat card reuses native `SerializableCard` in `saved`, then adds the actual
runtime relationships, replay count, local keywords, lifecycle flags, dynamic values that can change, cost storage, affliction reference and enchantment runtime. Native saved
properties are not repeated. `local_keywords` is the local keyword set, seeded from
definition's keywords when the native lazy backing field has not been initialized; it is
not the hook-derived effective keyword set.

### Costs and powers

Energy modifiers have the four real properties: amount, Absolute/Relative type,
expiration flags (0 combat, 2 turn, 4 played, 6 turn-or-played), and reduce-only.
Temporary star costs have cost and the two real cleanup flags. Resolved current costs,
fixed card classifications and display previews are omitted.

Powers capture amount, amount at turn start, skip-duration flag, applier/target, mutable
dynamic variable inputs and their additional fields specific to the model. Orb queues capture
capacity and order; Dark/Glass stored values are concrete model fields, rather than
generic derived passive/evoke previews.

### Fields specific to each model

`ModelFields` is a **closed set of typed, audited member names**, such as
`Claw._extraDamageFromClawPlays`, `PanachePower.alreadyApplied`,
`NightmarePower.selectedCard`, and `Pocketwatch._cardsPlayedLastTurn`. For nested power
`Data`, the key names identify the member on that model's internal Data object. Capture
every applicable listed field, including zero, false and null; use `{}` for models
without additional fields. Only include fields belonging to the containing model. The
schema checks names and types; model applicability and required per-model membership are
semantic exporter checks. This avoids an arbitrary untyped runtime-state escape hatch.
Native `[SavedProperty]` counters remain in native `props`.

Reference-valued fields use object references. Fabricator's `_lastSpawned` instead
stores a game definition, so it is a native ModelId. Dictionaries are arrays of
`{key, value}` entries to preserve reference-valued keys. Attack associations point to
`combat.attacks`. Calculated attack variables point to the corresponding captured card
`dynamic_vars` member. Their formulas remain pinned game definitions.

Numbers originating as C# decimal must be parsed and compared exactly. JSON numeric
syntax does not imply IEEE double; use a lossless JSON parser. Avoid preview updates or
other changes to gameplay state during export.

## Enemy decisions, history, and pending choices

Monster AI records the initial/current state, scheduled `next_move`, ordered log,
first-move flag and move-state runtime flags/follow-up IDs. State identifiers are local
to that monster's state machine; preserve its effective runtime states, including
dynamically created `STUNNED` states. Omit immutable graph definitions, delegates,
derived intents and intent damage previews. Monster move randomness comes from native
run MonsterAi RNG; the inspected private monster RNG was cosmetic.

### Combat history

Combat history is separate from native map-point history. Its variants identified by a
type field capture the gameplay-consumed entry kinds and their query inputs, including
the round/side/player-turn context, card-play identity, spent versus effective
resources, hand-draw origin, damage results and historical ethereal flag. Description
strings and the currently unconsumed diagnostic history kinds are omitted. A historical
reference points to the retained live object, rather than an invented past snapshot.

### Pending card selections

`pending_choice` uses the inspected CardSelectorPrefs: ordered candidates, min/max,
manual confirmation and cancellation, plus player and source relationships. It has no
rendered prompt, legal-action catalogue, duplicate-selection flag or scheduler label.
Ordinary free play has **null** pending choice. Candidate/source order is preserved.
Capture at the point the request awaits an answer, before UI-only tentative selection.

### Active card plays

`card_plays` preserves identity across history, relic associations and active plays.
`active_card_plays` is ordered outermost to innermost, with actual captured result
locations. CardPlay supplies replay index/count, target, resource values and autoplay
status. These are observed values from native execution, not a generic effect stack or
simulator instruction pointer.

## Capture timing and remaining gaps

At `awaiting_input`, gameplay is paused with no automatic effects still running until a
player command or pending answer. Turn-control fields remain explicit because a required
choice can interrupt turn start/end and nested effects. They must not be inferred from a
CLR task state such as Executing. At `run_complete`, pending choice is null and the
ending reason is explicit. Observe the engine's actual ending path: winning also
eventually kills players, so zero HP does not imply death. Capture after the ending
operation has completed; retain only the combat objects actually still held then. Do not
synthesize a final combat frame to compensate for victory cleanup.

### Coverage still missing

This initial contract deliberately leaves the following coverage work open:

* Per-operation captured locals beyond CardPlay/result location: suspended loop
  cursors, captured amounts/targets, hook listener snapshots and nested operation
  progress. The old generic EffectState is removed rather than pretending to cover these.
* Choice families beyond the inspected card selector. Add a discriminated concrete
  variant when a parity scenario needs one; do not disguise it as card selection.
* Mutable AI branch configurations/intent parameters not represented by move state
  flags or model fields, and attack callback/loop-local state.
* Verification of which fields are still used and which can be left out at all boundaries and representative
  victory, death and abandonment cleanup timing.

Reject a scenario explicitly if its relevant state cannot be exported. A structurally
valid snapshot alone is not proof of complete parity. For each supported scenario,
compare after matching actions and continue past choices to expose omitted state. When a
gap is found, add concrete fields, both exporter changes and a regression case in the
same change; migrate fixtures rather than supporting multiple state shapes.

### Checks outside the schema

JSON Schema does not enforce reference resolution, correct reference target kinds,
unique pile membership, exactly one owner, source-model field applicability, min-select
<= max-select, or consistency between active plays and history. These are semantic
checks for exporters/comparators. The schema tests include an illustrative Survivor
selection fixture and verify its reference resolution; the fixture is [synthetic
example](examples/run-parity-survivor.json), not a claimed full export from the real
engine.
