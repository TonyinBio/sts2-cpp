# Instance lifetimes and retained state

Source-backed resolution evidence for
[Determine instance lifetimes and retained history across run and combat](https://github.com/TonyinBio/sts2-cpp/issues/10),
within [Map the complete headless STS2 simulator build](https://github.com/TonyinBio/sts2-cpp/issues/1).
This is a planning audit. It supplies required semantic relationships, lifetime gates
and regression requirements; it implements no engine behavior and establishes no new
oracle or C++ branch-parity result.

## Findings and evidence

| Evidence | What it establishes |
| --- | --- |
| [cards-inventory.md](cards-inventory.md), [cards-inventory.json](cards-inventory.json) | Distinct deck/combat/effect copies, floating original/modified offers, scope registries, removed cards, consumed potions, relic state/source retention and native clone limits |
| [entities.md](entities.md), [entities.json](entities.json) | Created versus active/escaped/dead/reviving entities, removed powers, AI/spawn/death continuations, combat-domain guards and deferred teardown |
| [history-dispatch.md](history-dispatch.md), [history-dispatch.json](history-dispatch.json) | All 17 native combat-history entry types, captured values versus live aliases, six direct scoped history consumers, run-history descriptors, ordered dispatch and type-specific eligibility |
| [rooms-continuations.md](rooms-continuations.md), [rooms-continuations.json](rooms-continuations.json) | Parent/child rooms, pending event/reward/service work, retirement gates, current C++ gaps and architecture clarification |
| [recording-examples.json](recording-examples.json) | Three consumed-potion choice-source examples extracted from the existing 697 stored boundaries; not a fresh execution |
| [source-index.json](source-index.json) | Checked source identities, indexed citation bounds and artifact hashes; an integrity index, not semantic verification |

Native sources are the local ILSpy 9.1.0.7988 decompilation described in
`docs/specs/combat-runtime-audit.md`, associated with pinned v0.111.0 assembly SHA-256
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.
The installed assembly was independently rehashed for this audit and matched. Exact
source hashes and inspected ranges include native sources, explicitly distinguished
adapter sources and current C++ gap evidence. No game assembly or full decompiled
source is published. This session did not regenerate the entire decompilation.

The scope is the confirmed victory models/fixed dependencies and shared native
mechanisms, supplemented by the normative state inventory. A source or consumer
search helps identify references but cannot prove absence of every delegated,
reflective or future-content consumer. Later content needs the same analysis.

## Required lifetime rules

1. **Separate identity from membership and activity.** A permanent deck card remains
   distinct from the combat clone linked by DeckVersion. Moving an existing object
   between piles preserves its identity; copying or transforming creates the native
   distinct relationships. Floating offers have identities before entering a pile.
   Removing a card from a pile does not remove it from the native scope registry or
   necessarily clear Owner. Removed, dead, targetable and eligible-for-hooks differ.
2. **Retain unfinished work and its graph.** Consumed potions, removed/replaced relics,
   detached deck versions, selected offers, source/target entities and attachments
   remain available while continuations, callbacks, histories or fields need them.
   Store captured values, candidate order/cursor, branch/payment/effect stage, nested
   return destination and scope identity explicitly. Scratch or a host-language call
   stack cannot be the sole owner at a published input stop.
3. **Preserve native aliases and snapshots distinctly.** Combat history keeps live
   object relationships alongside captured amounts, turn data and flags. Two entries
   referring to one card must still refer to one card after clone/restore. Run history
   often stores SerializableCard descriptors and model IDs instead: those snapshots
   must not start tracking later card mutation. A model-ID-only log or one immutable
   full-object copy per combat log entry is not equivalent to either graph.
4. **Follow native dispatch eligibility exactly.** Candidate snapshots retain sources
   through the pass, but each yield applies its type-specific predicate. A power
   removed from the owner's current Powers list may still receive its captured
   callback because its Owner/combat association remains valid. Normal/late and
   after-modifier passes may enumerate fresh candidates. Neither "always skip removed"
   nor "always call every captured source" is correct.
5. **Distinguish completion stages.** Zero HP, death prevention, removal prevention,
   same-instance revival/hatching, deferred own-move removal, combat end hooks,
   history clear, player cleanup, rewards, room exit/reset and run cleanup are separate.
   Preserve the native order and captured combat identity; old work must not adopt
   the next encounter. Encounter SpawnedEnemies is a unique canonical-definition
   list, while escaped creatures are instance references; neither is a live-enemy count.
6. **Retire only after semantic consumers end.** No active owner, pending choice,
   continuation, captured dispatch, history/field relationship, parent-room return or
   declared comparison surface may still require the identity or its values. Preserve
   required allocation generations/free-slot order and stale-reference behavior.
   Native GC timing and raw addresses are not requirements; arbitrary early deletion,
   history truncation or fixed capacities are not justified by this one run.

These rules combine the companion reference graphs. They are semantic conditions,
not a prescription for reference counting, an arena layout, a garbage collector or
one C++ object per native object. A more compact representation must preserve all
required behavior and comparison fields, with evidence for any replacement of live
relationships by values. No history-compaction or correctness exception is approved.

## Clone, restore and export obligations

Copy the complete mutable graph with consistent identity remapping. Preserve sharing
inside a clone and independence from the parent and siblings; include removed but
referenced instances, active/pending operations, room stack, dispatch payload/cursors,
RNG, counters, lifecycle stages and identity-allocation metadata. Immutable definitions
may be shared. Native MutableClone/AfterCloned operations often reset owner/state or
links for creating a new object; they are not full-state clone algorithms.

Restore resolves and validates references, owner/domain relationships, attachments,
ordered memberships, removal/eligibility conditions, frame destinations and compatibility
before publishing the restored state. Reject malformed references atomically. Do not
rerun creation, acquisition, payment, initial offer generation or reset hooks merely
to reconstruct data; that can repeat side effects and random draws.

The privileged exporter traverses selected runtime references, uses synthetic IDs,
and deliberately omits some history entry types. Its labels can reset with combat;
absence from a later export does not establish native destruction. Exported pending
sources also do not encode every async local needed to resume. Canonical snapshots
and native saves therefore remain distinct from restorable simulator checkpoints.

The retained-potion examples exist **after** actions 384, 611 and 646 and are selected
by actions 385, 612 and 647. This audit corrects the prior mechanic note's ambiguous
before/after wording. The source survives slot removal; disappearance from the later
retained-inventory export after selection is only a traversal fact.

## Specification corrections and remaining representation work

The architecture's claim that a persistent card moves between deck/combat zones
without changing identity is corrected: Player.PopulateCombatState creates separate
combat copies and DeckVersion links. This follows the existing normative state
inventory and ADR0001; neither is weakened or reversed. Unchanged identity applies
to pile movement of the same object. The architecture also now states that logical
removal/death alone does not authorize destruction. The earlier semantic audit's
generic membership wording is clarified with the removed-power dispatch case.

Existing generation-tagged handles, distinct instance domains and state-owned
continuations remain the design direction. Concrete record layouts, backing regions,
two-phase graph copy/restore machinery and retirement bookkeeping still need detailed
implementation specifications as each supported domain is introduced. Those are
constrained by this graph rather than unresolved permission to omit state. Optional
compaction/reclamation optimizations wait for consumer proofs and measurements;
no new allocator/layout choice is made by this source task.

The current C++ engine has narrow card/history/entity storage, one optional combat
and simple reward state. It does not yet implement the general room/retention graph.
Its supported-subset tests remain useful evidence for that subset, not certification
of these requirements. Keep later implementation slices responsible for their state,
transition, persistence, export and regressions together.

## Acceptance handoff

[Define parity acceptance beyond the recorded victory route](https://github.com/TonyinBio/sts2-cpp/issues/4)
and [Choose implementation milestones and dependency order for complete coverage](https://github.com/TonyinBio/sts2-cpp/issues/8)
already own the decisions this task informs. Use the companion regression lists to
require separate deck/combat identities, detached sources through choices, independent
branches after clone, exact history aliases/snapshots, mutation during captured dispatch,
revival/spawn/deferred removal, event-combat-return, nested/empty/abandoned rewards,
and room/encounter reuse without stale work or identity aliasing. Compare both current
state and future resumed behavior at supported input boundaries.

No new decision ticket duplicates these existing questions. Full-content model-owned
references, later representation questions exposed by those additions and measured
storage optimizations remain broader planning work. Public visibility and English
certification retain their separate open audits. No new domain term or ADR is needed.

## Reproduce integrity and recording extraction

```sh
python3 docs/planning/instance-lifetimes/scan_recording.py
python3 docs/planning/instance-lifetimes/verify.py \
  --game-dll '/path/to/pinned/sts2.dll'
```

The extraction uses ordinary JSON only to locate stored examples. It is not the exact
comparator and never executes the game. Verification checks source hashes and citation
bounds and regenerates the integrity index. These checks do not establish runtime
coverage, exhaustive semantic completeness or exact garbage-collection timing.
