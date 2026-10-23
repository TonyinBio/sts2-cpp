# Interactive CLI recording

`tools/oracle/play.py` runs the pinned sts2-cli Python play interface with an injected
recording transport. Its renderers, room prompts, map, card descriptions, enemy intents,
and input validation come from `sts2-cli/python/play.py`. The submodule stays unchanged:
`upstream_play.py` applies checked transport and recording hooks to an in-memory copy.
The recorder owns the disposable reference process, full-state validation and atomic
publication. It does not maintain a second terminal interface.

## Start a recording

Requires Linux, Python 3.10+, .NET 9, the initialized clean sts2-cli submodule, and the
supported local game installation. Build the validator first:

```sh
cmake --preset debug
cmake --build --preset debug
python3 tools/oracle/play.py \
  --game-dir '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2' \
  --output-dir build/recordings/my-session \
  --character Silent --seed MYSEED --checkpoint-every 5
```

The output directory must be new. All five characters are selectable with `--character`.
`--ascension` accepts 0–10; `--lang` accepts `en`, `zh`, or `both`; `--auto` uses upstream
autoplay. The default is a seeded Ironclad A0 run, with seed ORACLECHECK01 and English
presentation. `--run` remains accepted for older commands; runs are now the default.
`--validator` selects another parity_validate executable; `--timeout` sets the response
deadline. Unsupported runtime exports still stop recording explicitly; this interface
integration does not establish complete-run content coverage.

## Play controls

Use the same controls as sts2-cli. Displayed indices are **zero-based**.

| Prompt or command | Input |
| --- | --- |
| Map | Path number |
| Combat | Card index, `e` to end turn, or `p0` to use potion slot 0 |
| Target selection | Enemy index |
| Card reward | Card index, or `s` when skipping is allowed |
| Card selection | Index or comma-separated indices; cancellation where allowed |
| Event or rest site | Option index |
| Shop | Card index, `r0` for a relic, `p0` for a potion, `rm`, or `leave` |
| `help` | Show upstream help |
| `map`, `deck`, `potions`, `relics` | Inspect without recording an action |
| `save` | Save an upstream action-log file and add a recording checkpoint |
| `saves` | List action-log saves in this session's `saves/` directory |
| `quit` | Save the recording prefix and use the upstream quit/save prompt |
| `abandon` | Confirm abandonment and record it as a gameplay decision |

The former `play N`, `end`, and raw `act` interface has been replaced by the upstream
prompts. Choose the checkpoint interval at launch with `--checkpoint-every N`.

EOF and Ctrl+C retain the committed action prefix. An action interrupted before
its commit marker is published is excluded; an interruption after the marker
switch may leave it committed even if the interface did not receive success.
The reference process is isolated from terminal Ctrl+C and terminated when recording
closes. A malformed response, invalid exported state or backend failure stops the
session and leaves the committed journal prefix and any earlier JSON export available.

## Gameplay and display boundaries

Each user decision is translated to an actions-v1 entry before execution. Card and
target indices are resolved against the current snapshot to recording identities.
Combat selection answers retain candidate identities; noncombat answers use the
schema's indexed form. Native queued actions pause at pending choices and resume only
after a recorded answer.

The upstream interface implicitly advances completed rooms, collects treasure and
processes rewards. The recording transport performs those operations through explicit
recorded decisions, including `proceed`, `pick_treasure`, and `take_reward`. It stops
when the next player choice is pending. Inspecting the map, formatting state, saving
and quitting do not create gameplay actions.

Automatic collection attempts each reward once per reward set. A skipped card remains
untaken in native state; the interface continues with other rewards and records
`skip_rewards` to close the set, rather than offering the skipped card again.

`oracle_display` provides native CLI presentation data at the settled boundary. It
uses the upstream combat, shop, rest, player and card formatting helpers and a read-only
version of event formatting. It does not invoke the UI decision detector that generates
rewards or changes rooms. Display data stays outside the saved state schema. Optional
live audits compare full snapshots before and after display generation with fixture
clocks fixed; no state fields are excluded from comparison.

At the shared settled boundary, recording and replay materialize inventory potions'
lazy dynamic variables before snapshot capture. Inventory display therefore cannot
introduce a potion amount that is absent from a replay without display. Export and
comparison retain those values without filtering or normalization.

## Checkpoints and files

Every validated gameplay action is recorded. At interval 5, snapshots are retained
after actions 5, 10, 15, etc. The initial state is always retained. Manual `save`, run
completion, and exit retain the current state, including a final partial interval.
Automatic gameplay decisions also count toward the interval. Repeated saves at the
same action do not duplicate checkpoints.

The exporter returns and validates a state after every action. Sampling controls which
snapshots are retained, not which actions execute or which states are validated.

The interactive recorder keeps one validator process per run. Setup, supported
features, provenance, and the initial snapshot are validated at initialization.
Each new action uses the existing action-entry schema; each returned snapshot
receives full snapshot and reference-graph validation once. The live path does
not send earlier actions or snapshots to the validator. Sequential action IDs,
the latest retained checkpoint, and terminal boundaries enforce the incremental
recording relationships. Standalone export and verification still validate the
complete pair independently.

The durable recording consists of three files in one session directory:

```text
actions.jsonl
states.jsonl
commit.json
```

