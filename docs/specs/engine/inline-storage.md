# Inline complete state and owning clones

[Issue #55](https://github.com/TonyinBio/sts2-cpp/issues/55) implements
[ADR 0003](../../adr/0003-inline-state-with-correct-overflow.md) for the currently
supported engine content. `StateRecord`, `SetupRecord`, and `RunRecord` are
trivially copyable. Complete `RunRecord` embeds setup, optional active combat,
progression, maps, clocks, choices, rewards, generation metadata and RNG.
`State`, `SetupState`, and `RunState` own those records and separate overflow.
No completed encounter graph or replacement archive exists.

## Storage and clone contract

`InlineSequence<T, N>` contains trivial elements, a count and an overflow ID.
Each overflow collection has an independent allocation; there is no arena or
copy-on-write. Root owners carry allocation callbacks outside the record.
Borrowed `StateView`/`SetupView`/`RunView` values pair the record with its own
storage; const views expose const data. Moving a record does not move any source
pointer because records contain none. Owned states must be copied with their
copy operations or `clone_into`, not by copying an overflowed record alone.

`clone_into(destination, source)` returns `CloneStatus::cloned` or
`CloneStatus::resource_limit`. It prepares all independent overflow ownership
before publication and copies the entire record with `memcpy`. For a wholly
inline source it allocates nothing, including when replacing an overflowed
reused destination. C++ owning copy construction/assignment use the same path
and throw `std::bad_alloc` on resource failure. A failed explicit clone leaves
the complete destination unchanged. Self-clones are harmless.

Run overflow has setup, active-combat and run-progression ownership domains.
All three are copied before any is published. Natural run victory/death releases the combat graph immediately after folding
its generation high-water value into run metadata. Reward continuations use
run phase and active offers, without a completed graph. Isolated victory resets
all collection records and ownership, retaining scalar outcome and identity
progress. An isolated defeat retains its visible piles and active terminal
observation state; no terminal graph is archived in a natural run. Clone
eligibility never relies on a per-history field list. Adding a new history
requires registering its sequences in the record's storage validator and its
semantic checkpoint codec; an unregistered overflow block fails validation.
All blocks nevertheless participate in owning copies.

Growth preserves order, stable slots, generations and free-slot order. Shrinking
or clearing retains overflow and its capacity; explicit owner `reset()` resets
records and releases collection allocations. Allocation/identity exhaustion is
an operational failure. Existing configured `Limits` remain operational budgets;
inline capacity does not reject content. Referenced detached cards remain live in
the pool even when they have no zone membership.

Initial capacities are estimates: 32 combat cards and zone handles, four execution
frames, 64 combat history entries, 16 play records, eight powers, 32 seed bytes,
32 persistent cards and act-queue entries, 16 relics, four potions, 64 entries per
relic-bag tier and 64 visited coordinates. These are not tuned distributions or
acceptance percentiles; unusually large collections use overflow.

## Public boundaries and persistence

The existing run/combat initialization, observations, action validation/application
and automatic advancement operate on the new owners/views. Public observation
content and hidden-information rules are unchanged. Views and spans borrow the
owner: discard them before owner destruction, move, replacement, clone publication,
reset or a mutation that grows their collection. Materialize an owning `State`
from a combat view when retaining a combat independently.

Checkpoint encoding is semantic little-endian data, never a record memory image.
Isolated combat uses magic `STS2CP04`, version 4; runs retain magic `STS2RUN1` and
use version 3. Older versions are rejected explicitly. Decode builds a temporary
owner, validates complete storage and gameplay relationships, then publishes it.
Encoding and semantic equality ignore inline/overflow mode, capacity, addresses
and padding. Restore does not replay initialization, acquisition, payment or RNG
hooks. Public allocation failures do not publish partially restored ownership.

Combat identity uses explicit `combat_generation`, independent of history length.
Pools track their highest issued/retired generation as semantic metadata,
including after terminal collection cleanup. The next combat starts above that
value, including recycled and retired slots. Exhausted generation space returns a resource error; no
encounter-count limit replaces the deleted archive limit.

## Comparison surface and evidence

The field classification and individual export removals are in
[the retention inventory](inline-state-retention.md). Engine exports declare
`comparison_revision: 2` and validate against `run-parity-v2.schema.json`, which
reuses the original active-gameplay definitions. Historical v1 schemas, fixture
bytes and provenance remain unchanged. V1/v1 comparisons remain exact. Comparing
v1 with v2 explicitly projects the named diagnostic fields, validates the revised
shape and preserves exact wire-decimal tokens; retained fields still compare
exactly. This revises acceptance and does not retroactively certify v2 from older
passing evidence.

Focused tests cover trivial byte relocation, allocation-free public clones,
inline boundaries, independent overflow, growth, shrink retention, reset, reused
destinations, allocation failure in each ownership domain, invalid storage IDs,
free-slot reuse, generation exhaustion, a 200-card run, suspended choices with
referenced detached sources, semantic checkpoint determinism and independent
futures. Existing reward-branch, hidden-state, rejection-purity and consecutive
combat regressions remain the behavioral seams. Full-game content coverage,
previously documented oracle gaps and capacity tuning remain separate work.

## Executed verification

On 2026-10-26, against the existing working tree and unchanged historical fixtures:

- `cmake --build --preset debug -j 4`: exit 0; no compiler warnings in the final build.
- `ctest --preset debug -j 4 --output-on-failure`: exit 0, all 172 tests passed.
  This includes 84 engine checks and 21 tests carrying the replay-parity label,
  including consecutive-room scenarios and dense/sparse decision correspondence.
- A separate `/usr/bin/g++` build with AddressSanitizer and UndefinedBehaviorSanitizer:
  all 50 selected storage, setup, engine, run and checkpoint tests passed.
  LeakSanitizer was disabled because it cannot run under the sandbox's tracing;
  allocator-failure tests independently count and release every owned allocation.
- Both benchmark consumers passed C++23 syntax/warning checks against the new API.
  No throughput percentile, capacity distribution or performance target is claimed.
- Independent standards and spec reviews reported no remaining actionable findings.

Full logs remain in `/tmp/sts2-55-final-full-tests.log`,
`/tmp/sts2-55-final-asan-tests.log` and
`/tmp/sts2-55-benchmark-compile.log`. No new live-oracle session or reference capture
was performed. The replay result establishes revision-2 acceptance against preserved
recordings, not full-game parity or a retroactive rewrite of original evidence.
