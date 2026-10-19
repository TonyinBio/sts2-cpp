# Oracle Recording Specification

The oracle recorder runs saved actions in the reference game and captures the resulting
combat states. Tests then replay those actions in the C++ simulator and compare the
results.

See the [shared terms](README.md#terms-used-in-these-sheets) for definitions of parity, snapshot, boundary, and model ID.

## On this page

- [How to read the requirements](#how-to-read-the-requirements)
- [Overview](#overview)
- [How to run it](#how-to-run-it)
- [Result file format](#result-file-format)
- [Output files and failures](#output-files-and-failures)
- [Supported build and mechanics](#supported-build-and-mechanics)
- [IDs and counters](#ids-and-counters)
- [Capture timing and limits](#capture-timing-and-limits)
- [What was checked](#what-was-checked)

| Attribute | Value |
| --- | --- |
| Status | Implemented; support is limited to the scope below |
| Profile / reference | `states-v1 / ironclad_starter_shrinker_v1` |
| Scope | Reference recorder behavior and result envelopes for the audited combat profile. |

## How to read the requirements

**Must**, **must not**, and direct instructions state requirements. Examples and run
instructions explain how to use the format. Test results describe only the cases checked;
they do not add support for other mechanics.

## Overview

The recorder runs an actions-v1 file through a temporary extended build of sts2-cli and
writes a separate states-v1 file containing the initial combat-v1 snapshot and one
resulting snapshot per action. C++ validates the input, every exported snapshot, and the
completed result envelope. The separate C++ `parity_replay` runner replays supported
actions and compares the resulting simulator states against the recorded snapshots.

## How to run it

Requires Python 3.10+, Linux .NET 9 SDK, the initialized sts2-cli submodule, and a local
game installation. Build the C++ validation executable first:

```sh
cmake --preset debug
cmake --build --preset debug
python3 tools/oracle/record.py tests/parity/examples/starter.actions.json \
  --output build/recordings/starter.states.json \
  --game-dir '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2' \
  --verify-repeat
```

### Time limits and validator location

Use `--validator build/release/parity_validate` for another build location. `--timeout`
sets the per-response deadline in seconds (default 30); the adapter additionally checks
for a supported settled boundary within five seconds after an action returns. Build has
a separate 120-second timeout. The Python runner can terminate a stalled process even if
an adapter method blocks before reaching its boundary check.

### Repeat check

`--verify-repeat` runs the sequence in two fresh reference processes and requires exact
equality of snapshots and runtime identity before publishing. The recorder never chooses
player decisions. The checked-in victory action sequence is another runnable input:
`tests/parity/fixtures/ironclad-shrinker-victory.actions.json`.

## Result file format

The result must match `tests/parity/schema/states-v1.json` and contain the following
structure:

```text
schema_version: 1
profile: states-v1
recording_id
actions_sha256
reference:
  revision, game_dll_sha256, adapter_sha256, assembly_mvid, runtime_version
initial: combat-v1 snapshot
transitions:
  - action_id
    state: combat-v1 snapshot
```

`actions_sha256` hashes the exact action-file bytes. `adapter_sha256` covers the actual
injected C# adapter/stub sources and project files, Python runner, schema files, and
copied dependency DLLs. No timestamps or temporary paths appear in the result. The
caller must verify the input digest when consuming a result; the envelope parser checks
its shape but has no action-file bytes with which to recompute it.

## Output files and failures

Logs are retained next to the output in `<output-name>.logs/`: build output and separate
stdout/stderr transcripts per process. Engine stdout diagnostics are retained but not
confused with protocol responses. Malformed JSON responses, mismatched request IDs,
errors, crashes, timeouts, and actions following combat end fail the recording. Results
are written to a temporary file and atomically replace the destination only after all
validation succeeds. Existing output survives failure. Parent directories and logs may
still be created on a failed attempt.

### Temporary build and game files

The runner copies source and managed game dependencies into a temporary directory and
cleans up those copies on exit. Steam files and the submodule stay unchanged. It does
not launch the Steam game, apply setup.sh IL patches, regenerate localization, or write
native game saves. The adapter's existing Godot stubs and runtime patches remain active.
Recording mode bypasses normal UI-state construction and reward generation, so terminal
RNG states describe combat completion before rewards are generated.

## Supported build and mechanics

The runner accepts only the audited original DLL SHA-256
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9` (v0.111.0 Windows
managed DLL, tested under Linux .NET) and clean sts2-cli revision
`084d1aa3d8e118ca7ce8d8774ad16d6be9c92367`. Other builds fail until audited.

- Ironclad, ascension 0, default ten-card starter deck, Burning Blood, and
  `SHRINKER_BEETLE_WEAK` only; no potions or pets.
- Required feature list, in order: `basic_combat`, `ironclad_starter_shrinker_v1`.
- Normal Strike, Defend, and Bash; Shrink and Vulnerable powers; the beetle's fixed
  move sequence. Unsupported models, modifications, generated cards, and selectors fail.
- Supported actions: play card and end turn. Explicit card targets are required when
  the card targets an enemy. Incorrect targets, unavailable cards, and unplayable cards
  fail, rather than relying on the CLI's automatic target fallback.
- All five piles, recording-local identities, card upgrades/costs, player/enemy HP and
  block, energy, ordered powers/relics, move ID/history, and all 15 required RNG streams
  are exported. RNG words use decimal strings, matching combat-v1.

Selection actions are valid in actions-v1 but not implemented by this exporter. This
starter loadout cannot produce a selection, so expanding that coverage requires another
audited scenario and, where needed, extending setup. The runner fails instead of
fabricating selection state or claiming support.

## IDs and counters

At initial export, assign `card_0` onward by hand, draw, discard, exhaust, then playing
pile enumeration, retaining these identities as cards move. Keep the card inventory in
that registration order. `enemy_0`, `player_0`, and `relic_0` identify the only enemy,
player, and relic. Powers receive `power_0` onward when first exported, in
enemy-then-player traversal order, and retain IDs until removed. This order is part of
this adapter's feature definition. Dead enemies stay in the enemy list through the
terminal snapshot.

### Required counters

The following keyed counters are required for `ironclad_starter_shrinker_v1`:

| Object | Counters |
| --- | --- |
| Player | `turn_number` |
| Card | `current_play_index` |
| Enemy | `performed_first_move`; `performed_move:<state ID>` for every state in its machine |
| Power | `amount_on_turn_start`, `skip_next_duration_tick`, `applier_actor` |
| Shrink power | Additional `damage_decrease_percent` (30) |

Boolean counters use 0/1. `applier_actor` uses -1 for none, 0 for player_0, 1 for
enemy_0. Vulnerable's 1.5 multiplier is checked against the audited model. Relic
counters and card flags are empty in this scope. Non-default card modifications are
rejected rather than omitted. Static numeric rules come from the pinned game
model/build, not localized text. The beetle's own per-monster RNG is not consumed by its
audited fixed move implementation; its move rolling uses the shared run MonsterAi
stream.

`move_id` is the current/planned move; `move_history` lists performed moves
chronologically, excluding an unperformed current intent. The exporter preserves the
original machine reference so death/removal cannot erase this history before terminal
export.

## Capture timing and limits

For ongoing combat, the adapter requires player Play phase, player side, no running
executor/action, no monster performing a move, and no pending selector/reward/bundle. It
also requires end turn to advance the player's turn number unless combat ended.

### Combat completion

Terminal snapshots are taken after the supported action settles and combat cleanup/hooks
finish. On victory, Burning Blood healing has occurred and piles have been cleared, so
the live card inventory is empty. On defeat, the engine may retain piles and powers.
These differences reflect the inspected DLL behavior; a C++ adapter must use the same
boundary convention. Rewards are outside this snapshot scope.

This is evidence for the supported reference scenarios, not proof that all sts2-cli
patches match the running Godot game. Arbitrary reference snapshot loading remains
unimplemented. C++ engine replay is supported for the audited starter encounter profile.
Extending scope requires inspecting state dependencies, adding exporters/guards, and
adding reference fixtures before changing the feature definition.

## What was checked

Live same-input repeat runs cover defend, attack, Bash/Vulnerable, Shrink, reshuffle,
victory with healing/cleanup, and defeat. Invalid runtime card identity fails without
replacing an existing output. Offline CTest tests cover result shape, action
association, invalid snapshots/source information, identity consistency, process
crash/timeout, malformed responses, output preservation, and stderr draining. No live
game is needed by CTest.
