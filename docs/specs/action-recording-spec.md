# Action Recording Specification

An action recording tells a runner how to start one combat and which player decisions to
replay. Expected states are stored in a separate result file.

See the [shared terms](README.md#terms-used-in-these-sheets) for definitions of parity, snapshot, boundary, and model ID.

## On this page

- [How to read the requirements](#how-to-read-the-requirements)
- [Overview](#overview)
- [File format and combat setup](#file-format-and-combat-setup)
- [Actions and replay rules](#actions-and-replay-rules)
- [Recording the results](#recording-the-results)
- [Validation and checks](#validation-and-checks)

| Attribute | Value |
| --- | --- |
| Status | Implemented; support is limited to the scope below |
| Profile / reference | `actions-v1` |
| Scope | Combat initialization, ordered player decisions, and result association. |

## How to read the requirements

**Must**, **must not**, and direct instructions state requirements. Examples and run
instructions explain how to use the format. Test results describe only the cases checked;
they do not add support for other mechanics.

## Overview

An action recording is a JSON document describing how to initialize one combat and which
player decisions to execute, in order. It contains no expected states. See
`tests/parity/schema/actions-v1.json`, `ActionRecording` in
`tests/parity/support/recording.hpp`, and `tests/parity/examples/starter.actions.json`.

Resulting states use the existing [run-parity-v1 format](run-parity-spec.md), validated by
`Snapshot::parse`. This action schema does not change or duplicate it. The example is
executable with the reference recorder; its captured output lives in
`tests/parity/fixtures/starter.states.json`. It is not paired with the synthetic
`starter.json` state example. The extra feature `ironclad_starter_shrinker_v1` declares
the recorder's audited encounter and exporter coverage.

## File format and combat setup

```json
{
  "schema_version": 1,
  "profile": "actions-v1",
  "recording_id": "starter-two-turns",
  "state_profile": "run-parity-v1",
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

### Recording and action IDs

`recording_id` identifies the scenario. Each action ID is nonempty and unique within
that recording; array order alone determines execution order. IDs remain useful when
inserting actions later. Empty action arrays are valid for checking initialization
alone.

### Required mechanics

`required_features` declares all mechanics required by the initialized encounter and its
subsequent actions. These exporter coverage declarations live outside run-parity state.
Consumers must supply their list of supported features. Validation rejects unknown features; it cannot
infer that a particular loadout or encounter needs additional features.

### Setup rules

The initial profile supports `seeded_combat` only. The reference runner translates it to
`start_run` with character, seed, ascension, and explicit English language, then
`enter_room` with type `combat` and the given encounter. Use the character's default
loadout. Encounter names are unlocalized protocol entries, e.g. `SHRINKER_BEETLE_WEAK`,
not localized titles. Unknown encounters fail at execution; schema validation does not
contain the game catalog. Ascension is an integer from 0 through 10.

Setup is a scenario-construction recipe, not an assertion that this combat was reached
through normal map progression. It is separate from replayed combat actions. No native
save paths, arbitrary protocol commands, snapshot loading, deck editing, or implicit
seed generation are supported by this first profile.

## Actions and replay rules

| Kind | Fields in addition to `kind` | Meaning |
| --- | --- | --- |
| `play_card` | `card_instance`, optional `target` | Play the identified hand card; supply a creature instance ID when targeting is required |
| `end_turn` | None | End the player's turn |
| `select_cards` | `card_instances` | Resolve a pending card selection with an ordered array of distinct card IDs |
| `select_target` | `target` | Resolve a pending creature-target selection |

Action instance IDs use recording-driver identities. Snapshot object links use
document-local JSON Pointers; the exporter maps action identities to runtime objects
independently of local snapshot labels. The driver must preserve its action mapping
throughout the recording. Actions never store shifting hand indices,
localized names, model IDs as substitutes for instance IDs, or raw object addresses. For
example:

```json
{"id":"strike-1","action":{"kind":"play_card","card_instance":"card_2","target":"enemy_0"}}
```

### Choices and invalid actions

Omit `target` for a card that does not take a target; explicit null is invalid. Empty
`card_instances` means selecting zero cards and is legal only if the pending selection
permits it. List order must be retained. A runner checks the current selection kind,
options and min/max counts before executing; it must not silently discard invalid
choices.

The parser checks shape and feature support, not action legality. An unavailable card,
unknown instance ID, wrong boundary, invalid target, or unsupported action mapping is a
replay error. Stop rather than replacing or skipping that action. In particular,
`select_target` defines the shared intent but still needs a reference implementation;
the current sts2-cli protocol does not provide a generic target-selection command.

### Sending actions to the reference adapter

Runners resolve identities to protocol indices immediately before each action. For
`select_cards`, sts2-cli currently expects a comma-separated `indices` string. That wire
encoding belongs in its adapter, not this format. Random results are generated from
engine RNG state; they are not player actions or alternate actions chosen by the runner.

## Recording the results

Keep action input and state output in separate files. The `states-v1` envelope is
defined in `tests/parity/schema/states-v1.json` and validated with the existing C++
run-state parser. The recorder produces:

- A versioned result envelope containing the recording ID, SHA-256 of the exact action
  file bytes, reference submodule revision, original game DLL hash, adapter identity,
  and envelope profile. Adapter identity must cover exporter/runtime patch changes.
- An initial run-parity-v1 snapshot after setup, before any player decision.
- Subsequent run-parity-v1 checkpoints for an ordered subset of actions, keyed by each
  checkpoint's preceding action ID. Capture at the next settled awaiting_input or run_complete boundary,
  not merely when a command is acknowledged. Combat completion alone does not imply run_complete.

### Sparse checkpoints

All player actions remain in the action input. The state recording always contains an
initial snapshot; transitions may contain any strictly ordered subset of action IDs.
A checkpoint describes the settled state after its named action, with all preceding
actions executed. Omitted checkpoints do not mean omitted actions. Consumers replay
every action between checkpoints, then compare at matching action IDs.

For example, 12 actions can have checkpoints after actions 5 and 10, or after 5, 10 and
12. With no transitions, only the initial state can be compared. The recorder's
`--checkpoint-every 5` option chooses the latter sequence automatically, always including
the final action. The default interval 1 retains N+1 snapshots for N actions. A choice
answer remains an explicit action whether or not the preceding choice state is saved.

Unknown, duplicate and out-of-order checkpoint IDs fail validation. A terminal initial
state requires zero input actions; a terminal checkpoint must refer to the final input
action. This also forbids unrecorded actions after a known terminal boundary.
Early termination with unconsumed input or failed export still fails the recorder.
Verify source information and action-file digest before comparison.

The reference runner and scoped exporter are implemented in `tools/oracle/record.py` and
`RecordingSession.cs`; see [Running the oracle recorder](oracle-recording-spec.md). The
current exporter supports `play_card` and `end_turn` in the audited starter encounter.
Selection actions remain part of actions-v1 but are explicitly rejected by this
exporter. These files are comparison inputs; snapshot-based engine initialization and
a C++ replay exporter for the full run-parity contract are not implemented.

## Validation and checks

Unknown fields, missing required fields, unsupported versions/profiles/actions, null
collections, duplicate action IDs, duplicate selected cards, and unsupported feature
names are rejected during validation. Input order is preserved without normalization.
The C++ validator uses the embedded action JSON schema and adds unique-action-ID and
feature checks. The shared schema validator implements only the keywords used by these
schemas.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug -L action_recording
```

The unit tests run offline once build dependencies are available and require no game
DLLs.
