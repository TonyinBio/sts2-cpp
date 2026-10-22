# Tools

- `parity/validate_main.cpp`: entry point for the CMake `parity_validate` target,
  which validates run-parity snapshots, actions, and reference-state recordings.
- `oracle/reference_runtime.py`: shared build, protocol, and validation helpers for interactive recording and live audits.
- `oracle/replay.py`: verifies canonical recordings against an independently initialized reference backend.
- `oracle/check_kernel_rng.py`: captures pinned-game RNG, UTF-8 hash and equal-key sort vectors.
- `oracle/check_kernel_choices.py`: captures native Survivor continuation cases for kernel mechanics checks.
- `parity/replay_main.cpp`: `sts2_replay`, full-state canonical replay against the new C++ engine.
- `rules_compiler/setup.py`: compiles audited immutable setup definitions into C++ tables; `--check` verifies generated output.
- `oracle/check_setup.py`: captures immutable setup definitions and native map vectors.
- `oracle/check_seeded_setup.py`: captures 30 full starting-run snapshots.
- `oracle/check_run_prefix.py`: records and independently replays Neow/Golden Pearl/map boundaries.
- `oracle/check_combat_corpus.py`: records dense combat victory and block/death cases and independently replays each.
- `oracle/check_run_combat.py`: records a naturally entered Shrinker fight, explicit reward collection/skip/re-entry, potion discard and map return, plus natural run death; independently replays both.
- `oracle/check_clock_inputs.py`: captures and replays zero, non-unit and maximum timestamp clock inputs.
- `oracle/check_recording_replay.py`: regenerates and twice replays the initial live reference corpus.
- `oracle/play.py`: sts2-cli play interface with validated action/state recording.
- `oracle/upstream_play.py`: checked upstream interface hooks and gameplay-command recording transport.
- `oracle/check_state_access.py`: probes access to game internals.
- `oracle/check_event_choices.py`: live regressions for event effects and headless presentation dependencies.
- `oracle/check_shop_removal.py`: verifies cancellation, one successful removal per shop, hidden purchased removal, duplicate rejection, and availability at a new shop.
- `oracle/check_play.py`: checks full-state equivalence of seeded gameplay with native display enabled and disabled.
- `oracle/check_combat_export.py`: validates shared combat export and fail-closed model storage against the pinned game.
- `oracle/combat-field-contract.json`: checked classifications for concrete model storage.
- `oracle/check_combat_fields.py`: audits combat fields in the reference game.
- `oracle/check_native_run.py`: checks native save serialization and round trips.
- `oracle/native_run.py`: shared exact JSON comparison and native reference-build utilities.
- `oracle/native_run_fixtures.py`: synthetic inputs for native serialization checks.
- `oracle/adapters/`: C# adapters injected into disposable reference builds.
- `oracle/requirements-test.txt`: Python dependencies for schema validation.

Python regression tests live in `../tests/oracle/`. See `../docs/specs/` for
reference-game prerequisites, supported profiles, and tool commands.
