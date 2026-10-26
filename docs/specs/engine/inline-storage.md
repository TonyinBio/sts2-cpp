# Inline storage foundation

This implements the storage mechanism needed by
[issue #55](https://github.com/TonyinBio/sts2-cpp/issues/55), not the complete-state
migration. `State`, `SetupState`, and `RunState` still use their existing storage.
Their public clone/checkpoint paths do not yet use these records. Completed
encounter retention and native export/comparison surfaces are unchanged.

`InlineSequence<T, N>` stores trivial inline elements, a count, and an overflow ID.
Access takes the owning `OverflowStorage`; a record contains no owner address or
pointer into another record. Each overflow collection has its own allocation.
Growth preserves element order and the overflow ID. Shrinking and clearing retain
overflow until the complete owner is explicitly reset. Inline capacity is a storage
choice, not a collection-size limit; integer/allocation exhaustion throws an
operational exception for the public engine boundary to translate.

`InlineState<Record>` separates the byte-copyable record from heap ownership.
`clone_into` copies all owned overflow into temporary ownership before publication,
then uses `memcpy` for the record. A wholly inline source requires no allocations,
including when replacing an overflowed destination. Failed ownership copies leave
the destination record and existing collections intact. New overflow-bearing
histories automatically participate in owner cloning without an eligibility list.

Borrowed spans are invalidated by collection growth, owning clone publication,
reset, or destruction. Callers must use the owner associated with the record and
must not keep an independently copied overflow record beyond its owner's lifetime.
Sequence equality compares elements, ignoring storage mode, capacity and addresses.
Complete-state comparison and semantic checkpoint encoding remain integration work;
the record's memory image must never become a checkpoint format.

The focused tests use capacities of one and two solely to force storage boundaries.
They cover byte relocation, allocation-free inline clones, independent overflow
copies, histories, growth, aliasing append, shrink retention, reset, destination
reuse, failure cleanup, and a 200-element collection. These capacities are not
production estimates, and the tests do not certify gameplay or reference parity.

Remaining work in #55 includes field classification and audited export removals;
combat, setup, and complete-run migration; explicit encounter-generation metadata;
public initialization/observation/action integration; versioned semantic
checkpoints; schema/export/comparison reconciliation; public-boundary continuation
and branch tests; and removal of the obsolete storage and archive paths. Existing
fixtures and historical evidence must remain unchanged through that work.
