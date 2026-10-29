# Integrated architecture verification

This records #56/#62 architecture evidence against integration commit
`4f12be737d0a898aa6a4190ee3add767fd1e6b44` plus the benchmark/documentation
change containing this report. It covers supported gameplay only. No production
content, old content-ticket completion requirement, native fixture replacement or
binary-to-JSON checkpoint migration is introduced. The current binary revisions
remain combat 12 and run 10.

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
| Captured listeners and nested return order | `NestedShuffleChoiceResumesDrawAndParentExactlyOnceAfterRestore`, `NestedContinuationOverflowKeepsIndependentSameAndAlternativeFutures`, `NestedRelicDrawDefersItsCounterResetUntilTheChildChoiceFinishes` |
| Parent combat/reward return and cleanup | `ParentInteractionReturnsAfterChildCombatAndRewardsExactlyOnce` |
| Transactional restore and resource failure | `OverflowingNestedCloneAndRestoreFailuresKeepDestinationTransactional`, `CorruptionCompatibilityAndMalformedPayloadsAreTransactional` |
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
The supported workload initializes seeded combat with default configuration.
Creature overflow adds twelve controlled retained creatures. The 200-card workload
uses supported Strike/Defend definitions in a controlled large deck. Nested overflow uses
Burning Pact plus seven controlled shuffle listeners and suspends at the nested
choice. Initial setup is outside measurement. No observation text, JSON or codec
work is timed. The existing combat `kernel_benchmark` remains unchanged.

`query_validate` is one complete indexed action traversal plus validation of every
offered input (including restrictions), rather than one indexed lookup. `clone`
is complete owning `clone_into`, including destination overflow replacement.
`step` is one public `apply_action`: end turn from the seeded opening or selection
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
| Supported seeded combat | 32,864 | 32,864 | 51,356 / 0 | 540 / 0 | 977 / 0 | 1,023,770 |
| Controlled creature overflow | 32,864 | 36,352 | 52,542 / 0 | 700 / 30,000 | 1,231 / 0 | 812,557 |
| Supported cards, controlled 200-card deck | 32,864 | 60,256 | 1,538,750 / 0 | 1,457 / 120,000 | 1,473 / 0 | 678,859 |
| Controlled nested overflow | 32,864 | 34,784 | 20,348 / 0 | 655 / 60,000 | 3,133 / 0 | 319,207 |

Every row uses 10,000 iterations per operation; checksum was 400,000.
`sizeof(RunData)` is 32,768, `CombatData` 3,968, `CardInstance` 36 and
`EffectFrame` 52 bytes on this compiler/ABI. Common
cloning uses the inline complete-record memcpy with no allocation. Overflow
cloning independently owns its retained buffers and has an explicit allocation
cost. Allocation-free traversal does not imply constant-time validation: complete
state validity checks remain observable in query timing. Root size and query cost
are observations for future tuning; this report selects no alternative layout.

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

The isolated Debug `engine_test` build passed, and the new concurrent overflow
public-run regression passed (`PublicRun.ConcurrentOverflowBranchesOwnStateAndScratchIndependently`).
The initial full suite passed 233/234 registered tests; its sole failure exposed a worktree setup issue: the pinned `sts2-cli`
submodule had not been populated, so Python recorder tests could not read
`sts2-cli/python/play.py`. The submodule was restored from the existing local pinned
checkout without changing fixture bytes; the recorder retry passed 1/1. Thus every
initial registered check passed across the original run and environment retry.
Final integration verification is recorded
after the runtime review fixes; the earlier measurements above remain pinned to
their explicitly named revision until rerun.
