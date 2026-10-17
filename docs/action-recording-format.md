# Action recordings, version 1

An action recording is a JSON document describing how to initialize one combat and
which player decisions to execute, in order. It contains no expected states. See
`tests/parity/schema/actions-v1.json`, `ActionRecording` in `tests/parity/recording.hpp`,
and `tests/parity/examples/starter.actions.json`.

Resulting states use the existing [combat-v1 format](combat-state-format.md),
validated by `Snapshot::parse`. This action schema does not change or duplicate it.
The example is executable with the reference recorder; its captured output lives in
`tests/parity/fixtures/starter.states.json`. It is not paired with the synthetic
`starter.json` state example. The extra feature `ironclad_starter_shrinker_v1` declares
the recorder's audited encounter, powers, and counter conventions.

## Envelope and initialization

```json
{
  "schema_version": 1,
  "profile": "actions-v1",
  "recording_id": "starter-two-turns",
  "state_profile": "combat-v1",
  "required_features": ["basic_combat", "ironclad_starter_shrinker_v1"],
  "setup": {
    "kind": "seeded_combat",
    "seed": "ORACLECHECK01",
    "character": "Ironclad",
    "ascension": 0,
    "encounter": "SHRINKER_BEETLE_WEAK"
  },
  "actions": [
    {"id": "end-turn-1", "action": {"kind": "end_turn"}},
    {"id": "end-turn-2", "action": {"kind": "end_turn"}}
  ]
}
```

`recording_id` identifies the scenario. Each action ID is nonempty and unique within
that recording; array order alone determines execution order. IDs remain useful when
inserting actions later. Empty action arrays are valid for checking initialization alone.

`required_features` declares all mechanics required by the initialized encounter and
its subsequent actions, using the same feature vocabulary as combat-v1. Consumers must
supply their allowlist. Validation rejects unknown features; it cannot infer that a
particular loadout or encounter needs additional features.

The initial profile supports `seeded_combat` only. The reference runner translates
it to `start_run` with character, seed, ascension, and explicit English language, then
`enter_room` with type `combat` and the given encounter. Use the character's default
loadout. Encounter names are unlocalized protocol entries, e.g. `SHRINKER_BEETLE_WEAK`,
not localized titles. Unknown encounters fail at execution; schema validation does not
contain the game catalog. Ascension is an integer from 0 through 10.

Setup is a scenario-construction recipe, not an assertion that this combat was reached
through normal map progression. It is separate from replayed combat actions. No native
save paths, arbitrary protocol commands, snapshot loading, deck editing, or implicit
seed generation are supported by this first profile.

## Player decisions

| Kind | Fields in addition to `kind` | Meaning |
| --- | --- | --- |
| `play_card` | `card_instance`, optional `target` | Play the identified hand card; supply a creature instance ID when targeting is required |
| `end_turn` | None | End the player's turn |
| `select_cards` | `card_instances` | Resolve a pending card selection with an ordered array of distinct card IDs |
| `select_target` | `target` | Resolve a pending creature-target selection |

IDs use combat-v1 recording-local identities. The recorder must establish and preserve
the same mapping in every state it emits. Actions never store shifting hand indices,
localized names, model IDs as substitutes for instance IDs, or raw object addresses.
For example:

```json
{"id":"strike-1","action":{"kind":"play_card","card_instance":"card_2","target":"enemy_0"}}
```

Omit `target` for a card that does not take a target; explicit null is invalid. Empty
`card_instances` means selecting zero cards and is legal only if the pending selection
permits it. List order must be retained. A runner checks the current selection kind,
options and min/max counts before executing; it must not silently discard invalid choices.

The parser checks shape and feature support, not action legality. An unavailable card,
unknown instance ID, wrong boundary, invalid target, or unsupported action mapping is a
replay error. Stop rather than replacing or skipping that action. In particular,
`select_target` defines the shared intent but still needs a reference implementation;
the current sts2-cli protocol does not provide a generic target-selection command.

Runners resolve identities to protocol indices immediately before each action. For
`select_cards`, sts2-cli currently expects a comma-separated `indices` string. That wire
encoding belongs in its adapter, not this format. Random results are generated from
engine RNG state; they are not player actions or alternate actions chosen by the runner.

## Result files

Keep action input and state output in separate files. The `states-v1` envelope is
defined in `tests/parity/schema/states-v1.json` and validated with the existing C++
combat parser. The recorder produces:

- A versioned result envelope containing the recording ID, SHA-256 of the exact action
  file bytes, reference submodule revision, original game DLL hash, adapter identity,
  and state profile. Adapter identity must cover exporter/runtime patch changes.
- An initial combat-v1 snapshot after setup, before any player decision.
- One subsequent combat-v1 snapshot per action, keyed by its unique action ID and in
  execution order. Capture at the next settled player-action, selection, or combat-end
  boundary, not merely when a command is acknowledged.

For N actions, a complete recording has N+1 snapshots. A selection is an explicit action
in the input, so a card that prompts for a choice yields a selection-boundary snapshot
before the next recorded selection resolves it. Earlier-than-expected combat termination,
unconsumed input, missing results, or failed export makes the recording incomplete.
Do not present partial results as passing reference data or overwrite a good result file
on failure. Verify provenance and action-file digest before comparison.

The reference runner and scoped exporter are implemented in `tools/oracle/record.py`
and `RecordingSession.cs`; see [Running the oracle recorder](oracle-recording.md).
The current exporter supports `play_card` and `end_turn` in the audited starter encounter.
Selection actions remain part of actions-v1 but are explicitly rejected by this exporter.
C++ simulator import/replay and comparison are implemented for this supported combat
profile in `parity_replay`; see [simulator coverage](simulator.md).

## Validation

Unknown fields, missing required fields, unsupported versions/profiles/actions, null
collections, duplicate action IDs, duplicate selected cards, and unsupported feature
names fail validation. Input order is preserved without normalization. The C++ validator
uses the embedded action JSON schema and adds unique-action-ID and feature checks.
The shared schema validator implements only the keywords used by these schemas.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug -L action_recording
```

The unit tests run offline once build dependencies are available and require no game DLLs.
