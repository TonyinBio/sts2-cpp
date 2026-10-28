# Engine implementation status

The C++23 implementation is in `sts2::engine`. Seeded setup, native maps, the
Neow/Golden Pearl/map prefix and A0 Ironclad/Shrinker combat have full-state
reference checks. A naturally entered combat now continues through explicit rewards
and returns to the map, or reaches a natural run death. Complete runs remain unfinished. The original
`sts2::CombatState` demo and its diagnostic adapter remain separate.

## Engine API organization

`include/sts2/engine/combat.hpp` exposes `initialize_combat`, `validate_combat`,
`apply_combat` and `advance_combat` as free functions. They accept optional rules,
and initialization also accepts limits, with the existing pinned/default behavior.
`include/sts2/engine/run.hpp` exposes `apply_input` for one historical run or combat
transition. `include/sts2/engine/action.hpp` exposes `get_actions`,
`validate_action`, `apply_action` and `advance_to_decision`; action application
additionally advances forced transitions. Observation headers retain projection
and description functions. CMake target
`sts2_engine` replaces `sts2_kernel`; the namespace remains `sts2::engine`.

Persistent inventory, RNG, content queues and reward odds live directly in
`RunData`, accessed through `RunView`. `SetupState` and its record/view are removed.
`CombatData` and `RunData` are byte-copyable; their owning states retain independent
overflow. History/play/power storage, gameplay and diagnostic comparison contracts
are unchanged by this refactor. Checkpoint encoding is unchanged.

## Complete-state inline storage and owning clones

