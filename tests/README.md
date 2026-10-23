# Tests

- `unit/`: C++ simulator and RNG unit tests.
- `parity/`: run-parity snapshot, action-recording, and reference-state tests.
  `examples/` and `fixtures/` contain test inputs and reference recordings.
  Shared implementation lives in `../tools/parity/src/`, public headers in
  `../tools/parity/include/sts2/parity/`, and canonical schemas in `../data/schemas/`.
- `oracle/`: offline Python tests for the reference recorder, profiler, and native saves. These import utilities from `tools/oracle/`.
- `native/fixtures/`: golden native serialization data and its provenance.

Run the configured C++ and Python tests with:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Install `tools/oracle/requirements-test.txt` for Python schema validation.
The run-parity schema checks are registered with CTest and can also run with
`python3 tests/parity/test_run_parity_schema.py -v`.

Live reference-game checks are tools, documented in `../tools/README.md`.
