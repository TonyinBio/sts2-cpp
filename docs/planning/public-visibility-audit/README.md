# Player visibility, actions and prompt evidence

Source-backed planning evidence for
[Audit player-visible fields, reveal rules and prompt semantics](https://github.com/TonyinBio/sts2-cpp/issues/11),
within [Map the complete headless STS2 simulator build](https://github.com/TonyinBio/sts2-cpp/issues/1).
This audit supplies factual constraints, source-derived golden-case specifications,
paired-hidden-state cases and concrete evidence-tool gaps. It implements no engine
behavior or public transport and certifies no new native/C++ runtime parity.

## Evidence and scope

| Artifact | Contents |
| --- | --- |
| [combat.md](combat.md) | Passive versus selection pile ordering; duplicate ties; visible powers/intents, targets, failed-play feedback, explicit draw-choice scope and combat golden specifications |
| [prompts.md](prompts.md) | Screen-specific partial/zero/confirm/cancel behavior; native setup/map/reward/shop/event/rest/treasure/transition/terminal matrix and golden specifications |
| [adapter.md](adapter.md) | Actual injected `oracle_display`/action behavior, upstream differences, missing public decisions, possible leaks and inspection limitations |
| [invariants.md](invariants.md) | Cross-cutting native fields, public identity relationships, missing/hidden/zero/empty distinctions, eight paired-state cases and purity experiment |
| `*-sources.json`, [source-index.json](source-index.json), [verify.py](verify.py) | Local source identities and inspected citation bounds; integrity verification, not semantic or runtime certification |

Native sources are the existing local ILSpy 9.1.0.7988 decompilation described in
`docs/specs/combat-runtime-audit.md`, under ignored
`build/combat-state-audit/decompiled/`. The installed assembly was independently
rehashed for this audit and matches the pinned v0.111.0 SHA-256
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.
The wrapper remains `084d1aa3d8e118ca7ce8d8774ad16d6be9c92367`.
This audit did not regenerate the entire decompilation or publish native source or
assemblies. Reproduction needs the pinned local installation and decompiled sources.

The reports inspect shared native consumers for the first interface's supported
screen families and mechanisms exposed by the victory audit. They do not claim
every field of every reachable model, modifier, custom event or future reveal
mechanic is certified. The detailed matrices state known source rules and remaining
coverage gaps. Unsupported visibility remains explicit; neither a broad model scan
nor a dense complete-state corpus proves public observation coverage.

Every proposed golden has native source anchors and expected relationships. **All
runtime captures and public-interface executions are pending.** These are test
specifications, not files of expected rendered native outputs. Complete-state
snapshots may establish preconditions and independent transition comparison, but
filtering them does not create a public oracle. Exact English resources/rendering
remain the separate English-source audit's responsibility.

## Findings that affect the handoff

1. Passive draw inspection, native draw-selection screens and the adapter selector
   have different sorting rules. Same-key visibly modified duplicates have an
   unresolved native tie-output capture case; do not assert arbitrary hidden-draw
   permutation invariance or invent backing-slot public IDs.
2. Confirmation and cancellation are screen-specific. Deck selection preview
   cancellation returns to the selection grid; closing the overall request is a
   separate cancelable operation. Fixed-count grid, hand and upgrade screens cannot
   share guessed submission rules. Some native commands auto-resolve without an
   input stop; an automated test selector wait is not proof of a native user prompt.
3. Hidden powers and hidden intents do not become visible simply because the adapter
   exports their runtime records. Visible counter amount, zero, no amount label and
   no visible object differ. Target UI filters and card/potion model validity differ.
4. Rewards, treasure, final event pages and transitions are incompletely represented
   by `oracle_display`; the convenience transport automatically chooses several of
   them from privileged state. That transport cannot certify the public C++ interface.
5. Native stored rest options must be inspected rather than regenerated. Rendering
   previews, upgrade copies and graphical screens need purity checks; some graphical
   paths draw cosmetic RNG or cancel targeting. A pure query must not recreate those
   UI side effects. The existing display-on/off regression covers only its bounded
   scenario and was not rerun here.
6. Stable public identities are a simulator contract obligation, not native numeric
   UI labels. Definition IDs, current positions and combat-local recording labels
   do not establish them. Test identity relationships and visibility-history
   noninterference separately from native-to-C++ identity correspondence.
7. A visible native failure may be sound/animation rather than an English sentence.
   Preserve native accepted failure, especially full-inventory potion purchase,
   separately from atomic simulator input rejection. Raw exceptions, hidden fields
   and internal restriction names are not public English reasons.

## Evidence work still needed

These are precise gaps for acceptance and implementation specifications, not work
performed or new gates silently chosen by this factual audit.

| Gap | Required evidence capability |
| --- | --- |
| Native public observation oracle | Probe actual native display/inspection consumers, including rendered fields and visibility predicates; export public evidence independently of privileged setup/checkpoints. The evidence format need not be a product JSON API. |
| Intermediate prompt states | Observe native select/deselect, current selected order, preview stage, confirm and both cancellation levels; preserve automatic no-prompt paths. Atomic `ICardSelector` replies cannot supply these expectations. |
| Missing boundary families | Capture reward sets, card alternatives, closed/revealed treasure, final event text, nested event-combat returns, act advance and result acknowledgment; retain reachable source-specific option semantics. |
| Ordering and reveal lifetime | Capture duplicate draw ties, passive versus selection ordering, explicit reveal before/during/after and view sort changes. Match pinned English title resources where sorting uses localized text. |
| Safe fields and reasons | Validate per-model displayed variables/counters, visible disabled options, actual target filters and emitted feedback. Exclude unrendered variables and privileged diagnostic traces. |
| Paired states and purity | Create reachable/native-valid pairs under declared equal-public-history preconditions; compare observations/actions/IDs/restrictions. Compare complete state before/after queries and future execution against a control. |
| English and terminal presentation | Establish authoritative resources, actual substituted text and emitted values; handle native presentation randomness without rerolling on every read. Excluded profiles/platform services stay outside certification. |
| Full-target expansion | Audit additional custom controls, modifier parameters and model-specific visibility/reveals when their content is supported; shared screen rules do not certify arbitrary future content. |

[Define parity acceptance beyond the recorded victory route](https://github.com/TonyinBio/sts2-cpp/issues/4)
owns the evidence acceptance decision, including which independently grounded
cases must pass before each supported scope can be certified.
[Establish authoritative pinned English sources and rendering evidence](https://github.com/TonyinBio/sts2-cpp/issues/12)
owns English provenance and rendering facts.
[Choose implementation milestones and dependency order for complete coverage](https://github.com/TonyinBio/sts2-cpp/issues/8)
owns placement of public probes, interface behavior and scenario coverage in the
implementation plan. Their existing tickets cover these follow-ups; no duplicate
decision or simulator implementation ticket is introduced here.

No correctness exception, ADR change, public JSON transport or new glossary term is
introduced. ADR0001's separation of complete state and observations is preserved.
The source-derived discrepancies constrain future implementation and evidence;
they are not permission to weaken the decision contract.

## Recheck integrity

```sh
python3 docs/planning/public-visibility-audit/verify.py \
  --game-dll '/path/to/pinned/sts2.dll'
```

Verification checks recorded local hashes, citation bounds, wrapper revision and
optional assembly identity, then regenerates the integrity index. It does not run
the game, construct the proposed states or compare native and C++ observations.
