# Kernel reference inputs

`kernel-rng-v1.json` contains values captured by calling the pinned DLL's `Rng`,
`StringHelper` and .NET list sort directly. It exercises mixed raw/bounded/bool/float
calls, equal-range float draw consumption, Fisher-Yates and all final state words /
counters, UTF-8 hashing at block boundaries, and equal-key sort identity ordering.

`kernel-survivor-v1.json` contains full native snapshots for explicit isolated combat
scenarios with zero, one and two possible Survivor discards. Its setup deck and HP
are fixture inputs; full seeded-run equivalence is not claimed. Tests verify the
implemented mechanics at each boundary and clone/restore the suspended continuation.
The full-state parity comparator continues to fail closed for missing simulator fields.

Both files include the independently built reference backend's provenance. Regenerate:

```sh
python3 tools/oracle/check_kernel_rng.py --game-dir '/path/to/game' \
  --logs build/kernel-rng-audit --output tests/fixtures/kernel-rng-v1.json
python3 tools/oracle/check_kernel_choices.py --game-dir '/path/to/game' \
  --logs build/kernel-choice-audit --output tests/fixtures/kernel-survivor-v1.json
```

These inputs never link into the production kernel or supply its runtime rules.
