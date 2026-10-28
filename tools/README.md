# Tools

Upstream interface problems are tracked locally in the [sts2-cli issue tracker](../docs/sts2-cli-issues.md).

- `parity/src/` and `parity/include/`: shared C++ recording validation, exact comparison, replay, and the original simulator adapter.
- `../data/schemas/`: canonical recording and native-save schemas shared by C++ and Python.
- `parity/validate_main.cpp`: entry point for the CMake `parity_validate` target,
  which validates run-parity snapshots, actions, and reference-state recordings.
- `oracle/reference_runtime.py`: shared build, protocol, and validation helpers for interactive recording and live audits.
- `oracle/replay.py`: verifies canonical recordings against an independently initialized reference backend.
- `oracle/check_kernel_rng.py`: captures pinned-game RNG, UTF-8 hash and equal-key sort vectors.
- `oracle/check_native_choice_callbacks.py`: runs real packed Godot hand selection callbacks and a native Purity command/effect continuation; see `oracle/native_choice_probe/README.md` for prerequisites and limits.
- `oracle/check_kernel_choices.py`: captures native Survivor continuation cases for kernel mechanics checks.
- `parity/replay_main.cpp`: `sts2_replay`, full-state canonical replay against the new C++ engine.
- `rules_compiler/setup.py`: compiles audited immutable setup definitions into C++ tables; `--check` verifies generated output.
- `oracle/check_setup.py`: captures immutable setup definitions and native map vectors.
- `oracle/check_seeded_setup.py`: captures 30 full starting-run snapshots.
- `oracle/check_run_prefix.py`: records and independently replays Neow/Golden Pearl/map boundaries.
- `oracle/check_combat_corpus.py`: records dense combat victory and block/death cases and independently replays each.
- `oracle/check_consecutive_combats.py`: captures and independently replays declared consecutive native room scenarios, checks every public C++ logical boundary, and replays the preserved scenario fixture offline.
- `parity/run_scenario_main.cpp`: diagnostic scenario transport; player choices use the typed public API, while declared room entry remains separate from player actions.
- `oracle/check_run_combat.py`: records a naturally entered Shrinker fight, explicit reward collection/skip/re-entry, potion discard and map return, plus natural run death; independently replays both.
- `oracle/check_clock_inputs.py`: captures and replays zero, non-unit and maximum timestamp clock inputs.
- `oracle/check_recording_replay.py`: regenerates and twice replays the initial live reference corpus.
- `oracle/play.py`: sts2-cli play interface with validated action/state recording.
- `oracle/profile_play.py`: repeatable timing of real recorder loading, with retained reports and optional cProfile captures.
- `oracle/upstream_play.py`: checked upstream interface hooks and gameplay-command recording transport.
- `oracle/check_state_access.py`: probes access to game internals.
- `oracle/check_event_choices.py`: live regressions for event effects and headless presentation dependencies.
- `oracle/check_shop_removal.py`: verifies cancellation, one successful removal per shop, hidden purchased removal, duplicate rejection, and availability at a new shop.
- `oracle/check_reattach.py`: verifies Pact's End defeats the final Decimillipede segments and permits reward collection in the headless oracle.
- `oracle/check_egg_ai.py`: verifies live Tough Egg AI aliases remain recorded, obsolete aliases are omitted after removal, and combat continues.
- `oracle/check_test_subject.py`: verifies empty-target player turns, both Test Subject revivals, third-phase continuation, and completed empty combat rewards in the real engine.
- `oracle/check_play.py`: checks full-state equivalence of seeded gameplay with native display enabled and disabled.
- `oracle/check_ancient_rewards.py`: verifies acts 2 and 3 require entering the Ancient, grant its reward, and then unlock the first fights.
- `oracle/check_combat_export.py`: validates shared combat export and fail-closed model storage against the pinned game.
- `oracle/combat-field-contract.json`: checked classifications for concrete model storage.
- `oracle/check_combat_fields.py`: audits combat fields in the reference game.
- `oracle/check_native_run.py`: checks native save serialization and round trips.
- `oracle/native_run.py`: shared exact JSON comparison and native reference-build utilities.
- `oracle/native_run_fixtures.py`: synthetic inputs for native serialization checks.
- `oracle/adapters/`: C# adapters injected into disposable reference builds.
- `oracle/requirements-test.txt`: Python dependencies for schema validation.

Reference-tool Python regression tests live in `../tests/oracle/`; shared run-state schema checks live in `../tests/parity/test_run_parity_schema.py`. See `../docs/specs/` for
reference-game prerequisites, supported profiles, and tool commands.

## Recorder speed profiles

Run the real `play.py` load path repeatedly, stopping automatically before its
first input prompt:

```sh
python3 tools/oracle/profile_play.py \
  --game-dir '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2' \
  --load build/recordings/my-run2-egg-ai-fixed/saves/Ironclad_1234_egg-ai-fixed.json \
  --output-dir build/profiles/recorder-baseline \
  --repeat 3 --max-commands 50
```

Choose a new output directory for each experiment. Omit `--max-commands` to load
the entire save. The limit counts saved UI commands, which can produce additional
automatic recorded actions. The original save is never modified. Other recorder
options, including `--validator`, `--checkpoint-every`, `--ascension`, and `--lang`,
are forwarded to `play.py`; its defaults are preserved. Autoplay and export-only
mode are excluded because the measurement ends at the first interactive prompt.

The printed breakdown and retained files include:

- `summary.json`: median time to input, individual successful timings, source-save
  hash, validator path/hash, Python/platform, Git revision, and profiled source hashes.
- `run-NNN/profile.json`: startup, replay, and shutdown/export timings; nested
  inclusive and exclusive category costs; individual saved-command timings;
  recorded-action counts; backend provenance when an export is available; errors.
- `run-NNN/console.log` and `run-NNN/recording/`: interface output, ordinary
  recorder logs, committed journals, and the final compatible JSON export.

Time to input starts on entry to `play.main`; it includes backend copy/build,
initialization, replay, validation, and durable journal commits. It excludes Python
interpreter/import startup and the automatic shutdown/export after the measurement.
The profiler performs the real shutdown/export and reports its time separately.
Failed loads retain diagnostics and are excluded from the successful-run median;
the command stops further repeats and exits nonzero on failure.

Category timings are wall times. Exclusive costs subtract nested categories, so
JSON encoding and `fsync` are not counted again under journal commits. Validator
wait includes pipe transfer, C++ JSON parsing, and validation; it does not split
those C++ internals. The unattributed remainder includes UI rendering, Python
bookkeeping, and profiler overhead. Console output is captured consistently to a
file rather than rendered in a terminal. Each repeat builds a fresh backend;
filesystem and OS caches are not cleared.

Add `--cprofile` to retain `run-NNN/python.prof` for Python function-level detail:

```sh
python3 -m pstats build/profiles/recorder-baseline/run-001/python.prof
```

cProfile adds substantial overhead on recursive JSON encoding. Compare timings
with the same profiling options, save prefix, checkpoint interval, validator,
and storage location. Profiles and recordings are retained until you remove them.
