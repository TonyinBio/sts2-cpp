# Tools

- `parity/validate_main.cpp`: entry point for the CMake `parity_validate` target,
  which validates run-parity snapshots, actions, and reference-state recordings.
- `oracle/record.py`: captures reference-game combat recordings with configurable state intervals.
- `oracle/play.py`: interactive starter-combat player and action/state recorder.
- `oracle/check_state_access.py`: probes access to game internals.
- `oracle/check_combat_fields.py`: audits combat fields in the reference game.
- `oracle/check_native_run.py`: checks native save serialization and round trips.
- `oracle/native_run.py`: shared exact JSON comparison and native reference-build utilities.
- `oracle/native_run_fixtures.py`: synthetic inputs for native serialization checks.
- `oracle/adapters/`: C# adapters injected into disposable reference builds.
- `oracle/requirements-test.txt`: Python dependencies for schema validation.

Python regression tests live in `../tests/oracle/`. See `../docs/specs/` for
reference-game prerequisites, supported profiles, and tool commands.
