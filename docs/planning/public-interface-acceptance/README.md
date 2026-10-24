# Public interface and checkpoint acceptance

> Acceptance update: [the milestone plan](../ironclad-milestones.md) retains partial
> selection, permits equivalent objects to be interchangeable, makes forced event
> pages/continues automatic, and pairs meaningful-choice tooling with implementation.
> Its projection-test policy supersedes earlier blanket screen-capture requirements.
> Source findings remain evidence; no meaningful reward/relic decision is omitted.

Confirmed decisions for [Set public decision and checkpoint acceptance for the full simulator](https://github.com/TonyinBio/sts2-cpp/issues/5), within
[the complete simulator map](https://github.com/TonyinBio/sts2-cpp/issues/1).
These are acceptance requirements and planning decisions, not implemented coverage.

## Confirmed delivery gates

| Obligation | First complete victory milestone | Later/full completion |
| --- | --- | --- |
| Typed C++ interface | Required; run every supported decision through it | Expand to the full pinned single-player scope |
| Player-facing structured observations and actions | Correct visible values, legal choices, targets, prompts, restrictions and ordering; hidden-information protections required | Complete the existing decision contract for all supported mechanics |
| Privileged complete-state replay | Independent comparison evidence alongside public-interface execution | Expand corpus and interaction coverage |
| Clone and simulator-owned binary checkpoint/restore | Required at every supported published input stop, including pending/nested choices and removed continuation sources | Extend with every newly supported domain |
| English text | Available on demand; clearly distinguish uncertified wording/formatting from structured correctness | Exact pinned English names, descriptions, substitutions, prompts, reasons and feedback are a full-completion gate |
| Python bindings | Not required | Planned after the C++ milestone; detailed placement belongs to milestone sequencing |
| Public JSON transport | Not planned | Optional future addition requiring a separate scope decision |

The user explicitly chose C++ first, Python bindings later, and public JSON as an
unplanned possibility. A versioned logical observation/action contract does not
require JSON encoding. Existing privileged JSON recording/replay formats are evidence
interfaces and do not establish a player-facing interface.

Full completion retains the matching contract's exact English requirement. Staging
that certification is not approval to change final behavior for speed. The existing
requirement of at least 2× equivalent warm end-to-end throughput versus `sts2-cli`
remains; the benchmark decision must declare matched requested outputs and transport
costs rather than compare kernel-only C++ timing with reference end-to-end timing.

## Public C++ acceptance

Preserve the normative [decision contract](../../specs/engine/decision-contract.md)
and [complete-state/observation ADR](../../adr/0001-separate-complete-state-from-observations.md).
The public interface must expose the real decision, not a privileged replay shortcut.

- Drive the complete victory path using player-facing actions and observations.
  Resolve recording labels in the adapter against offered public identities; do not
  let expected snapshots or hidden state supply action availability or execution.
- Expose supported run/map/room/combat/player/instance/prompt/action/terminal records,
  with documented contract revision, build/rules identity, stable public IDs, ordering,
  units, restriction meanings and view lifetimes. Missing, hidden, zero and empty differ.
- Preserve the distinction among partial selection, zero selection, confirmation,
  cancellation and skipping. Nested prompts have independent identities. Cancellation
  and payment rules come from the mechanic, not from a universal rollback assumption.
- Hide draw sequence, RNG internals, AI storage, unrevealed assignments/offers and
  execution frames. Public IDs, option ordering and errors must not encode those facts.
  Explicit reveal effects expose only their audited scope and duration.
- Repeated observation, action enumeration, text formatting and pending-input advance
  do not mutate authoritative state, RNG, clock, boundary or prompt identity.
- Reject stale, malformed and illegal actions without state mutation. Keep these
  distinct from native accepted attempts that fail to acquire an item, such as the
  recorded full-inventory potion purchase with no charge and an advancing action clock.
- Keep public English reasons separate from hidden validation diagnostics. Until exact
  text is certified, its safety and structured facts still need correct behavior.

Typed C++ contract revisions and binary checkpoint schema versions are separate.
Do not imply a stable C++ binary ABI, public wire protocol or cross-release migration
promise merely by assigning a semantic revision. Document those separately if needed.

## Checkpoint and clone acceptance

Simulator-owned checkpoints contain complete state, including hidden RNG/history,
identity allocation, nested room stack, partial prompts, continuations, captured values,
ordered trigger positions and references to objects no longer in inventory or active
zones. Scratch and C++ stack locals cannot be the only copy of resumable information.

For every dense supported route boundary, clone and binary-round-trip the state,
compare all owned semantic fields, and resume the same inputs to the same future
states, RNG and public observations. Include independent branching at representative
alternative selections; mutating a child must not mutate its parent or sibling.
Supplement the route for supported prompt shapes it does not exercise: partial/zero
selection, confirm/cancel/skip, nested room return, potion-sourced choices, payment
stages, death/revival and nested hook processing. Checkpoints are required at published
logical input stops, not arbitrary machine instructions inside a running transition.

Retain explicit endianness, lengths, checksums, schema and game/rules/content identity
checks. Restore validates references, ownership, zone membership, prompt eligibility
and frame destinations into temporary storage, then publishes only a fully valid result.
Corruption, truncation, incompatible identities and resource failures preserve the
destination. Compatible checkpoint support is the existing matching-identity policy;
cross-version migrations require an explicit tested migration and are not promised here.

Native-save continuation and complete-state imports remain separately deferred by
the map. That deferral does not weaken local simulator checkpoint completeness.

## Required evidence and remaining audits

The dense complete-state recording is not an independently certified public observation
oracle. Filtering obvious hidden keys out of that recording is insufficient evidence
for visibility, inspection order, disabled choices or native prompt semantics.

The visibility audit must establish source-anchored golden cases for all supported
room/prompt kinds: player-visible fields/order, inspection/reveal behavior and lifetime,
disabled options/reasons, stable identities, targets and selection/confirm/cancel rules.
Paired-state tests should vary hidden facts while holding visible facts fixed and
require identical public projections, IDs and exposed restrictions. Explicit reveal
mechanics need before/during/after cases.

The English audit must establish authoritative pinned localization provenance and
reference rendering cases. The matching contract explicitly warns that submodule
localization is not automatically authoritative for the installed build. Verify names,
dynamic substitutions, number/plural formatting, markup meaning, conditional reasons,
prompts and feedback values at emission time. Structured and exact-English comparisons
remain separate so a passing mechanics test cannot silently certify wording.

The existing implementation is narrower: `observe.hpp/cpp` exposes fixed combat and
simple card descriptions, `Session` JSON exports privileged state, and the codecs only
cover implemented domains. Existing Survivor/reward continuation tests are useful
regressions, not certification of all potion/event/shop/nested-room states. Relevant
sources are `include/sts2/{observation/observe,integration/session,persistence/checkpoint}.hpp`,
`src/observation/observe.cpp`, `src/persistence/{checkpoint,run_checkpoint}.cpp`,
`tests/unit/{engine,run}_test.cpp` and the normative state inventory.

No existing ADR is contradicted and no new glossary term is needed. No engine behavior,
bindings, public JSON schema or acceptance test is implemented by this planning record.