[Issue #55](https://github.com/TonyinBio/sts2-cpp/issues/55) implements byte-copyable
complete run/combat records with separately owned collection overflow. The public
initialization, observation, action, clone and semantic checkpoint paths now use
that representation. Completed encounter graphs and export-only statistics are
removed; generation identity, RNG/queues, inventory, gameplay histories and pending
work remain. See [the implementation contract](inline-storage.md) and
[field/export classification](inline-state-retention.md).

Engine exports now declare comparison revision 2. Original v1 fixtures and schemas
remain historical evidence; v1/v1 comparison stays exact, and crossing to v2 removes
only the enumerated diagnostic surface. Previously passing native comparisons below
do not retroactively certify the revised surface. Checkpoints explicitly reject the
superseded combat v3 and run v2 formats. New tests cover allocation-free common
clones, 200-card overflow, independent futures, pending choices, detached sources,
allocation failures, reused destinations and generation exhaustion. Executed
commands and results are recorded in the
[verification section](inline-storage.md#executed-verification). Older retained-graph
descriptions below remain historical evidence of the former storage/comparison
contract, not the current implementation.

## Implemented and checked

| Area | Behavior | Evidence |
| --- | --- | --- |
| Rules and storage | Immutable indexed programs; trivial complete records, inline collections and independent overflow; explicit generation identity; atomic owning memcpy clones | Allocation-free common clones, stale handles, overflow/relocation, allocation failure, large decks and semantic codecs |
| RNG | Native xoshiro256**, SplitMix64, UTF-8 xxHash64, legacy ASCII seed hash, all 12 run and 3 player streams, bounded draws, shuffles and .NET equal-key sort | Independent game-derived mixed-call vectors and complete RNG word/counter comparison |
| Setup | All five characters, A0..A10; starting inventory; shared/player relic bags; all act encounter/event/ancient/boss queues | Exact bags/queues/UpFront RNG; 30 complete initial native states across five characters, two seeds and A0/A4/A10 |
| Maps | All four standard acts, native paths, room assignments, pruning/repair and edge enumeration order | 36 native cases, every coordinate/kind/ordered edge and final map RNG; full initial-state comparisons also cover saved start-coordinate order |
| Run progression | Neow offers/heal, Golden Pearl, map travel into the generated Shrinker encounter, explicit rewards, return to map and natural death | Complete native action/checkpoint pairs; other Neow choices and encounters return unsupported |
| Rewards | A0 normal-room gold, potion and unique card generation; player RNG/float odds; explicit take/skip/re-entry, supported card acquisition and potion discard | Native reward offer and every intermediate boundary, including Skip and reward alternatives |
| Combat | A0 Shrinker opener and alternating attacks; Strike, Defend, Bash, Survivor and upgrades; Shrink/Vulnerable; energy, damage, block, draws and reshuffles; Burning Blood; combat victory and death | Dense canonical victory and block/death recordings, complete native comparison at every action |
| History and runtime export | Card relationships, plays, power application/duration history, detached powers, AI, per-run statistics and native model fields | Full-state combat replays; death retains native cards/history, victory clears them; lethal Bash still applies Vulnerable |
| Choices | Survivor auto-resolves zero/one candidate and stores a continuation for manual discard | All seven full native states across three isolated scenarios; binary restore and resume inside the effect |
| Clocks | Shared `action_clock_v1` input; advance once before each accepted action; observation/checkpoint calls do not advance | Native self-replay and simulator replay at epoch zero, seven-second steps and the UTC upper bound; all clock fields compared |
| Boundaries | Stale/invalid input is immutable; accepted input creates a boundary; execution failures invalidate; explicit resource and execution budgets | Randomized actions, malformed wire inputs, rejection purity and failure/restore tests |
| Persistence | Combat schema 2 and run schema 1 codecs; maps/queues/inventory/player RNG/reward odds and suspended choices; build/rules/definition identity and checksum | All run-loop boundaries round-trip every owned field and native export; restored reward branches, corruption and malformed metadata checks |
| Observation | Borrowed hand/action views, intent, pile counts and disabled input restrictions; on-demand card descriptions | Inspection purity and hidden-draw tests; exact English parity is not certified |
| Integration | Canonical setup/action wire session; standalone full-state simulator replay with independent provenance | SHA-256 action-byte association, schema validation, sparse/dense checkpoints and full comparator |
| Workers | Independent state/scratch/storage and immutable shared rules | Concurrent workers produce identical complete binary states |

The immutable setup tables are generated from audited **definitions**, not recorded
states. Regenerate with `tools/rules_compiler/setup.py`; `--check` is a CTest check.
Neither reference checkpoints nor their expected clock/RNG/HP values enter simulator
initialization, transitions or export. The combat HP roll consumes the native Niche
stream, independently of the UpFront stream consumed by run generation.

[Coverage annotations](../../../data/builds/v0.111.0/coverage.json) describe each
implemented model's supported context and evidence. The declaration inventory merges
those annotations while retaining all remaining unimplemented candidates. Base-card
reference verification does not certify upgraded cards or arbitrary content interactions.

## Run and verify

```sh
cmake --build --preset debug
./build/debug/sts2_play KERNELRUN14
ctest --preset debug -L engine

./build/debug/sts2_replay \
  --actions tests/parity/fixtures/reference/combat-victory-dense/actions.json \
  --states tests/parity/fixtures/reference/combat-victory-dense/states.json \
  --reference tests/parity/fixtures/reference/combat-victory-dense/reference.json

cmake --build --preset release
./build/release/kernel_benchmark 100000
```

`sts2_play [seed]` accepts displayed action indices and `quit`. The C++ persistence
API checkpoints/restores combat and `RunState`, including pending reward selections.
These are engine formats, not native
saves or comparison snapshots. The integration library provides privileged native
state export and canonical recording identities for replay and scenario tooling.
It is separate from the public hidden-information observation interface.

`sts2_replay` validates canonical action/state files and independently supplied
reference metadata, then initializes from setup only. It compares every saved
checkpoint and executes all actions between sparse checkpoints. Exit 2 means a
state divergence; exit 1 means invalid input or simulator execution/unsupported
content. The Python `tools/oracle/replay.py` separately verifies recordings against
the reference backend, reporting reference execution failures separately from
recording divergence. Simulator replay is built with the test/comparison tooling.

To build the dependency-free kernel and interactive run CLI, configure with
`-DBUILD_TESTING=OFF -DSTS2_BUILD_INTEGRATION=OFF -DSTS2_BUILD_PARITY_TOOLS=OFF`. JSON links into integration
and comparison tooling only; rules/kernel do not use game assemblies, filesystem
access or JSON during transitions.

A local GCC 11 Release sample measured 5.54 million combat transitions/second over
100,000 battles, including clone reset, with zero warmed allocations, a 928-byte
root and 7,928 retained bytes. This includes typed combat history. It excludes setup,
text, JSON and checkpoint encoding. It does not establish complete-run throughput
or speed/memory improvement over `sts2-cli`; rerun after changes.

## Remaining work

The complete-run acceptance requirement is still open. One native map/combat/reward
loop works, including natural death. [Consecutive combat ownership](consecutive-run-loop.md)
now preserves permanent-card links and explicit generation metadata without completed
graphs. The original two-fight recording remains preserved; revision-2 acceptance is
verified separately by the checks above.
Natural subsequent encounters, events beyond Neow/Golden Pearl, shops, rest sites,
potion use, treasure, act continuations
and naturally reached run victory remain unfinished. The older recorded Slimes
run-death scenario verifies the reference backend only; the kernel rejects that
unsupported encounter.

Reward generation uses all native Ironclad reward definitions, but that does not
implement those cards' combat effects. Acquisition is currently admitted for True
Grit, Juggling and Rage; other offers can be skipped. Owned potions can be discarded;
potion use and passive potion hooks remain unsupported. Potion of Binding can remain
in inventory during supported scenario combat with source-backed model metadata;
other active potion metadata is unsupported. Natural travel supports continuation
after a previous combat; unsupported generated encounters and
inventory effects still stop explicitly.

General multi-enemy/entity domains, trigger subscriptions, other character combat
mechanics and remaining content are not implemented. Native complete-state imports,
exact English feedback and public versioned wire observations remain pending. The
run codec covers every implemented field, including map edge free-slot order,
persistent inventory, all RNG streams, exact float bits and nested combat continuations.
Wire-session card-label bindings are integration context; the C++ run codec resumes
with typed handles, not a native save or a recording-label import.

The next integrated slice needs general room-history ownership and additional
encounters, followed by other room categories and a natural victory. Expand canonical
recordings alongside each supported feature. Native-save continuation, graphical
recording and comparison-snapshot restoration remain deferred as requested.

## Reference/interface limitations

The reused recording interface still omits `reward_alternative` and potion discard.
The transport automatically collects rewards and chooses treasure index 0; those
submitted decisions are recorded and canonical replay executes them once.

Exporter guards still reject unsupported callbacks, custom AI storage and runtime
shapes. Schema validation establishes format integrity, not faithful execution by
`sts2-cli`. Presentation compatibility fixes, including cases such as Jungle Maze,
need focused regressions that preserve gameplay effects. The current corpus does
not certify all such fixes or complete-run reliability.

The integrated reward regression found and fixed three reference adapter failures:
first model initialization overwrote its explicit reward selector; Skip detection
used class names instead of `OptionId`; and a valid unresolved reward was treated as
an execution failure. The retained recording verifies an unchanged gold/potion
inventory at offer, visible Skip availability, skipping and re-entering card selection,
explicit collection, potion discard and return to map. Backend provenance changed; all ten canonical pairs were recaptured and self-replayed
against the corrected adapter. Every recording publication now also retains the
independently obtained `reference.json` beside the matched action/state pair.
