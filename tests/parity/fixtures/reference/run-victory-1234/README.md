# Ironclad seed 1234 victory evidence

This is reference evidence for the first complete-run milestone in
[the simulator decision map](https://github.com/TonyinBio/sts2-cpp/issues/1).
Its verification findings belong to
[Establish trustworthy and durable victory-run parity evidence](https://github.com/TonyinBio/sts2-cpp/issues/2).
It does not establish C++ simulator support or general mechanic coverage.

## Source and identity

The source is `build/recordings/victory-run-1234-20261023-151952`, recording ID
`final-boss-resumed-20261023-151952`. It contains 696 actions from seeded setup
through a natural victory: Ironclad, A0, standard mode, seed `1234`, three acts.
The explicit action clock starts at epoch `1700000000`, advancing one second per
accepted action. The final state has `boundary_kind: run_complete` and
`run_ending_reason: victory`.

`original/` preserves the source's canonical action, state and independent reference
files byte for byte, plus its original commit marker and gzip-compressed journals.
The original state file contains an initial snapshot and seven checkpoints at
actions 100, 200, 300, 400, 500, 600 and 696. All 705 committed journal records
passed repository validation; reconstructed journal documents equal the original
canonical export. `manifest.json` records artifact byte counts and SHA-256 hashes.
Gzip preserves the decompressed journal bytes; the original commit offsets apply
to those decompressed bytes.

The game DLL SHA-256 is
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`;
the wrapper revision is `084d1aa3d8e118ca7ce8d8774ad16d6be9c92367`.
Both match the v0.111.0 matching contract. Full runtime and adapter identities are
stored in each pair's independent `reference.json`.

## Provenance and recapture

Strict self-replay of the original pair stopped before gameplay comparison because
its adapter hash differs from the current backend hash. The original provenance
was preserved, not rewritten. `capture.py` independently initializes the current
oracle from the source setup, executes its canonical actions once, compares the
original initial state and every original saved checkpoint, and records every
action boundary under fresh backend provenance. It then independently initializes
a second oracle process and self-replays the new dense recording, comparing every
saved boundary.

`source-hashes.json` identifies the local oracle/schema/build sources and validator used.
The working tree included existing uncommitted tool changes at verification time;
the base repository commit alone does not identify that backend.
`verification-tools.tar.gz` preserves those tool, schema and CMake sources without
game assemblies. For historical reproduction, extract it into an isolated checkout
of base commit `2bc67c3f401600056bd9185ce9740f10d21e6187`, add this fixture, and build
the validator with testing disabled. Avoid extracting it over ongoing local work.
Reproduction requires the pinned local game installation and reference submodule.
Subsequent adapter changes must retain provenance checks and produce an explicitly
new recapture rather than rewriting historical reference metadata.

## Reproduce

Run from the repository root with a built `build/release/parity_validate` and the
oracle prerequisites in `tools/README.md`. Use a fresh output directory:

```sh
cmake --build --preset release --target parity_validate
python3 tests/parity/fixtures/reference/run-victory-1234/capture.py \
  --game-dir '/path/to/game' \
  --output-dir build/wayfinder/victory-recapture
```

The script validates source schemas and action digest, compares source checkpoints,
exports a dense pair with independent provenance, and self-replays it. Any failed
comparison or execution stops the script and leaves diagnostic artifacts. The
independent replay uses a fresh process from the same independently built backend.
No source expected snapshot enters initialization or action execution.

To replay the preserved dense pair, decompress it into a new working directory:

```sh
mkdir -p build/wayfinder/victory-pair
gzip -dc tests/parity/fixtures/reference/run-victory-1234/dense/actions.json.gz > build/wayfinder/victory-pair/actions.json
gzip -dc tests/parity/fixtures/reference/run-victory-1234/dense/states.json.gz > build/wayfinder/victory-pair/states.json
python3 - <<'PYCODE'
import sys
from pathlib import Path
sys.path.insert(0, 'tools/oracle')
from reference_runtime import JsonProcess, ValidationSession, reference_build
from replay import load_pair, replay
validator = Path('build/release/parity_validate').resolve()
logs = Path('build/wayfinder/victory-self-replay').resolve()
logs.mkdir(parents=True, exist_ok=False)
with ValidationSession(validator, logs / 'validation', timeout=600) as validation:
    actions, states = load_pair(Path('build/wayfinder/victory-pair/actions.json'),
                               Path('build/wayfinder/victory-pair/states.json'), validation)
with reference_build(Path('/path/to/game'), logs) as (command, env, provenance):
    host = JsonProcess(command, env, logs / 'replay', 30, log_protocol=False)
    try:
        print(replay(host, actions, states, provenance, validator))
    finally:
        host.close()
PYCODE
```

Strict replay requires the matching backend identity. The independent reference file
is retained alongside the dense pair for eventual simulator replay.

The initial dense capture validated every action/state using the Debug validator,
but whole-document export exceeded the fixed 30-second timeout with both Debug
and Release validators. The complete committed journal was subsequently exported
using the existing Release validation session with a 600-second document deadline;
no gameplay actions were repeated for that recovery and no states or provenance
were rewritten. Use the Release validator for this large recording. The capture
script uses that session for full-document validation and defaults to the Release
validator. This changes only the validation deadline, not validation semantics.

## Verification result

On 2026-10-23, all eight original saved states matched the current oracle.
Independent dense self-replay then passed all 696 actions and 697 complete-state
boundaries under matching current provenance. Dense and original action bytes are
identical. See `verification.json` for identities, digests and validation recovery.

## Evidence limits

This is one character, ascension, seed, route and set of selected options. Even
every-action complete-state agreement does not verify alternative choices,
unexercised content, hidden-information observation rules, exact English text,
clone/restore behavior, or the 2× performance requirement. Every-action snapshots
are input boundaries, not captures of every internal effect or suspension point.

The current C++ replay accepted initialization but stopped at `action-000001`,
`choose_option` index 2, with `unsupported_content` / `unsupported_setup`
(`status=4 restriction=9`, exit 1). This pair is intentionally not registered as a
passing simulator CTest fixture. Expected states remain comparison evidence only.
