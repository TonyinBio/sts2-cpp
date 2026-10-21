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

EOF and Ctrl+C retain the last validated action prefix. If interruption happens during
an unacknowledged action, that action is excluded; its engine outcome is unknown.
The reference process is isolated from terminal Ctrl+C and terminated when recording
closes. A malformed response, invalid exported state or backend failure stops the
session and leaves the previously published pair available.

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

`oracle_display` provides native CLI presentation data at the settled boundary. It
uses the upstream combat, shop, rest, player and card formatting helpers and a read-only
version of event formatting. It does not invoke the UI decision detector that generates
rewards or changes rooms. Display data stays outside the saved state schema. Optional
live audits compare full snapshots before and after display generation with fixture
clocks fixed; no state fields are excluded from comparison.

## Checkpoints and files

Every validated gameplay action is recorded. At interval 5, snapshots are retained
after actions 5, 10, 15, etc. The initial state is always retained. Manual `save`, run
completion, and exit retain the current state, including a final partial interval.
Automatic gameplay decisions also count toward the interval. Repeated saves at the
same action do not duplicate checkpoints.

The exporter returns and validates a state after every action. Sampling controls which
snapshots are retained, not which actions execute or which states are validated.

```text
build/recordings/my-session/current/actions.json
build/recordings/my-session/current/states.json
```

The action file contains the complete validated prefix. Its exact bytes match
`actions_sha256` in the states file. Each checkpoint identifies its preceding action.
Both files are validated before publication. The writer creates a private generation,
then atomically switches the `current` directory symlink and retires the previous
generation. Consumers should read the pair after the session stops.

Build output and diagnostics are in `logs/`. Full protocol transcripts are disabled
by default; enable `--protocol-log` for debugging. Provenance includes the upstream
interface, recording hooks, adapters, schemas and managed dependencies.

## Saves and new runs

Upstream action-log saves created with `save` live under the recording directory's
`saves/` folder. Use `--load path/to/save.json` with a new output directory to replay
the logged decisions from seeded setup and continue recording. The replayed prefix
is recorded too. Saves created here retain ascension; for older upstream saves that
omit it, pass the original `--ascension` value when loading. This is separate from `current/actions.json`, which uses actions-v1.
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
