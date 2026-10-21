# Tools

- `parity/validate_main.cpp`: entry point for the CMake `parity_validate` target,
  which validates run-parity snapshots, actions, and reference-state recordings.
- `oracle/reference_runtime.py`: shared build, protocol, and validation helpers for interactive recording and live audits.
- `oracle/play.py`: sts2-cli play interface with validated action/state recording.
- `oracle/upstream_play.py`: checked upstream interface hooks and gameplay-command recording transport.
- `oracle/check_state_access.py`: probes access to game internals.
- `oracle/check_event_choices.py`: live regressions for event effects and headless presentation dependencies.
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
