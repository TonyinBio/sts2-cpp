# Shared combat state format, version 1

Both the reference adapter and simulator adapter must export the same canonical combat
representation. The schema is `tests/parity/schema/combat-v1.json`; its validated C++
representation is `sts2::parity::Snapshot` in `tests/parity/snapshot.hpp`.

This establishes the representation and offline comparison rules. Neither engine is
mapped into it yet. `tests/parity/examples/starter.json` is synthetic schema example
data, not a captured reference fixture or evidence of simulator/game parity.

## Scope and compatibility

`schema_version: 1`, `profile: "combat-v1"` describes single-player combat at decision
boundaries. Its RNG stream catalog matches the locally inspected v0.111.0 DLL.
Run/map progression, multiplayer, orbs, Osty, stars, arbitrary pending action queues,
and mechanics requiring additional state are outside this initial profile.

Every snapshot declares `required_features`. Each consumer supplies its explicitly
supported feature names to `Snapshot::parse`. A feature outside that allowlist fails
validation, even when the JSON structure is valid. The synthetic example uses
`basic_combat`. An adapter must declare every mechanic needed by the encounter and
loadout; this validator cannot infer complete feature requirements from a model ID.
A production adapter must also reject unimplemented models and state it cannot export.
Do not label arbitrary cards `basic_combat` merely because they fit the schema.

A missing required field, unknown field, unknown version/profile/enum, null in place of
an empty collection, or unsupported feature is an error. Empty arrays and counter maps
must be present explicitly. Integer game values use signed 64-bit integers, not floats.
Breaking field/semantic changes require a new version or profile; never silently drop
state to make a comparison pass.

## Decision boundaries

- `player_action`: all effects from the previous action have settled and the player can
  submit a new normal combat action. `active_side` must be `player`, outcome `ongoing`.
- `selection`: effects are paused for an explicit card or creature-target choice.
  Include ordered candidate identities and minimum/maximum selection counts.
- `combat_end`: effects have settled, outcome is `victory` or `defeat`, active side `none`.

A command acknowledgment is not proof of completion. Each adapter is responsible for
establishing one of these boundaries. A selection can pause during either side's effects.
No-selection boundaries require `{ "kind": "none", "options": [], "min": 0, "max": 0 }`.

## State fields

| Group | Required representation |
| --- | --- |
| Combat | Round (starts at 1), active side, outcome, boundary |
| Player | Instance/model IDs, HP/max HP, block, energy/max energy, powers, relics, counters |
| Enemies | Every combat enemy including dead enemies, instance/model IDs, HP/max HP, block, powers, current move ID, chronological move history, counters |
| Cards | Every live combat card's instance/model IDs, upgrade level, effective cost, flags, counters |
| Piles | Hand, draw, discard, exhaust, playing: ordered card instance IDs |
| Selection | Kind (`none`, `card`, `target`), ordered options, min/max count |
| Randomness | Required run and player RNG streams, including counters and state words |

Effective cost has `kind: fixed | x | unplayable` and `amount`. Only fixed costs use a
nonzero amount; X and unplayable costs must use zero. Flags are an ordered list of unique adapter-defined
semantic tags. Counters are keyed, named integer values needed by supported mechanics;
missing keys do not default to zero. Counter names and flag meanings must be agreed by
both adapters and covered by their feature definitions. New mechanics that do not fit
these fields require a schema change, rather than being encoded as opaque text.

Power instances have instance/model IDs, amount, and counters. Relic instances have
instance/model IDs and counters. Preserve their iteration order until we have evidence
that reordering cannot affect hook execution. Current move IDs and history similarly
need an explicit mapping from each reference monster implementation; do not substitute
localized intent descriptions.

The `randomness` object contains required `run` and `player` stream maps. Include
all streams listed in the schema, each with a nonnegative integer counter and
`s0`–`s3` as canonical decimal strings representing unsigned 64-bit words. Leading
zeros, signs, floats, numeric JSON words, and values above `18446744073709551615`
are rejected. Standard schema tooling needs our `uint64-decimal` format check for
that upper bound; the C++ validator enforces it. Compare all stream states exactly,
including streams not used by the last action.

## Object IDs and ordering rules

Use unlocalized model IDs such as `CARD.STRIKE_IRONCLAD`. Adapters map simulator enums
and reference `ModelId`s explicitly. No localized names/descriptions, timestamps, object
addresses, or generated UI values belong in the state.

Instance IDs are recording-local strings, unique across cards, creatures, powers, and
relics. The recorder establishes identities at combat setup; the simulator importer
retains that mapping. Generated objects receive deterministic identities from their
creation sequence. Never assign IDs afresh at every snapshot or match identical card
models by their current pile position.

Each card appears in exactly one pile. Unknown pile references, duplicate placement,
missing placement, and duplicate instance IDs fail validation. Exhausted cards remain
in the inventory; removed cards leave it. Pile arrays preserve engine order, with draw
index 0 representing the next card to draw. Selection target IDs refer only to creatures,
card selection IDs only to live cards.

Every array must match exactly in its supplied order, including the card inventory,
feature declarations, card flags, enemies/targets, piles, effects, and move history.
`Snapshot::parse` validates the input without sorting or reordering it. A sequence
with the same entries in a different order is a comparison failure.
JSON object key order is immaterial. Comparisons otherwise use exact values and report
JSON Pointer paths, expected values, and actual values. Missing keyed counters are
reported explicitly, with JSON null representing absence in the difference report.

## Recording envelope and action mapping

Snapshot JSON describes state, not its provenance. Future fixture files must wrap it
with a fixture ID, reference revision, game DLL SHA-256, initial snapshot including RNG state,
prerecorded player actions and selection decisions, and expected snapshots. Verify these
identities before replay. Keep incompatible game builds in separate fixture sets.

Replay executes the prerecorded actions and selection decisions in order. Seeds
and restored RNG state reproduce game behavior such as draws and enemy moves;
they do not choose player actions. If a recorded action is unavailable, stop and
report the divergence rather than selecting a replacement. Random outcomes are
produced by the simulator from restored RNG state.

Actions must refer to the recording's stable identities, for example:

```json
{"kind": "play_card", "card_instance": "card_0", "target": "enemy_0"}
```

Adapters resolve identities to current protocol indices immediately before executing.
End-turn has no card or target. Selections reference the advertised options. Reject
unknown identities or unavailable actions. The action envelope and replay runner are
subsequent work; they are not accepted as fields in a version-1 snapshot.

## Tests and diagnostics

Run the combat state tests without installed game DLLs:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug -L combat_state
```

The comparator returns all differing fields at one boundary. A future replay runner
should stop at the first divergent boundary and prefix those differences with fixture
ID, action index/description, and boundary. Invalid snapshots fail before comparison;
validation failures must never be reported as a passing fixture or skipped test.

Passing reference fixtures are evidence of simulator accuracy for their scenarios and
compared state. Track fixture/mechanic coverage and retain known gaps in accuracy reports.
