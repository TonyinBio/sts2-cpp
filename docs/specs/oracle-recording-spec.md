# Oracle recording

The interactive recorder captures gameplay decisions through a disposable, pinned
sts2-cli build and exports states-v1 envelopes whose snapshots conform to
[run-parity-v1.schema.json](schemas/run-parity-v1.schema.json). See the
[run state contract](run-parity-spec.md) and [action contract](action-recording-spec.md).

Build and run:

```sh
cmake --preset debug
cmake --build --preset debug
python3 tools/oracle/play.py \
  --output-dir build/recordings/starter \
  --checkpoint-every 5 \
  --game-dir '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2'
```

Python 3.10+, .NET 9, the initialized clean sts2-cli submodule and the local game
installation are required for live capture. Offline CTest does not need the game.
`--validator` selects another parity_validate executable; `--timeout` is the response
deadline. The adapter also requires a settled boundary within five seconds.
For interactive play and recording, use the [interactive recorder](interactive-recording-spec.md).

## Envelope

The `tests/parity/schema/states-v1.json` envelope retains schema_version 1 and profile
states-v1. It contains recording_id, actions_sha256, reference source information,
initial, and ordered `{action_id, state}` checkpoints for a subset of input actions.
Both initial and state contain full run-parity snapshots; their native schema_version
is 20. Feature declarations remain in the associated action recording.

`--checkpoint-every N` saves after every N actions; it defaults to 1. Every action is
executed and its exported state validated, even when that state is not retained.
The initial and final states are always saved. For 12 actions at interval 5, the file
contains initial plus checkpoints after actions 5, 10, and 12. A zero-action input
contains only initial. This reduces the state file size. Full protocol logs are optional with `--protocol-log`.

The validator accepts any strictly ordered subset of known action IDs, including an
empty subset or a final checkpoint before the last action. Such a file only supplies
comparison evidence at its recorded checkpoints. Duplicate, unknown and reordered IDs
are rejected. A run_complete checkpoint cannot precede any remaining input actions,
even when those actions have no checkpoints. Playback must execute all intervening
actions and compare only at the specified IDs; there is no full-run simulator replay
exporter yet.

The action digest hashes exact source bytes. The adapter digest covers injected source,
project files, the runner, all three envelope/action/state schemas, the native run
schema, and copied dependency DLLs. Consumers must independently verify the input digest.
The validator checks its syntax and action association.

## Export scope

The accepted reference is v0.111.0 DLL SHA-256
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9` and clean
sts2-cli revision `084d1aa3d8e118ca7ce8d8774ad16d6be9c92367`.

The seeded-combat driver supports Ironclad A0, Burning Blood, the default starter deck and
SHRINKER_BEETLE_WEAK. Required action features are basic_combat and
ironclad_starter_shrinker_v1. Its action driver remains play_card/end_turn only.
The seeded-run backend uses the shared combat exporter described below; model IDs
are no longer restricted to that starter encounter.

The exporter captures native run data through native serialization metadata, plus
current room, turn control, ordered piles, card runtime data, creature/power tables,
monster AI and gameplay-consumed history. History retains referenced cards, removed
powers and creatures through a queue drained to reference closure. Diagnostic history
kinds outside the contract are omitted. No generic counters or separate RNG projection
are synthesized. Native uint64 words and decimal inputs retain numeric JSON syntax.

Action identities such as card_0 and enemy_0 remain recording-driver identifiers.
Snapshot references use document-local JSON Pointers, such as /combat/cards/card_0.
Table labels need not persist between boundaries.

The seeded-combat driver captures settled player Play boundaries. The run driver also
captures suspended card selections, generated card rewards and bundle choices, with
the active play and source references retained until the answer resumes execution. An ended combat is exported after its hooks
settle, with its actual retained objects. Winning this encounter leaves the run ongoing
and uses awaiting_input. The driver observes CombatWon and CombatEnded to distinguish victory from the loss
path; a settled combat loss exports run_complete with reason death. Full-run victory
and abandonment remain outside this seeded driver. The state validator supports all
schema-defined ending reasons and null/residual combat.

Native save/start/run times are retained and compared in full. Reliable repeated-run
comparison requires deterministic clock control in the test fixtures.

## Publication and failures

Source and DLL copies live in a temporary directory; the installed game and submodule
stay unchanged. The shared `reference_runtime.py` module provides process communication,
validation and disposable builds for `play.py` and live audits; it has no batch recorder.
Build and runtime diagnostics are stored in the session's `logs/` directory.
Malformed responses, identity mismatches, invalid states, process failure and timeouts
stop recording. The last published action/state pair remains available. Each successful
action is validated before the interactive recorder publishes a new pair atomically.
See [interactive recording](interactive-recording-spec.md#checkpoints-and-files).

`starter.states.json` is a fresh live capture in the new contract, not a structural
conversion of a combat-only recording. Offline tests cover envelope/action association,
wire parsing, schema validation, reference checks and publication failure behavior.

## Run backend

`RunRecordingSession.cs` adds a separate backend protocol. The action recording schema supports gameplay decisions through `seeded_run`;
interactive run recording is the default in `play.py`.
These endpoints are available in the disposable build produced by `reference_build`:

```json
{"cmd":"oracle_run_start","request_id":"initial","setup":{"character":"Ironclad","ascension":0,"seed":"PARITY"}}
{"cmd":"oracle_run_action","request_id":"choice","action":"choose_option","args":{"option_index":0}}
{"cmd":"oracle_run_state","request_id":"inspect"}
```

All three return `oracle_state` with a run-parity snapshot. Initialization uses the
native seeded run setup and initial ancient event. Backend operations include
`select_map_node` (col/row), `choose_option` (option_index), `select_cards` (indices),
`skip_select`, `select_card_reward` (card_index), `reward_alternative` (index),
`skip_card_reward`, `select_bundle` (bundle_index), `take_reward` (index),
`skip_rewards`, `buy_card` (card_index), `buy_relic` (relic_index), `buy_potion`
(potion_index), `remove_card`, `pick_treasure` (index), `proceed`, and `abandon`.
These gameplay commands have corresponding actions-v1 variants. Recordings use
`proceed` as the canonical form of `leave_room`. Inspection commands are excluded.

Native asynchronous operations pause at captured choices without choosing a default.
Selection bounds, cancellation and confirmation flags come from native preferences.
Rewards use native synchronizers; room transitions preserve native hooks and act setup.
Victory and abandonment are marked before native cleanup kills players; a settled
native death signal produces the death ending. Terminal states reject further actions.

Noncombat `run_flow` captures current offers and prices, event page/options/variables
and event RNG, rest options, treasure offers, reward sets and suspended operations.
Manual or filtered card rewards that native saves cannot serialize have `saved: null`;
their concrete offered cards, native class name, taken status and skip policy remain
explicit. This is comparison data, not a restorable coroutine representation.
Active combats use the shared, checked combat exporter below, including supported
suspended choices and active card plays.

Run the optional live backend audit:

```sh
python3 tools/oracle/check_run_flow.py \
  --game-dir '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2' \
  --validator build/debug/parity_validate --logs build/run-flow-audit
