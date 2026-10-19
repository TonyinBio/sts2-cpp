# Tests

- `unit/`: C++ simulator and RNG unit tests.
- `parity/`: combat snapshot, action-recording, and reference-state tests.
  `support/` contains their shared C++ validation and comparison code;
  `schema/`, `examples/`, and `fixtures/` contain the interchange contracts and data.
- `oracle/`: offline Python tests for the reference recorder, native saves, and
  proposed run-parity schema. These import utilities from `tools/oracle/`.
- `native/fixtures/`: golden native serialization data and its provenance.

Run the configured C++ and Python tests with:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Install `tools/oracle/requirements-test.txt` for Python schema validation.
Run the draft schema checks separately with
`python3 tests/oracle/test_run_parity_schema.py -v`.

Live reference-game checks are tools, documented in `../tools/README.md`.