`actions.jsonl` begins with `{"kind":"setup","actions":<empty actions-v1 document>}`,
followed by ordinary actions-v1 entries (`id` and `action`), one per line.
`states.jsonl` begins with `{"kind":"initial","reference":<provenance>,"state":<snapshot>}`,
followed by ordinary states-v1 transitions (`action_id` and `state`), one per line.
Every line is a complete UTF-8 JSON object followed by a newline. Existing action
and snapshot definitions are reused by [journal-v1](../../tests/parity/schema/journal-v1.json).

The `journal-v1` commit marker contains `schema_version`, `profile`, `recording_id`,
`actions_bytes`, `states_bytes`, `action_count`, and `checkpoint_count`. Byte
lengths include the headers and newline delimiters; counts exclude the headers.
For every action, including replayed actions during `--load`, the recorder appends
and `fsync`s the new action and any retained snapshot, then writes and `fsync`s a
temporary marker, atomically replaces `commit.json`, and `fsync`s the session
directory before returning success. Initialization also persists the parent
directory entry. A manual save can commit a snapshot for the latest action
without appending another action; repeated saves do not duplicate it.

The marker selects a matched committed prefix of both journals. Bytes beyond
its offsets are uncommitted, even if they form complete lines. An interruption
after the marker switch may leave the new action committed even though the
caller did not receive success. A publication error stops further gameplay
dispatch in that session. Durability assumes a filesystem that honors file and
directory `fsync`; this does not protect against arbitrary storage corruption.

Manual save and normal exit materialize a compatible complete JSON export:

```text
build/recordings/my-session/current/actions.json
build/recordings/my-session/current/states.json
```

The action file contains the complete validated prefix. Its exact bytes match
`actions_sha256` in the states file. Each checkpoint identifies its preceding action.
Both files are validated before export publication. The writer creates a private
generation under `.exports/`, atomically switches the `current` directory symlink,
and retires the previous export generation. Ordinary gameplay does not create or
refresh `current`; it may be absent or represent an earlier manual save.
Consumers should read the pair after the session stops.

After interruption, export an offline session without starting the game:

```sh
python3 tools/oracle/play.py --export build/recordings/my-session \
  --validator build/debug/parity_validate
```

This reads only the committed byte prefixes, independently validates the complete
recording, publishes the three JSON files, and truncates any uncommitted tails.
Missing, truncated, malformed, or invalid committed data is an error, not a tail
to discard; a failed validation preserves the previous export. The command must
not run concurrently with an active recorder. It does not synthesize a final
checkpoint for an unsampled state lost with the process, execute gameplay, or
resume recording in the same directory.

Build output and diagnostics are in `logs/`. Full protocol transcripts are disabled
by default; enable `--protocol-log` for debugging. Backend provenance includes injected adapters, schemas, protocol/build helpers and
managed dependencies. The pinned wrapper revision also identifies the upstream interface.
Recorder UI code is outside backend identity so the verifier builds the same backend.

## Saves and new runs

Upstream action-log saves created with `save` live under the recording directory's
`saves/` folder. Use `--load path/to/save.json` with a new output directory to replay
the logged decisions from seeded setup and continue recording. The replayed prefix
is recorded too. Saves created here retain ascension; for older upstream saves that
omit it, pass the original `--ascension` value when loading. This is separate from `current/actions.json`, which uses actions-v1.
JSONL journals are not accepted by `--load`. Existing action-log saves and existing
complete JSON replay/validation inputs remain supported.
Native `.save` continuation is not an accepted recording setup; `--continue` is not
exposed. A native save written by the upstream quit prompt can be opened with sts2-cli.

Choosing `n` after game over starts a fresh recording in a numbered sibling directory
such as `my-session-002`, with a new seed. Each run has its own action/state pair.

## Live interface audit

```sh
python3 tools/oracle/check_play.py \
  --game-dir '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2' \
  --validator build/debug/parity_validate --logs build/play-audit
```

This compares a seeded Neow/map/combat trace with native rendering enabled and disabled.
Fixture clocks are fixed; every snapshot field is compared. The combat export and choice
audits additionally check display purity for all five character loadouts and suspended
Survivor/Discovery selections.

## Canonical recording verification

New sessions carry the explicit logical clock described in the
[action contract](action-recording-spec.md#reproducible-clock-input).
Verify a stopped session's canonical pair with:

```sh
python3 tools/oracle/replay.py --game-dir '/path/to/game' \
  --actions build/recordings/my-session/current/actions.json \
  --states build/recordings/my-session/current/states.json \
  --logs build/recordings/my-session/verification
```

This verifies the exact action-file digest, schemas, independently built backend
provenance and runtime identity before comparing initialization and every saved
checkpoint. It executes every action, including those between sparse checkpoints,
without using the interface transport's automatic settling loop. Already-recorded
reward collection, treasure choices and room transitions therefore execute once.
The full-state comparator retains exact decimal tokens and document-local identity
normalization; clock fields are compared. Expected snapshots never initialize the
backend. Divergence exits with status 2 and the action/checkpoint plus first differing
JSON Pointer. Reference execution, compatibility and input errors exit with status 1.
These results establish reference recording reliability, not simulator correctness.

Run `tools/oracle/check_recording_replay.py --game-dir '/path/to/game'
--output-dir build/self-replay-check` to regenerate the initial corpus and replay each
recording in two independent processes. The checked-in corpus and its coverage gaps
are described in `tests/parity/fixtures/reference/README.md`.