```

The audit validates seeded Neow choices and map transition, native shop purchase,
Smith selection/upgrade, treasure collection, abandonment, native combat death,
and the Architect victory event. Room fixtures avoid
requiring unsupported combat models to reach each noncombat scenario.


Record a seeded run interactively with sparse checkpoints:

```sh
python3 tools/oracle/play.py --run \
  --output-dir build/recordings/my-run --checkpoint-every 3 \
  --game-dir '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2'
```

Gameplay actions alone advance the checkpoint interval. Snapshot inspection does not
send gameplay commands or append actions. Unsupported combat models still fail export.


## Shared combat model export

`RecordingSession.cs` captures common card, creature, power, monster AI, orb,
affliction and inventory state. It follows references through piles, history,
model fields and retained attacks until all reachable objects are exported. Generated
cards receive driver identities when first discovered; clone, floating-card, pet,
orb-owner and removed-inventory relationships remain explicit references.

`CombatModelFields.cs` reads model storage through the checked
`tools/oracle/combat-field-contract.json`, grounded in the
[model field audit](combat-model-field-audit.md). One serializer handles all 125
additional gameplay fields and eight AI state aliases, including nested power data,
ordered lists, sets and model-keyed dictionaries. The contract checks all 293 declared
storage members against the pinned DLL once per process; capture checks each encountered
model. Newly added content with no extra storage uses common export automatically.
New storage requires an audited classification and, for gameplay values, a typed schema
entry. Unknown members/shapes, active test overrides and missing power data fail;
presentation fields are excluded explicitly rather than by naming heuristics.

Native saved properties remain in native objects. Additional native-model fields and
stored numeric dynamic variables live under `native_model_state`; card/power runtime
fields live in their combat tables. Enchantment status, concrete orb values, monster
RNG, effective move follow-ups and registered AI aliases are captured without previews
or RNG draws. Every monster includes its own native `rng` record, separate from the run RNG set.
The starter recording fixture is recaptured with the shared exporter.

Stored attack commands include their targets, sources, hit configuration and results.
Calculated-damage variables reference their owning card's captured inputs; only pinned,
stateless calculation callbacks are accepted. Retained callbacks with captured storage,
unregistered AI aliases, custom move-state storage, and suspended combat continuations
still require explicit handlers. This implementation is not a claim of complete
all-mechanics or complete-run parity coverage.

Run the optional live audit:

```sh
python3 tools/oracle/check_combat_export.py \
  --game-dir '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2' \
  --validator build/debug/parity_validate --logs build/combat-export-audit
```

It checks all five character loadouts, multiple enemy types and a boss, generated and
floating cards, clone identity, attachments, nested power dictionaries, orbs, inventory,
AI aliases and calculated attacks. Negative probes check unknown fields, active test
overrides and uncaptured callbacks. Live fixtures fix native clock values so
repeated captures compare the entire state without excluding fields. The fixtures set up isolated states, not completed campaigns.
