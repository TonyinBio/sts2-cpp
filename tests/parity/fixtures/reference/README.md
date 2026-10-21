# Canonical reference corpus

Every pair retains backend provenance, an SHA-256 digest of the exact action bytes
and an explicit `action_clock_v1` input. Reference self-replay initializes a fresh
process from setup and executes the canonical actions, including recorded automatic
transitions, exactly once. Every saved state field is compared.

| Pair | Coverage | Actions / checkpoints | C++ full-state replay |
| --- | --- | --- | --- |
| combat-victory | Sparse starter Shrinker fight through combat victory | 9 / 4 | Passed |
| combat-victory-dense | Every action, Shrink/Vulnerable, native reshuffle identities, damage and Burning Blood | 9 / 10 | Passed |
| combat-block-death | Block, repeated draws/reshuffles, natural death, retained history and detached powers | 12 / 13 | Passed |
| neow-map | Seeded Neow offers, Golden Pearl and proceed to map | 2 / 3 | Passed |
| clock-zero | Epoch zero and zero action step, two combat turns | 2 / 3 | Passed |
| clock-step | Non-default epoch and seven-second action step | 2 / 3 | Passed |
| clock-limit | Maximum native UTC timestamp and zero action step | 2 / 3 | Passed |
| run-shrinker-victory | Native map travel, combat victory, explicit rewards, both card-skip paths, card acquisition, potion discard and map return | 23 / 24 | Passed |
| run-shrinker-death | Native Neow/map travel through natural Shrinker run death | 12 / 13 | Passed |
| run-death | Neow, recorded automatic proceed, seeded map travel into Slimes and natural player death | 12 / 5 | Unsupported encounter; reference self-replay only |

`combat-victory*` are isolated seeded combats, not naturally reached run victories;
they do not cover rewards. `run-death` reaches combat through actual map travel,
without HP overrides or synthetic room placement. Reference self-replay passes for
all pairs. Each supported simulator replay compares the entire schema; no expected
fields are copied into initialization/export or excluded from comparison.

Additional native inputs outside this canonical corpus include 30 full seeded initial
states in `tests/fixtures/seeded-setup-v1.json`, 36 exact map cases, native RNG/sort
vectors and three isolated Survivor scenarios. All seven Survivor states compare
fully, including suspension, binary restoration and explicit discard. The scenario
metadata explicitly specifies the modified deck and enemy HP; those fixtures are
not claims of a naturally reached choice or complete-run support.

Regenerate into new directories:

```sh
python3 tools/oracle/check_recording_replay.py --game-dir '/path/to/game' \
  --output-dir build/reference-corpus
python3 tools/oracle/check_combat_corpus.py --game-dir '/path/to/game' \
  --output-dir build/dense-combat-corpus
python3 tools/oracle/check_clock_inputs.py --game-dir '/path/to/game' \
  --output-dir build/clock-corpus
python3 tools/oracle/check_run_combat.py --game-dir '/path/to/game' \
  --output-dir build/run-combat-corpus
python3 tools/oracle/check_run_prefix.py --game-dir '/path/to/game' \
  --logs build/prefix-logs --output-dir build/neow-map-corpus
```

Use `tools/oracle/replay.py` with `--actions`, `--states`, `--game-dir` and `--logs`
to verify a pair against a freshly built reference. Source/schema changes that alter
backend identity invalidate provenance until audited recapture. Dense combat/clock
capture tools also save `reference.json` directly from the running backend. All ten pairs have been recaptured and self-replayed against the corrected current
backend, with its independently confirmed runtime identity.

Run C++ comparison with `sts2_replay --actions ... --states ... --reference ...`.
The reference argument is independent metadata, not read from the expected-state
file. CTest registers each supported pair under the `engine` and `replay_parity` labels.

Remaining corpus targets: a combat choice during turn processing, branching events,
shops, rest sites, potions, further room transitions and a natural complete-run
victory. Transport limitations remain automatic reward collection and treasure
index 0, unexposed reward alternatives and potion discarding. Exporter callbacks,
custom AI storage and runtime-shape guards remain explicit failures. Each newly
supported shape or presentation compatibility fix needs a gameplay regression;
schema validation alone does not establish faithful reference execution.
