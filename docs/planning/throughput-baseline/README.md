# Victory throughput baseline and proposed acceptance protocol

Evidence for [Establish an equivalent sts2-cli baseline and 2× benchmark protocol](https://github.com/TonyinBio/sts2-cpp/issues/6).
These are measured facts and protocol options for review by
[Set performance gates and user review rules for correctness tradeoffs](https://github.com/TonyinBio/sts2-cpp/issues/7),
not a speedup certification or an implemented full-run benchmark.

## Existing tools and measurement limits

- `tools/oracle/profile_play.py` measures the real recorder load-to-input path,
  including copying/building the backend, validation, journaling and fsync. Its
  replay phase is useful for recorder diagnosis, not an equivalent simulation gate.
- `benchmarks/kernel_benchmark.cpp` measures isolated Shrinker combat, including
  clone reset, without observation text or serialization. `simulation_benchmark.cpp`
  uses the older demo. Neither is comparable to complete-run reference throughput.
- `tools/oracle/replay.py` validates states and runs an external comparator at saved
  boundaries; that correctness cost must not be charged only to the reference.
- `OracleRunStart` in `RunRecordingSession.cs` explicitly rejects a second run in
  one process. Fresh processes lose JIT/runtime warming; warm filesystem caches
  are insufficient to call them a warm-process benchmark.
- The current adapter exports privileged complete-state JSON at every action,
  rather than the separately required public structured observation interface.
  Its copied backend is built in Debug. A Debug reference is useful for reproducing
  the pinned adapter, but cannot establish a fair optimized acceptance baseline.
- The supplied victory fixture documents the C++ replay stopping at its first
  unsupported choice. No full-run C++ speedup is available.

## Reproduce the diagnostic baseline

From the repo root, with the prerequisites in `tools/README.md`:

```sh
python3 docs/planning/throughput-baseline/measure.py \
  --game-dir '/path/to/pinned/game' \
  --output-dir build/wayfinder/throughput-baseline-new --repeat 3
```

The script preloads the validated dense victory fixture, checks its action digest
and reference identity, builds once, then launches a new backend for each run.
It times setup through receipt and decoding of the final victory response,
including 696 action translations/requests, 697 privileged state exports, pipe
transfer, Python JSON decoding, minimal response checks and response retention.
Launch-to-ready is separate. File loading, backend build, post-run comparisons,
shutdown, recording writes/fsync and schema validation are outside that interval.
JSON protocol logging is disabled; native non-JSON stdout and stderr still drain
to files and their overhead remains. No public display/English renderer or binary
checkpoint encoding is requested. This is intentionally a diagnostic workload.

All 697 parsed state objects are compared with the previously independently
verified dense corpus after each timed run. Both sides use the repository strict JSON parser. This Python value comparison
is a sanity guard, not a replacement for the canonical typed exact comparator
(Python equality still has weaker boolean/numeric type distinctions). Expected snapshots never enter
setup or execution; action identity translation uses each actual returned state.
Raw timings, machine information, runtime/provenance and script hash are retained
in `report.json`. Three samples describe this machine only and cannot support an
acceptance confidence interval. No speedup may be calculated from these times
against an unmatched C++ benchmark.

## Measured result (2026-10-23)

On AMD Ryzen 7 3700X (16 logical CPUs), Linux under Microsoft virtualization,
Python 3.11 and .NET SDK 9.0.203 / runtime 9.0.4, one worker, no explicit affinity
or power policy controls, no cache clearing and uncontrolled background load:

| Sample | Launch to ready (s) | Setup through victory (s) |
| --- | ---: | ---: |
| 1 | 0.154 | 11.542 |
| 2 | 0.179 | 12.657 |
| 3 | 0.143 | 12.898 |

Median setup-through-victory time: **12.657 seconds**. All three runs reached
natural victory and matched all 697 parsed dense states. See `report.json` for
unrounded samples and exact runtime/machine/build identities. No acceptance
confidence interval is asserted. Debug build preparation took the separately
reported duration; it is excluded from these columns. Protocol output volume was
not instrumented, so these samples are insufficient for output-equivalence gates.
The first exploratory attempt reached victory but its post-run comparison mixed
ordinary float parsing with strict decimal parsing; it was rejected and excluded.
The retained three samples use strict parsing on both sides.

## Proposed equivalent warm end-to-end protocol

The following choices require review in the performance-gates decision.

1. Use an optimized, pinned reference adapter and optimized C++ build. Preserve
   game/wrapper/content/runtime/compiler identities and source hashes, including
   dirty sources. Revalidate adapter changes against the dense fixture; never
   rewrite historical fixture provenance. Declare tiered JIT, GC and build options.
2. Add a reference lifecycle harness capable of multiple independent runs in one
   warmed process. Audit singleton, event-handler, clock, RNG, identity and resource
   reset; compare repeated same-seed and interleaved-seed outcomes in fresh and
   reused processes. A reset shortcut must not reuse a precomputed post-setup state.
   Until this exists, report fresh-process measurements separately and leave the
   warm requirement unverified. This is a benchmark prerequisite, not permission
   to claim warmed caches as warmed runtime.
3. Fix an immutable workload manifest: setup, canonical actions and clock, expected
   terminal result, public boundary requests and requested output fields. Resolve
   inputs using actual public observations on both sides. Include every input stop,
   action validation/translation, complete setup, progression and terminal processing.
4. Preload input files and initialize workers before timing. Warm each worker with
   at least five complete runs, continuing until a declared stabilization rule is
   met (for example three consecutive five-run block medians within 5%, capped at
   50 runs; failure to stabilize means inconclusive). Time new-run setup/reset
   through final requested output consumption, retaining all internal allocation,
   GC, observation, transport and serialization work. Do not subtract these costs.
5. Recommend a shared consumer/transport profile: request equivalent player-visible
   structured observations and legal choices every boundary, encode the same
   logical records using the same documented UTF-8 JSON rules, transfer through
   equivalent pipes and decode/consume them using the same driver. This is a
   benchmark adapter, not a planned public JSON API. Include English text when
   requested, with the same fields/frequency on both sides. Report a structured-only
   profile separately from a text-inclusive one; do not certify final text throughput
   with text omitted. Fix logging and backpressure policies symmetrically.
6. Count output records and bytes and retain output digests so omitted work is
   detectable. Declare schema, whitespace, ordering and numeric representation;
   require semantic equality where byte equality is not the contract. Privileged
   snapshots/exact comparisons and binary checkpoints belong to separate diagnostic
   profiles unless explicitly requested symmetrically in the acceptance workload.
   Their correctness acceptance remains mandatory outside timing.
7. Start with one worker. Repeat at each declared production worker count with
   identical worker counts, affinity/resource limits, corpus and driver on both
   sides. Measure aggregate completed runs divided by wall time of the entire
   batch, including slow workers. Report memory/RSS and allocation/GC diagnostics
   separately; no extrapolation from single-worker kernel rates.
8. Recommend at least 30 paired measurement blocks per profile/worker count,
   balanced randomized reference/C++ order, with enough runs per block to exceed
   one second and amortize timer overhead. Publish raw block times and completed
   counts, median and dispersion. Estimate the throughput ratio and a paired
   bootstrap 95% confidence interval over blocks with a recorded seed/method.
   Recommend acceptance only when its lower bound is at least 2.0; noisy or short
   experiments are inconclusive. Do not drop slow samples; disclose failures and
   rerun the declared experiment after fixing them.
9. Record hardware/OS/virtualization, CPU topology, affinity, power policy, RAM,
   background load, runtime and toolchain, date, source hashes and corpus identity.
   Run startup/process-launch/build/input-load timings separately. Warm execution
   results on virtualized shared hardware describe that environment; repeat release
   certification on the declared target machine under controlled load.

Alternative: benchmark typed in-process C++ consumption against an equivalent
in-process .NET reference driver, with matched requested serialization where needed.
This isolates embedding throughput but requires a separately reviewed common
consumer contract. It cannot be silently substituted for the pipe profile, nor can
typed C++ with no encoding be compared to the reference's privileged JSON exporter.

## Corpus expansion and handoff

The Ironclad A0 seed 1234 victory is the initial workload, not a representative
full-target certificate. Expand a versioned, held-out corpus across all five
characters, A0–A10, seeded/standard modes and supported modifiers; cover victories,
deaths, route lengths, large decks, trigger-heavy turns, nested selections,
shops/events/rewards, and text-heavy boundaries. Corpus selection and weights must
be fixed before optimization results are examined. Report per-run and per-stratum
ratios alongside aggregate throughput; the performance-gates decision must settle
whether the 2× gate applies to each stratum or only a declared aggregate.

Outstanding prerequisites: validated reference lifecycle reset, optimized adapter
configuration, independent public observation audit/adapter, actual C++ full victory,
and approved output/transport/corpus/statistical gates. These do not change exact
correctness obligations. Any proposed exception still needs the explicit user review
required by the map. No engine or benchmark lifecycle implementation is supplied.
