# Design principles audit

Reviewed on 2026-10-28 against the working-tree `AGENTS.md`, including its new
Core Performance Directives. This is a planning audit, not an implementation review
or a claim that the engine satisfies these requirements.

## Scope and interpretation

The inventory contains all 55 GitHub issue bodies (#1–#55): 24 open and 31 closed;
all 22 Markdown documents under `docs/specs/`; and all five documents under
`docs/adr/`. Decision comments were additionally checked on #1, #3, #5, #7, #8,
#10, #19, #54 and #55. Other comment threads were not exhaustively audited.
The glossary and repository tracker/domain conventions supplied context.

Local documents include existing uncommitted changes, including ADR 0004. This
audit evaluates that working-tree design, not just committed HEAD. It does not
change source code, canonical contracts, issue bodies, issue status or comments.

The principles require data-oriented records, common-case allocation-free raw
`memcpy` cloning, no virtual methods, little pointer chasing, packed state flags,
and procedural/data-driven rules without unsolicited pattern frameworks.
Exceptional heap overflow remains permitted: the majority/common-case qualification
in AGENTS.md agrees with ADR 0003. Fixed capacities must not silently truncate
gameplay. Native object identities, hook ordering and suspended work describe
required behavior; they do not require C++ object graphs or native class layouts.

## Findings

### 1. The architecture still permits copy-on-write

**Concrete conflict; high priority.**
[Architecture, implementation step 5](../specs/engine/full-game-simulator-architecture.md#7-verification-and-implementation-order)
suggests considering shared map storage and copy-on-write as measured optimizations.
[Specification #54](https://github.com/TonyinBio/sts2-cpp/issues/54) explicitly excludes
copy-on-write. [ADR 0003](../adr/0003-inline-state-with-correct-overflow.md) requires
common-state byte copies and independently copied mutable overflow;
[inline-storage.md](../specs/engine/inline-storage.md#storage-and-clone-contract)
explicitly says there is no copy-on-write.

Remove copy-on-write as an available implementation option. Qualify shared-map
storage as sharing immutable definitions only; generated mutable map state remains
part of each independent complete state. LTO/PGO and measured layout changes remain
compatible. This conflict predates the new principles but directly affects MCTS
branch ownership and copying.

### 2. Proposed subscriptions need a procedural implementation boundary

**Architectural ambiguity; medium priority, not a proven violation.**
[Design §7](../specs/engine/full-game-simulator-design.md#7-transition-and-effect-semantics)
mandates event-indexed subscriptions.
[Architecture §4](../specs/engine/full-game-simulator-architecture.md#4-transitions-effects-and-choices)
proposes a `TriggerDispatcher` with owner/handler/order metadata and rebuildable
subscription indices. These could be compact indexed data, but the wording also
permits an Observer/event-bus implementation contrary to AGENTS.md.

[Issue #19](https://github.com/TonyinBio/sts2-cpp/issues/19) requires captured listener
lists/cursors, mutation-safe dispatch and ordinary/late passes.
[Issue #34](https://github.com/TonyinBio/sts2-cpp/issues/34) calls for generic ordered
inventory hooks. Those semantic requirements are compatible and must remain.
[The confirmed #3 decision](https://github.com/TonyinBio/sts2-cpp/issues/3#issuecomment-6376328498)
explicitly authorizes shared state-owned resumable work with typed system-specific
frames and ordered hooks. It does not require a general event framework.

Clarify that hook dispatch uses flat records, typed IDs, explicit candidate order
and procedural dispatch. Candidate captures and cursors belong in byte-copyable
state, with independent exceptional overflow. Do not infer a need for heap-owned
listeners, virtual callbacks, closures or a general registration/event-bus API.
Whether an event index is needed should follow the implemented mechanics rather
than mandate a framework in advance.

The proposed opcode interpreter is not inherently a violation: compact instruction
records and switch dispatch are data-driven. Function-pointer tables likewise are
not evidence of virtual object polymorphism; AGENTS.md explicitly allows dispatch
tables. Preserve the resumability requirement without making an elaborate generic
VM a prerequisite for feature delivery.

### 3. Packed flags are not consistently carried into future feature contracts

**Requirement propagation gap; medium priority.**
[Design §5](../specs/engine/full-game-simulator-design.md#5-state-storage-and-memory-layout)
specifies compact records and default 32-bit arithmetic but does not specify packed
boolean/status storage. The architecture and
[state inventory](../specs/engine/state-inventory.md) list flags without a storage
requirement. Most open feature tickets inherit the #54 inline/overflow addendum
without repeating its packed-flags requirement. #54 and the closed #55 do mention
packed flags, so this is incomplete propagation rather than universal disagreement.

Add a shared requirement for named masks in `uint64_t` flag words for independent
core-state booleans and status conditions, and reference it from the parent feature
specification. Keep quantities, signed power amounts, durations, identities and
mutually exclusive phase values appropriately typed; they cannot all be replaced
by boolean bits. Preserve semantic checkpoint fields independently of runtime
packing. Native C# `bool` fields and JSON booleans in audit/wire documents describe
their source/encoding, not mandatory C++ member layouts.

### 4. The no-polymorphism wording is narrower than AGENTS.md

**Requirement propagation gap; medium priority.**
The design says to avoid virtual entity hierarchies; the architecture says no
card/entity hierarchy, per-instance vtable, RTTI or per-effect closure is needed.
#54/#55 similarly prohibit virtual entity hierarchies. This does not explicitly
carry AGENTS.md's stronger prohibition on **all virtual methods** into proposed
effect, trigger, room and ownership interfaces.

Replace advisory hierarchy-specific language with the repository-wide prohibition
and explicit procedural/static alternatives. Keep ownership wrappers and short-lived
views where they manage overflow or borrowing; those are compatible with flat
authoritative records and do not imply dynamic polymorphism. No reviewed ticket
explicitly requires virtual methods.

### 5. The greenfield design has stale authority language

**Instruction conflict; medium priority.**
[The design's status paragraph](../specs/engine/full-game-simulator-design.md)
says existing implementation and requirements are not constraints. The architecture
also describes a proposed greenfield system without compatibility commitments.
The design subsequently incorporates ADR 0003, making the broad requirements
disclaimer internally misleading.

[Issue #15](https://github.com/TonyinBio/sts2-cpp/issues/15) and
[the confirmed #3 decision](https://github.com/TonyinBio/sts2-cpp/issues/3#issuecomment-6376328498)
instead require reusing verified foundations, extending the public execution seam,
and proving mechanics in integrated slices. Narrow the disclaimer to illustrative
file/type names and unfrozen layouts. State explicitly that current AGENTS.md,
accepted ADRs and applicable gameplay contracts constrain implementation.

### 6. JSON checkpoint migration has not reached the active acceptance text

**Concrete contract drift; medium priority; separate from the new performance rules.**
[ADR 0004](../adr/0004-use-json-for-restorable-checkpoints.md) selects JSON checkpoints,
explicitly preserving common-state `memcpy` cloning and independent restoration.
The design §11 still mandates compact binary checkpoints; the architecture's file
tree specifies binary encoding and its checkpoint section requires fixed endianness.

These 21 open tickets still contain binary-specific acceptance wording:
#15, #18, #19, #23, #31, #32, #34, #35, #36, #37, #38, #39, #40, #45, #46, #47,
#48, #49, #51, #52 and #53. Most share the same binary save/load checklist item;
#23 also retains it in its consolidated source contracts.

Update future acceptance to semantic checkpoint/restore through the chosen JSON
format, preserving RNG, references, suspended work, independent branches, validation
and atomic rejection. Link ADR 0004 and leave its explicitly unresolved module/format
details unresolved. Do not move JSON into core transitions or use serialization for
in-memory cloning. Keep historical binary test and benchmark results as historical
evidence. Current implementation documents may accurately describe binary persistence
until migration occurs; add a target-versus-current cross-reference instead of
rewriting those results as JSON verification.

## Compatible decisions to preserve

- **ADR 0001:** complete state and public observations remain separate. Compact core
  state does not justify leaking hidden data or omitting future-affecting state.
- **ADR 0002:** durable recorder journals and atomic commit markers belong to I/O
  tooling, outside the simulation hot path. Their filesystem work and allocations
  do not conflict with the common-state allocation rule.
- **ADR 0003:** flat trivial records, explicit cloning and independent exceptional
  overflow implement the new memory principles. Required counters, identities,
  captures and retained sources cannot be discarded merely to reduce state size.
- **ADR 0004:** external JSON persistence can coexist with byte-copyable runtime
  records. Persistence and in-memory cloning have different representations.
- **ADR 0005:** fixed power-type slots are data-oriented and byte-copyable. Its
  explicitly accepted larger footprint qualifies the compactness preference rather
  than violating a hard size limit. Preserve application order and distinct power
  instances. Additional-instance storage is deferred until needed; the later order
  tracking review is already recorded.
- Existing parity, unsupported-content, hidden-information, query-purity and
  independent-future tests remain valid. The principles do not authorize weakened
  gameplay, route shortcuts, filtered generation pools or rewritten evidence.

## Ticket coverage

Numbers below cover every retrieved ticket. “Compatible” means no additional
principle conflict was found in its stated scope; it does not certify implementation.
Closed source tickets consolidated into #23 are historical contracts, not completed
mechanics. Findings 2–4 should be addressed through shared parent requirements rather
than copying another long architecture checklist into every ticket.

| Tickets | Disposition |
| --- | --- |
| #1 | Compatible destination; retain #54 supersession of old encounter/archive requirements. Link current principles and ADR 0004 for future planning. |
| #2–#13 | Closed evidence/decision work. Preserve findings and resolutions; superseded retention/layout choices are historical, not new implementation requirements. Checked relevant decision comments as listed in scope. |
| #14 | Compatible deferred English decision; no reason to change its timing for this audit. |
| #15 | Findings 2–6 apply to its linked architecture, inherited requirements and binary-specific acceptance. Existing typed continuations and procedural mechanic ordering remain valid. |
| #16–#17 | Closed bounded-loop evidence. Preserve binary checkpoints and retained graphs as historical results with existing storage supersession. |
| #18–#19 | Compatible partial choices and ordered-hook semantics; apply shared flag/dispatch constraints and revise future binary wording. |
| #20–#22, #24–#30, #33, #41–#44 | Closed source contracts consolidated into #23. Preserve provenance; fix active copied requirements in #23. |
| #23 | Compatible incremental content scope and staged prerequisites; revise repeated binary acceptance and apply the shared core-state requirements to every new capability. |
| #31–#32, #34–#40 | Compatible inventory/room/lifecycle behavior; revise binary acceptance. #34 also needs the procedural hook clarification. |
| #45–#49 | Compatible named content and progression requirements; revise binary acceptance and inherit current core-state constraints. |
| #50 | Closed matched-throughput evidence; preserve measured work boundaries and limits. |
| #51–#53 | Compatible replay/acceptance/measurement scopes; revise future binary acceptance without rewriting historical evidence. |
| #54 | Strong alignment with DOD, packed flags, common-state memcpy and independent overflow. Its prohibition on copy-on-write should govern finding 1. Open status alone is not evidence of missing implementation. |
| #55 | Closed storage implementation; its completion comment reports memcpy/overflow integration. Preserve its historical verification; it does not establish all future content or JSON migration. |

## Specification coverage

| Document under `docs/specs/` | Disposition |
| --- | --- |
| `README.md` | Evidence-role distinctions are compatible; add links to ADR 0004/0005 and reconciled design requirements for discoverability. |
| `engine/full-game-simulator-design.md` | Findings 2–6. |
| `engine/full-game-simulator-architecture.md` | Findings 1–6. |
| `engine/state-inventory.md` | Compatible semantic inventory; carry the packed-flags and procedural-dispatch representation constraints without dropping required state. |
| `engine/decision-contract.md` | Compatible player-facing semantic fields; wire booleans and ordered selections do not mandate core member types. |
| `engine/matching-contract.md` | Compatible pinned target and complete-state obligations; preserve its ADR 0003 supersession. |
| `engine/inline-storage.md` | Compatible storage/clone contract; binary/version statements describe current implementation, not the ADR 0004 target. |
| `engine/inline-state-retention.md` | Compatible state/export separation and ADR 0005 slots; binary/version statements need current-versus-target labeling. |
| `engine/implementation-status.md` | Preserve scoped implemented/evidence claims; link JSON migration as separate future work. |
| `engine/public-run-loop.md` | Compatible bounded public behavior; owning observation vectors are projected output, not authoritative cloned state. Binary evidence remains historical. |
| `engine/consecutive-run-loop.md` | Compatible historical evidence with explicit supersession of completed graphs. |
| `engine/warm-public-throughput.md` | Compatible I/O-inclusive benchmark and historical binary diagnostics; do not reinterpret these as current JSON evidence. |
| `run-parity-spec.md` | Compatible external semantic comparison and explicit export-surface revision; not the in-memory state layout. |
| `action-recording-spec.md` | Compatible action wire contract; no core allocation/layout requirement. |
| `interactive-recording-spec.md` | Compatible recorder I/O contract under ADR 0002. |
| `oracle-recording-spec.md` | Compatible reference capture/validation requirements; native callback descriptions are evidence, not a C++ callback architecture. |
| `real-game-save-json-spec.md` | Compatible native save contract; not an arbitrary complete simulator checkpoint. |
| `oracle-state-access-spec.md` | Compatible reference-tool access requirements outside core transitions. |
| `combat-runtime-audit.md` | Compatible native semantic evidence; explicitly does not require copying native class layout. |
| `combat-model-field-audit.md` | Compatible source-field inventory; translate native collections/references/bools into indexed inline records and packed flags as appropriate. |
| `card-reward-upgrade-audit.md` | Compatible audited reward/upgrade behavior; no conflicting core layout requirement found. |
| `relic-offer-depletion-audit.md` | Compatible audited generation behavior; preserve pools/order/RNG independently of storage choice. |

## Suggested reconciliation order

1. Correct the architecture's copy-on-write option and greenfield authority wording.
2. Put the shared DOD/no-virtual/packed-flags/procedural-dispatch constraints into the
   design and architecture, then reference them from #15 and #54. Preserve #3's
   approved typed resumability and native hook semantics.
3. Reconcile future checkpoint acceptance in the design, architecture and 21 open
   tickets with ADR 0004; retain explicitly unresolved JSON design details.
4. Add current-versus-target links to status/evidence sheets and update the spec
   index. Preserve closed-ticket history and original reference artifacts.

No code, live-oracle, build or gameplay tests were needed or run for this audit.
The proposed reconciliations above have not been applied by this report.
