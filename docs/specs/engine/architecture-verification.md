# Integrated architecture verification

This records #56/#62 architecture evidence against integration commit
`3b422bb975e55eab6dd52259585219c26d69a784` (runtime fixes) merged with
benchmark commit `72886eb` at measured/tested source revision
`04c79645029b570e00bc0878093fdd35ec9307da`. It covers supported gameplay only. No production
content, old content-ticket completion requirement, native fixture replacement or
binary-to-JSON checkpoint migration is introduced. The current binary revisions
are combat 13 and run 11.

## Behavioral evidence

The integrated `engine_test` public-interface tests in `tests/unit/run_test.cpp`
exercise initialization, starting decisions, map/rewards, consecutive scenario
encounters and complete binary checkpoints. Same and alternate cloned/restored
futures compare complete state, including all RNG words and counters. Existing
ordered card-choice and native replay suites preserve their supported comparison
surfaces and original fixture bytes.

| Guarantee | Regression evidence |
| --- | --- |
| Canonical creatures, retained operands and generation-safe reuse | `CreatureOverflowCloneAndRestoreKeepIndependentBranches`, `DeadCreatureRemainsUntilCombatResolutionReleasesItsOperands`, `RecycledGenerationsAndExhaustionRemainSafeAcrossCombats` |
| Permanent/combat card isolation and layered costs | `PermanentUpgradeTargetsOriginAcrossDeckReorderingAndBranches`, `CombatUpgradeDoesNotChangePersistentDeck`, `CostLayerOverflowClonesIndependently` |
| Ordered separate/per-applier powers and retained creature targets | `DistinctAndPerApplierPowersSurviveOverflowCloneAndRestore`, `PerApplierIdentityAndArtifactRemainOnRetainedCreature`, `CapturedCreatureTargetSurvivesMembershipReplacementAcrossNestedPrompt` |
| Captured listeners and nested return order | `NestedShuffleChoiceResumesDrawAndParentExactlyOnceAfterRestore`, `NestedContinuationOverflowKeepsIndependentSameAndAlternativeFutures`, `NestedRelicDrawDefersItsCounterResetUntilTheChildChoiceFinishes` |
| Parent combat/reward return and cleanup | `ParentInteractionReturnsAfterChildCombatAndRewardsExactlyOnce` |
| Transactional restore and resource failure | `OverflowingNestedCloneAndRestoreFailuresKeepDestinationTransactional`, `CorruptionCompatibilityAndMalformedPayloadsAreTransactional`, `CombinedCreatureAndPowerOverflowRestoreReacquiresCanonicalRecords` |
| Pure borrowed traversal/direct validation, including overflow | `DirectActionValidationDoesNotCloneOverflowState`, `BorrowedRunActionsTraverseOverflowWithoutAllocationAndExpireAfterMutation` |
| Hidden information, invalid/restricted/stale actions and operational failure | `HiddenQueuesAndDrawOrderDoNotAffectVisibleChoicesOrIds`, `HiddenEncounterSupportIsNotDisclosedByTravelQueries`, `MalformedUnsupportedAndStaleActionsArePureAndFailuresInvalidate` |
| Worker-local complete state/scratch with shared immutable rules | `WorkersShareOnlyImmutableRulesAndProduceIdenticalCompleteStates`, `ConcurrentOverflowBranchesOwnStateAndScratchIndependently` |

Controlled shuffle listeners, remembered card choices, parent interactions and
extra retained creatures are architecture fixtures. They do not establish native
content parity. The native diagnostic adapter rejects unmapped controlled states;
`ControlledShuffleStateCannotBeExportedAsNativeDiagnosticEvidence` checks that
boundary. Native evidence remains the pinned supported recordings and their exact
revision-2 comparison contract. No live oracle capture was performed.

## Reproducible release measurement

Run from the repository root:

```sh
cmake -S . -B /tmp/sts2-architecture-release \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DBUILD_TESTING=OFF -DSTS2_BUILD_JSON_IO=OFF \
  -DSTS2_BUILD_PARITY_TOOLS=OFF -DSTS2_BUILD_BENCHMARKS=ON
cmake --build /tmp/sts2-architecture-release -j4
/tmp/sts2-architecture-release/architecture_benchmark 10000
/tmp/sts2-architecture-release/kernel_benchmark 10000
```

Measured 2026-10-29 using GCC 11.4.0, CMake Release defaults (`-O3 -DNDEBUG`),
x86-64 Linux 5.15/WSL2 on AMD Ryzen 7 3700X. Other integration tests ran
concurrently; these are one-run descriptive measurements, not an isolated CPU
comparison, performance gate, percentile estimate or capacity recommendation.

