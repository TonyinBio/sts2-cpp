# Interactive CLI recording

`tools/oracle/play.py` starts a disposable sts2-cli session, displays combat state,
and records your decisions as you play. It writes actions-v1 input and states-v1
output using the [run-parity state schema](run-parity-spec.md).

The current exporter supports **Ironclad A0 against SHRINKER_BEETLE_WEAK**, with the
starter deck and Burning Blood. It does not record a complete map-to-ending run or
attach to a separately running CLI. Map, reward, shop, event, selector, other character,
and other encounter decisions need additional audited schema/exporter support. See
[oracle recording](oracle-recording-spec.md) for the pinned game/CLI versions.

## Start a recording

Requires Linux, Python 3.10+, .NET 9, the initialized clean sts2-cli submodule, and a
local installation of the supported game build. Build the validator first:

```sh
cmake --preset debug
cmake --build --preset debug
python3 tools/oracle/play.py \
  --game-dir '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2' \
  --output-dir build/recordings/my-session \
  --checkpoint-every 5
```

The output directory must be new. `--seed` defaults to ORACLECHECK01; `--validator`
selects another parity_validate executable; `--timeout` sets the reference response
deadline. The adapter also has its own settled-boundary deadline. Startup prints the
supported scenario explicitly.

## Play controls

Hand and enemy positions start at **1** and are displayed again after each action.

| Command | Effect |
| --- | --- |
| `play 2` | Play hand card 2; an attack targets enemy 1 by default |
| `play 2 1` | Play hand card 2 against enemy 1 explicitly |
| `end` | End your turn |
| `state` | Display the current state again |
| `save` | Add a checkpoint at the current action, without playing an action |
| `every 5` | Change the state interval; multiples are counted from action 1 |
| `quit` | Save the final validated state and stop |
| `help` | Display controls |

EOF and Ctrl+C also save the last validated state. A command interrupted before its
response is validated is excluded from the recording; its engine outcome is unknown.
The game process is isolated from terminal Ctrl+C and terminated when the recorder
closes. Invalid positions, insufficient energy, malformed commands and invalid intervals
are rejected locally. A reference error or invalid exported state stops the session;
previously published files remain available.

## Checkpoints and files

Every successfully acknowledged and validated game action is recorded, independently
of the state interval. At interval 5, states are retained after actions 5, 10, 15, etc.
The initial state is always retained. `save`, combat completion, and normal exit also
retain the current state, including a final partial interval. Repeated saves at the
same action do not duplicate checkpoints. The default interval is 1.

The exporter still returns and validates a state after each action. The interval controls
which of these snapshots enter the saved sequence; it does not skip game commands or
reduce capture work inside the reference process.

Read the matching files through:

```text
build/recordings/my-session/current/actions.json
build/recordings/my-session/current/states.json
```

The action file contains the complete validated action prefix. Its exact bytes match
`actions_sha256` in the state file. `transitions[*].action_id` identifies the action
immediately preceding that checkpoint. Autosave runs after each successful action,
including actions without a saved state. Both files are schema-validated before
publication. Their format is the same as the offline recorder's files, so actions.json
can be passed to `tools/oracle/record.py` later.

The program writes a new pair in a private generation directory, then atomically
switches the `current` directory symlink. The previous generation is retired after
publication, keeping disk usage bounded rather than retaining cumulative copies at
every autosave. Consumers should read the pair after the session stops; concurrent
consumers would need coordination to hold a generation while it is retired. These JSON
files are test recordings, not restorable game saves.

Build output, diagnostics and stderr live in `logs/`. Full protocol-state transcripts
are disabled by default to avoid duplicating every unsampled state on disk. Pass
`--protocol-log` to retain full responses for debugging; their size is independent of
the checkpoint interval. The source digest includes the interactive runner as well as
the injected adapter, shared recorder, schemas and game DLLs.


## Seeded runs and gameplay decisions

Pass `--run` to initialize a native seeded Ironclad run instead of the starter encounter.
This writes `seeded_run` setup and the `run_flow_v1` feature declaration. Submit any
schema-valid gameplay decision with the `act` command, for example:

```text
act '{"kind":"choose_option","option_index":0}'
act '{"kind":"select_cards","indices":[0]}'
act '{"kind":"proceed"}'
act '{"kind":"select_map_node","col":1,"row":1}'
act '{"kind":"abandon"}'
```

All positions inside action JSON are zero-based. The existing `play` command uses
one-based displayed hand positions and records card identities. See the complete
[action vocabulary](action-recording-spec.md#actions-and-replay-rules).

`state`, `map`, `deck`, and `inventory` inspect the last validated snapshot locally.
They do not call the gameplay backend, create action entries, or advance checkpoint
sampling. `map` shows reachable nodes at a captured map boundary; it does not request a
new native map screen. Saving, help, interval changes, and quitting likewise stay out
of the action log. Quitting the recorder preserves the run prefix; use `abandon` to
record abandonment as a gameplay decision. The schema rejects view-only action kinds.

The run recorder can traverse noncombat decisions but retains the existing bounded
combat exporter: reaching unsupported combat content stops recording explicitly.
