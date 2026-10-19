# Oracle recording

The recorder runs actions-v1 input through a disposable, pinned sts2-cli build and
exports states-v1 envelopes whose snapshots conform to
[run-parity-v1.schema.json](schemas/run-parity-v1.schema.json). See the
[run state contract](run-parity-spec.md) and [action contract](action-recording-spec.md).

Build and run:

```sh
cmake --preset debug
cmake --build --preset debug
python3 tools/oracle/record.py tests/parity/examples/starter.actions.json \
  --output build/recordings/starter.states.json \
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
contains only initial. This reduces the state file size, but full per-action protocol
responses still appear in the offline recorder's logs.

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

The live driver supports Ironclad A0, Burning Blood, the default starter deck and
SHRINKER_BEETLE_WEAK. Required action features are basic_combat and
ironclad_starter_shrinker_v1. Supported actions are play_card and end_turn; unsupported
models, attachments, generated cards, selectors, potions, pets, and modifiers fail.

The exporter captures native run data through native serialization metadata, plus
current room, turn control, ordered piles, card runtime data, creature/power tables,
monster AI and gameplay-consumed history. History retains referenced cards, removed
powers and creatures through a queue drained to reference closure. Diagnostic history
kinds outside the contract are omitted. No generic counters or separate RNG projection
are synthesized. Native uint64 words and decimal inputs retain numeric JSON syntax.

Action identities such as card_0 and enemy_0 remain recording-driver identifiers.
Snapshot references use document-local JSON Pointers, such as /combat/cards/card_0.
Table labels need not persist between boundaries.

Capture requires player Play phase during ongoing combat, no running action or monster
move, and no pending selector/reward/bundle. An ended combat is exported after its hooks
settle, with its actual retained objects. Winning this encounter leaves the run ongoing
and uses awaiting_input. The driver observes CombatWon and CombatEnded to distinguish victory from the loss
path; a settled combat loss exports run_complete with reason death. Full-run victory
and abandonment remain outside this seeded driver. The state validator supports all
schema-defined ending reasons and null/residual combat.

Native save/start/run times are retained. `--verify-repeat` requires equality of every
saved checkpoint field in two fresh processes, including these clock-derived native fields; it can fail
when timestamps differ. It never silently ignores them. Deterministic clock control is
required for reliable full-run repeat checks.

## Publication and failures

Source and DLL copies live in a temporary directory; the installed game and submodule
stay unchanged. Recording bypasses UI preview and reward generation. Logs next to the
output retain build output and stdout/stderr for each process. Malformed responses,
identity mismatches, invalid states, process failure, timeout and actions after
run_complete fail before publication. Runtime combat commands also reject actions after
combat ends. Output is atomically replaced only after validation; failure preserves an
existing output.

`starter.states.json` is a fresh live capture in the new contract, not a structural
conversion of a combat-only recording. Offline tests cover envelope/action association,
wire parsing, schema validation, reference checks and publication failure behavior.

## Run backend

`RunRecordingSession.cs` adds a separate backend protocol. The action recording
schema and interactive recorder controls retain their existing combat scope.
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
These are backend command names, not additions to actions-v1.

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
Active combats still use the existing closed combat exporter. Additional combat
models and suspended combat choices remain unsupported and fail explicitly.

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