Each workload owns its `RunState` and `Scratch`; rules are shared immutable data.
The supported workload initializes seeded combat with default configuration:
seed `ORACLECHECK01`, `CHARACTER.IRONCLAD`, ascension 0, pinned build
`0.111.0+41cef1ea`, rules revision 1. The pinned assembly SHA-256 is
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`
(`game_digest` in `include/sts2/rules/ruleset.hpp`). Rules source/provenance is
fixed by measured source revision `04c79645029b570e00bc0878093fdd35ec9307da`; the generated definition tables are
unchanged.
Creature overflow adds twelve controlled retained creatures. The 200-card workload
uses supported Strike/Defend definitions in a controlled large deck. Nested overflow uses
Burning Pact plus seven controlled shuffle listeners and suspends at the nested
choice. Controlled power overflow adds nine separate Strength instances,
two distinct per-applier instances (player and enemy appliers), and one default
by-type instance to the player: twelve ordered instances in total. No production
power policy or new content is introduced by this fixture. Initial setup is outside measurement. No observation text, JSON or codec
work is timed. The existing combat `kernel_benchmark` remains unchanged.

`query_validate` is one complete indexed action traversal plus validation of every
offered input (including restrictions), rather than one indexed lookup. `clone`
is complete owning `clone_into`, including destination overflow replacement.
`step` is one public `apply_action`: end turn from the seeded/large-deck/power opening or selection
of the first nested-choice candidate. Each iteration resets from its baseline
outside step timing/counting. It measures repeated representative transitions,
not complete-run throughput or naturally generated unsupported encounters.

Global ordinary, array and aligned C++ allocation operators count allocations in
each measured region, including engine default overflow allocation. The counter
does not interpose arbitrary C `malloc` calls; engine storage uses C++ allocation.
Retained bytes are `sizeof(RunState)` plus both owners' retained bytes, excluding
allocator bookkeeping, stack scratch, code and shared immutable rules.

| Workload | Root bytes | Retained bytes | Query ns / allocations total | Clone ns / allocations total | Step ns / allocations total | Steps/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Supported seeded combat | 33,472 | 33,472 | 56,207 / 0 | 511 / 0 | 1,007 / 0 | 993,527 |
| Controlled creature overflow | 33,472 | 38,112 | 57,901 / 0 | 715 / 30,000 | 1,716 / 0 | 582,705 |
| Supported cards, controlled 200-card deck | 33,472 | 60,864 | 1,622,030 / 0 | 1,832 / 120,000 | 2,394 / 0 | 417,631 |
| Controlled nested overflow | 33,472 | 35,456 | 25,845 / 0 | 883 / 60,000 | 5,170 / 0 | 193,415 |
| Controlled twelve-power overflow | 33,472 | 34,000 | 64,543 / 0 | 589 / 20,000 | 1,600 / 10,000 | 625,179 |

Every row uses 10,000 iterations per operation; checksum was 490,000.
`sizeof(RunData)` is 33,376, `CombatData` 4,000, `CardInstance` 36,
`EffectFrame` 56, `CreatureState` 272 and `PowerInstance` 40 bytes on this compiler/ABI.
Common cloning uses the inline complete-record memcpy with no allocation. Overflow
cloning independently owns its retained buffers and has an explicit allocation
cost. Allocation-free traversal does not imply constant-time validation: complete
state validity checks remain observable in query timing. The twelve-power end turn
allocates once per transition when Shrinker's move adds
another power beyond the independently cloned overflow buffer's retained capacity.
The common, creature, large-deck and nested measured steps allocate nothing; this
is not a claim that every exceptional transition is allocation-free. Root size and query cost
are observations for future tuning; this report selects no alternative layout.

The unchanged isolated combat benchmark completed 10,000 battles / 130,000
transitions in 0.219658 seconds (591,828 transitions/s), including clone reset,
with a 5,160-byte root/retained state, zero warmed allocations and checksum 950,000.
It is a separate standalone-combat workload, not the public complete-run root.

Logs: `/tmp/sts2-architecture-release-{configure,build,results}.log` and
`/tmp/sts2-architecture-kernel-results.log`.

## Deferred choices and scope

Dense/sparse powers, exceptional-payload placement and inline capacities, dispatch
specialization and partial-run cloning remain deferred. Representative larger
content distributions, cache behavior, multithread throughput and comparison to
`sts2-cli` have not been measured here. No unmeasured winner is selected.

The earlier full-game file/class sketch is historical design guidance wherever it
conflicts with ADR 0006 or the implemented ownership/execution contract.
`implementation-status.md`, `inline-storage.md` and the actual public APIs describe
the supported implementation. Full-game content and victory acceptance remain
separate; architecture completion does not close or rewrite their tickets.

## Verification execution

Both final Release and Debug builds passed with no compiler warnings in their
captured logs. The final registered suite and benchmark results refer to the
measured/tested revision above.

`ctest --test-dir /tmp/sts2-final-debug -j4 --output-on-failure` passed all
243 registered tests in 166.57 seconds, including 155 engine checks and 23
replay-parity checks. Complete output is retained in
`/tmp/sts2-final-debug-tests.log`; build logs are
`/tmp/sts2-final-debug-{configure,build}.log`. The Release architecture benchmark
completed all five workloads at 10,000 iterations, and the unchanged kernel
benchmark completed 10,000 battles, both with exit status zero.

The preliminary full-suite attempt exposed a worktree setup issue: the pinned
`sts2-cli` submodule had not been populated. It was restored from the existing local
pinned checkout without changing fixture bytes before final verification.

Initial standards review reported no findings. Spec review identified power
identity and captured-creature-target gaps, fixed in `13f0a38`. Final correctness
review identified the combined creature/power overflow restore lifetime bug, fixed
in `3b422bb` with a deterministic poisoned-retired-buffer regression demonstrated
failing before the fix and passing afterward. Reviewers confirmed no remaining
blockers in their focused rechecks.
