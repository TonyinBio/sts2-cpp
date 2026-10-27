# Warm structured public execution

Issue [#50](https://github.com/TonyinBio/sts2-cpp/issues/50) measures the supported
consecutive Ironclad/Shrinker scenario. It does not certify an Ironclad victory,
natural travel into a second encounter, a twofold improvement, English rendering,
or another character. The benchmark adapters are diagnostic transports, not a
stable public JSON API.

## Correctness gates

`check_warm_lifecycle.py` independently executes two workloads in fresh native
processes, then repeats A,A,B,A,B,B,A in one process. A is the unchanged historical
`consecutive-shrinker-scenarios` fixture. B uses seed `WARMLOOP50`, independently
resolved actions, and declared native `EnterRoom` calls for both Shrinkers. No
expected snapshot is given to either game engine. Every corresponding full-state
boundary is checked using the existing exact native-state comparator, without
clock, RNG, history, discovery or model-field exclusions.

The reset uses `RunManager.CleanUp`, removes retained singleton listeners,
disposes the actual selector scope, restores a separately captured initial native
profile using the native progress serializer/deserializer, and replaces the host's
`RunSimulator` instance. Immutable model/JIT caches remain warm. Clock setup starts
again at the declared epoch; clock/choice/audio Harmony patches install once.
Pending selections, rewards and unfinished operations reject reset. Singleton
listener counts must remain stable, and selectors must be absent after cleanup.
Reset after terminal death or abandonment and reset with outstanding choices are
not certified by this map-return workload.

The first lifecycle attempt caught profile discovery surviving native cleanup:
the second Golden Pearl acquisition omitted `discovered_relics`. Capturing and
restoring initial native profile state fixed that drift. This resets host profile
state, not a run checkpoint or an expected game-state snapshot.

`check_warm_public.py` then compares every structured public boundary, checks
native query purity with the exact full-state comparator, and checks C++ query
purity with binary checkpoints. It submits an offered but unsupported Neow option
and verifies an explicit unsupported result with unchanged complete checkpoint
and repeated public output. Existing consecutive scenario replay independently
checks clone and binary restore with identical futures; engine tests cover
alternative resumed futures and retained encounter ownership.

Two additional regressions surfaced while removing privileged exports from the
measured path. Inactive-pile previews must retain base damage: native
`CardModel.UpdateDynamicVarPreview` applies global combat hooks only to Hand/Play
or explicit combat upgrade-preview copies. The typed inspector exposes live
cards, not upgrade-preview copies. The C++ projection now preserves that condition.
A completed reward-selector task must also stop counting as a player wait while
its owning continuation finishes; the native adapter now waits for that real
continuation instead of relying on the incidental time taken by a large export.

## Matched structured profile

`warm-public-structured-v1` requests the complete non-English structured surface
exposed by the supported loop's `RunObservation`, `CardView`, `CombatObservationView`
and `actions` at every boundary. Shared build/boundary metadata is represented
once; boundary identity is the record's logical ordinal, including declared
scenario entry. Native automatic `proceed` transitions correspond to automatic
C++ `advance_player` transitions and retain the existing action-clock mapping.

| Surface | Fields and correspondence |
| --- | --- |
| Run | Contract/build validity, character, seed, ascension, act, phase/prompt, HP/max HP, gold, potion capacity |
| Owned objects | Ordered deck, upgrade and energy cost; relic and potion models/slots; object correspondence uses deck positions for these unchanged starter instances |
| Map | Every visible node, kind, children in observable order, visited/current/available flags, and path; node identities map to unique coordinates |
| Prompts | All offered Neow options, all visible rewards and claimed flags, offered reward cards, skip policy, selection bounds and immediate submission metadata |
| Combat | HP/block/energy/turn, enemy HP, Vulnerable, Shrink, announced intent/attack, pile counts |
| Cards | Every hand/draw/discard/exhaust object's identity, model, upgrade, energy cost, damage/block/Vulnerable preview, target requirement and visible pile order; draw inspection follows native rarity/model sorting rather than hidden draw sequence |
| Actions | Full ordered offered pool, game legality/restriction, hand/option/reward/slot/coordinate identity, and target requirement; original implementation capability diagnostics are retained separately |

English names and on-demand descriptions belong to a different requested-output
profile. A prompt/effect outside this benchmark's supported projection stops
explicitly; it is not represented by fabricated empty choices. Current loop
workloads do not request card selection, potion use or card-reward selection.

`Restriction::unsupported_setup` is an implementation capability diagnostic,
not a native game-illegal choice. For currently offered/source-legal options only,
the adapter maps that category to native game legality `none`, while returning the
original restriction in a separate `capabilities` array. Native Neow capabilities
are `[0,0,0]`; C++ capabilities are `[0,9,9]`. No other restriction category is
normalized. The driver consumes, retains and checks those diagnostics separately;
submitted unsupported effects still stop before mutation. No native pool is
filtered, and target/cost/order/eligibility gameplay fields remain compared.

## Measurement boundary

Each engine has one persistent worker on the same machine and inherited CPU
affinity. Inputs are preloaded. The shared Python driver resolves each recorded
intent against the worker's actual last public choices, hand, target requirement
and capability result, then sends it to the real consumer. C++ translates it to
typed `PlayerInput`, validates and calls `apply_player`; native uses the actual
run operation/action consumers.

The timed region begins before fresh run setup and ends after final output and
reset acknowledgement have been decoded and consumed. It includes previous-state
cleanup, new state construction, validation/translation, native continuation
settling, all requested structured projections, allocations/GC, JSON encoding,
pipe transfer, exact JSON parsing, per-record canonical digest consumption and
record/byte counts. Both workers emit compact UTF-8 JSON with unescaped Unicode,
ordered arrays, no indentation and unordered object keys. Native decimals and
C++ integer tokens are compared by exact numeric value. Diagnostic `[SIM]` logs
are disabled in the benchmark; stderr remains drained and retained symmetrically.
Reset has the same logical record fields on both transports.

Privileged exports, exact comparisons, binary checkpoint/clone work, artifact
file writes, build and process startup are outside the timed region. Their costs
are not subtracted from end-to-end samples. Startup, kernel, snapshot and
checkpoint timings are separate profiles and are not estimated by these samples.

Both workers complete at least five full warmups. The implemented stabilization
rule requires three consecutive five-run block medians within 5% for each engine,
with a cap of 50 warmups; failure is inconclusive. Fifteen measured paired runs
alternate native/C++ order. Every measured output must equal both the other
engine and fresh public output; capability diagnostics must equal their own fresh
baseline. Raw samples retain times, output record/byte counts and consumption
hashes. Uncertainty uses a paired block percentile bootstrap of the ratio of
medians, seed 50, 10,000 resamples and a 95% interval. This interval describes
sampling variation within this machine/session, not cross-machine uncertainty.

## Reproduction

Build the release preset, then run the lifecycle gate and measurement tool with
an installed pinned game and the release validator/worker:

```sh
python3 tools/oracle/check_warm_lifecycle.py \
  --game-dir "$GAME_DIR" \
  --recording-dir tests/parity/fixtures/reference/consecutive-shrinker-scenarios \
  --validator build/release/parity_validate --output-dir /tmp/lifecycle-50
python3 tools/oracle/measure_warm_public.py \
  --game-dir "$GAME_DIR" \
  --recording-dir tests/parity/fixtures/reference/consecutive-shrinker-scenarios \
  --worker build/release/public_benchmark_worker \
  --validator build/release/parity_validate --lifecycle /tmp/lifecycle-50 \
  --output-dir /tmp/measurement-50
```

Output directories must not already exist. `STS2_REFERENCE_SUBMODULE` can point
to a clean checkout of the pinned `sts2-cli` revision when using a worktree; local
schemas and injected sources still come from the executing checkout. The native
builder validates game/submodule hashes and records actual source/configuration,
compiled assembly, SDK/runtime and GC provenance. The measurement report records
C++ release flags, actual worker hash, environment, CPU affinity and load.

## Published run

Final measurements and machine/session limitations are recorded alongside the
raw artifacts in `data/evidence/warm-public-50/`. Implementation, native
verification and interaction verification remain separate evidence levels.

The final capture used a quiet window coordinated with the other active agents:
all other project builds and native probes paused until the samples completed.
The machine was a Ryzen 7 3700X under WSL2, with inherited affinity CPUs 0–15,
one worker per engine, and starting system load averages 1.44/1.55/0.83.
Unrelated host/OS activity was not controlled. GCC release flags were `-O3
-DNDEBUG`; the native wrapper was .NET Release, SDK 9.0.203/runtime 9.0.4,
workstation GC/Interactive latency, and default tiered-compilation environment.
Actual worker/DLL/source hashes and compiler details are in the artifacts.

| Final capture | Native | C++ |
| --- | --- | --- |
| Stabilization | 20 complete warmups | 20 complete warmups |
| Measured paired samples | 15 | 15 |
| Median loop seconds | 0.229242143 | 0.112064564 |
| Minimum–maximum seconds | 0.224405287–0.241755183 | 0.111583158–0.113148984 |
| Records consumed per loop | 29 | 29 |
| Response bytes consumed per loop | 266465 | 266465 |

The descriptive ratio of median times is 2.0456, with paired-bootstrap 95%
interval [2.0233, 2.0747]. This is a result for this supported two-combat scenario
and its shared Python/JSON consumer on one session. It establishes no victory or
whole-run performance threshold. Shared transport/projection work is substantial;
these numbers cannot be substituted for a kernel-only comparison. The interval
does not cover workload, host scheduling, compiler, runtime or machine variation.
The previous exploratory run was excluded from final samples because other
agents could still be building and the final transport policy was not yet fixed.

The final [raw samples](../../../data/evidence/warm-public-50/samples.json),
[lifecycle proof](../../../data/evidence/warm-public-50/lifecycle.json),
[public verification](../../../data/evidence/warm-public-50/public-verification.json),
[compressed per-run measured outputs](../../../data/evidence/warm-public-50/measured-outputs.json.gz),
[warmup outputs](../../../data/evidence/warm-public-50/warmup-outputs.json.gz),
[fresh native B states](../../../data/evidence/warm-public-50/fresh-native-B.json.gz),
[regression counterexamples](../../../data/evidence/warm-public-50/regressions.json),
[source hashes](../../../data/evidence/warm-public-50/source-sha256.json),
[toolchain](../../../data/evidence/warm-public-50/toolchain.txt), and
[artifact hashes](../../../data/evidence/warm-public-50/sha256.json) retain the
reproducible evidence. Native and C++ consumed digests are separately stable across
all final samples; their difference retains the explicitly different Neow
implementation capability diagnostics.

Implementation is complete for this scoped benchmark. Native lifecycle verification
passed 212 exact boundary comparisons and seven audited resets. Interaction
verification passed all 28 full structured boundaries, native/C++ query purity,
source-legal unsupported-choice rejection purity, existing consecutive native
self-replay/C++ replay, and the engine/replay test labels. Broader prompts,
terminal-outcome lifecycle reset and complete victory throughput remain unverified.
