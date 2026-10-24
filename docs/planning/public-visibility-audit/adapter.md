# Adapter display and action audit

Source inspection for [Audit player-visible fields, reveal rules and prompt semantics](https://github.com/TonyinBio/sts2-cpp/issues/11).
This records what the adapter implements, not certification that its projection equals
the native player's view. No live oracle run or public golden fixture was produced by
this audit. Proposed cases below remain unexecuted.

## Source identity and reading the anchors

The inspected `sts2-cli` submodule is commit
`084d1aa3d8e118ca7ce8d8774ad16d6be9c92367`.
`RunSimulator.cs` SHA-256 is
`19176bfe655e1b27c79b3c6b9918114018568926d73b60dc8a90abf49085c2ab`.
Local wrapper file hashes at inspection:

| File | SHA-256 |
| --- | --- |
| `tools/oracle/adapters/RecordingDisplay.cs` | `757ce47b0dda810bca8ead0436628ee6f038bd8a25c4f4f920864feb027464c5` |
| `tools/oracle/adapters/RunRecordingSession.cs` | `3a7dfab76677b735ab8255b17f8479cd5a24e585c43ea5a9abdd9a65f7b6cee2` |
| `tools/oracle/reference_runtime.py` | `0fbe190c2f7154cf890c5415ff4eded97f49ae75ecbd9fb7b4eae8a9c8016f07` |

Anchors below name repository paths and methods; line numbers describe this inspected
source. The generated runtime differs from upstream: `reference_runtime.py::inject`
(lines 149–218) installs the run selector, bypasses ordinary decision detection in
recording mode, creates `RecordingEventState` from the formatting-only suffix of
`EventChoiceState`, and changes potion enumeration from `Potions` to `PotionSlots`.
Auditing upstream alone therefore gives incorrect claims about recording mode.

## Display/action matrix

`RecordingDisplay.cs::OracleDisplay` (lines 20–66) chooses bundle, pending card choice,
terminal or room display. It is a presentation helper, not an action-space API.

| Stop/domain | Adapter display and order | Action path | Gap relative to the public contract |
| --- | --- | --- | --- |
| Setup | No `oracle_display` setup prompt; run initialization consumes supplied character, ascension and seed | `RunRecordingSession.cs::OracleRunStart` (151): five names, A0–A10, nonempty seed, then `StartRun` | No setup choices/modifier parameter observation, public constraints or disabled reasons; the supplied configuration is evidence input, not a player-facing prompt |
| Map | `MapSelectState` (upstream 1976) supplies context, choices, act/floor, player; recording replaces choices with `RunMapChoices` (wrapper 552): starting node only before initial visit, then current node's children | `RunRoomAction` (325), `select_map_node`: map context, byte-range coordinates and membership checked | No stable run-scoped node identity; coordinates are positional handles. Full topology/path is a separate `GetFullMap` command (upstream 3835), not in the display; native map visibility still needs independent verification |
| Combat normal input | `CombatPlayState` (upstream 2073): hand backing-list order; alive-enemy backing-list order; current turn/resources, powers, HP/block, previews, draw/discard counts, conditional character fields | `play_card`: `CanPlay`, target resolver and `IsValidTarget`; `end_turn`: combat/play-phase checked | No inspectable pile contents/exhaust count; no stable card/creature IDs; enemy index renumbers after death; disabled hand cards carry boolean `can_play` but `CanPlay` reason outputs are discarded; no enabled action list or exposed end-turn status |
| Potions | `PlayerSummary` (2967): injected `PotionSlots` iteration retains source slot `index` on occupied entries, then filters nulls; name/template/vars/target type | `RunRoomAction` use/discard: occupied slot and `CanUseOrRemovePotions`; use delegates to queued native action | Empty slots and capacity absent; occupied indices may have gaps. No public use/discard availability, disabled reason or eligible-target list; source target type alone does not certify legality |
| Card/object choice | `RecordingDisplay` pending branch: ordered offered cards, min, clamped max, cancelable; generic kind | Wrapper `OracleRunAction` (257): whole index array, uniqueness/range/count checked; `skip_select` permitted only when cancelable | No prompt identity/owner, instruction, partial selection, manual-confirm flag, confirm action or ordering semantics. Submission resolves the entire native test-selector request |
| Card reward subprompt | Ordered candidate cards and `can_skip` | `select_card_reward`, `reward_alternative` by index, `skip_card_reward`; native alternative object is returned | Display omits all non-card alternative identities and ordering, despite accepting alternative actions; no prompt identity or full constraints |
| Reward set | `OracleDisplay` explicitly rejects any active reward frames | Wrapper selects/skip current frame; successfully selected rewards cannot be taken again; `DisallowSkipping` checked | No public reward rows, claim state, skip restriction, costs or offered alternative display. Privileged `ExportRunFlowState` contains rewards but cannot serve as independent public evidence |
| Shop | `ShopState` (2690): character then colorless cards, relics and potions in entry order; prices, stock flags, card sale flag, removal price. Recording changes removal price to null when unstocked | Wrapper checks stock and `EnoughGold`, runs `OnTryPurchaseWrapper`; removal marks used after success | No public entry identities, actionable affordability/restriction reasons, selection/payment stage or explicit exit action. Sold entries remain listed; their exact native appearance needs source/UI evidence |
| Event/ancient | Generated `RecordingEventState`: current option list order; index/title/description/text key/lock flag and event variables; current page description | `choose_option`: unfinished event, current index, not locked | No stable prompt/option identity, lock reasons, displayed continue distinction or partial-selection state. Raw dynamic-variable dictionaries are not proof each value is player-visible |
| Rest | `RestSiteState` (2657) enumerates every option in source order with index, OptionId, CLR type name, enabled | `choose_option`: enabled required | Missing native title/body/disabled reason and service previews. `oracle_display` only enters this branch if some option is enabled; zero-enabled rest stops cannot be displayed |
| Treasure | No `oracle_display` branch: throws settle-required error | Wrapper `pick_treasure` delegates to synchronizer, obtains awarded relics; later proceed | Cannot inspect offered treasures, choose publicly, or certify reveal/claim/continue from display. Upstream `TreasureState` auto-picks; recording uses a separate explicit action path |
| Transitions / combat end / finished event | No general continuation display; finished event is rejected by generated formatter; combat end falls through | `proceed`/`leave_room` resolve rewards, event completion, boss/act transitions and victory via wrapper | Offered continuation identity, final page and exact logical wait are absent. Room type is insufficient to infer continuation semantics |
| Terminal | `GameOverState` (2859): victory boolean, player summary, act/floor/context; loss HP may be corrected from `_lastKnownHp` | Recording rejects gameplay once ending reason set | No result acknowledgment, score breakdown/current-run history or explicit death/abandon distinction in display; `OracleDisplay` maps every non-victory ending to `victory=false` |

## Important differences from the native player interface

The convenience transport must not be used as the C++ public decision oracle.
`tools/oracle/upstream_play.py::RecordingTransport._settle_interface` (125–167) reads
privileged `run_flow`, attempts rewards in order, skips exhausted reward sets,
selects treasure index zero, and submits proceed for combat end, victory, finished
events, consumed treasure and rest sites without enabled options. These decisions
are recorded, but are hidden from that convenience interface's caller. A public
interface regression must expose and drive the corresponding supported inputs.

Likewise `_action` (102–123) resolves hand and enemy positional inputs through
privileged state, and translates `skip_select` to zero-card submission when minimum
is zero and cancellation unavailable. This preserves an important distinction in
the recording but does not certify that public offered identities alone suffice.

`RunSelector` (wrapper 70–128) captures native `CardSelectorPrefs`, including
manual confirmation and cancellation; `ExportRunChoice` (538–550) preserves more
of those facts than `OracleDisplay`. Its test-selector surface supplies one whole
array, so it cannot certify native intermediate selection/confirmation boundaries.
Upstream `HeadlessCardSelector` (3324–3381) is still less representative: it
automatically accepts empty or forced singleton options, filters invalid indices,
and returns an empty result on cancellation. Those upstream shortcuts must not be
mistaken for supported native prompt behavior or recording-mode validation.

## Identity, hidden data and diagnostic exposure

Displayed card `id` is a definition ID; duplicate copies share it. Card/option/shop
indices and alive-enemy indices are positions. They cannot satisfy stable public
instance identity. No evidence in this display establishes identity continuity
across zone changes, disappearance/reappearance or duplicate reorderings.

Most display fields do not directly include RNG or execution frames, but several
paths need explicit allowlists grounded in native UI, rather than blanket copying:

- `RecordingEventState` copies every event DynamicVar BaseValue into every option's
  `vars`, then may merge fresh relic variables. A `RandomCard` variable is specially
  resolved using a permanent-deck index; other variable names retain their values.
  Some can be internal selectors. This is an exposure risk, not a proven leak for
  every event. Audit per rendered variable before accepting it as public.
- `DisplayCard` (10–18) copies every DynamicVar BaseValue; hand display exports every
  preview variable. Restrict to values actually inspectable for that card and mode.
- `RunContext` (3038) reads current act boss definition directly. Native boss reveal
  policy, including alternate boss nodes, must independently justify publication.
- `ErrorWithTrace` (upstream 3823) returns inner exception type, message and stack.
  Wrapper validation exceptions are operational diagnostics, not native visible
  disabled reasons. Do not publish raw traces, source paths or hidden-state-derived
  error distinctions through the player interface.

The privileged `OracleRunAction` advances recording clock before validating its
kind/payload (wrapper 257–265). Consequently this adapter cannot certify the C++
contract's rejection purity; the latter requires no mutation on rejected actions.

## Missing, hidden, zero and empty

The existing display is not a lossless representation of these distinctions:

| Source pattern | Consequence / required regression |
| --- | --- |
| `count ?? 0`, `Energy ?? 0`, HP/maxHP defaults | Missing combat/player storage can appear as real numeric zero; require explicit validity/context in typed observations |
| Empty stats/keywords/powers/intents become null; other fields omitted conditionally | Null/absent do not consistently distinguish empty from hidden or unavailable; specify each typed field separately |
| `star_cost` only if positive; enchantment/affliction amount only if nonzero | Omitted zero is not proof the property is absent; zero-valued visible modifiers need distinct expectations |
| Empty potion slots filtered; empty orb queue omits even capacity | Explicit slot occupancy/capacity and zero-orb case need fixtures |
| Osty absent for other characters, `{alive:false}` for Necrobinder without Osty | Character applicability and dead/missing entity state must remain distinct |
| Removal price null when unavailable | Do not reinterpret null as zero price or fabricate a purchasable service |

## Proposed adapter-gap golden cases and noninterference pairs

These are source-grounded test specifications, not captured native goldens. Each
must obtain an independently native-grounded public expectation before certification.

1. **Duplicate identity:** two identical hand cards, one removed, then the survivor
   leaves/re-enters hand. Keep the survivor's public identity; reject stale identity
   for the removed card. Reorder hidden draw storage and require identical exposed
   IDs before a reveal.
2. **Target eligibility:** two enemies, one dies; preserve survivor identity and
   native display order while target choices update. Pair equal visible entities
   with differing hidden AI history and require identical choices/reasons.
3. **Resource zero:** zero stars, zero-cost star card, zero visible modifier amount,
   empty orb queue with nonzero capacity and an empty potion slot between occupied
   slots. Assert explicit zeros/empty/applicability independently.
4. **Selection semantics:** min=0 noncancelable zero submission; cancelable min>0
   cancellation; manual confirmation with valid/invalid partial selection; nested
   prompt after submitting the first. Confirm distinct IDs and actions. The existing
   atomic selector cannot establish intermediate expected native behavior.
5. **Rewards and treasure:** a skippable reward set, an unskippable reward set,
   non-Skip card alternative, full-inventory potion reward and multiple treasure
   options. No auto-pick/auto-claim; expose offers and native continuation separately.
6. **Visible restrictions:** unaffordable stocked shop entry, sold entry, disabled
   rest option and locked event option. Include only displayed native reasons,
   preserving order and distinguishing absent offers from visible disabled ones.
7. **Event noninterference:** equal current rendered event text/options but changed
   unused event vars, future branch roll or unrevealed reward. Require identical
   values, IDs, ordering, availability and diagnostics. Test `RandomCard` separately
   from pool-index variables; never globally export event variables.
8. **Continuations and endings:** finished event page, combat reward transition,
   next-act starting node, victory, death and abandonment. Native-visible result
   kinds/acknowledgments must not be inferred from a single victory boolean.
9. **Inspection purity:** compare complete state, RNG, clock, identities and pending
   choices before/after repeated display/actions/format/map/pile inspection, then
   resume equal actions. `CombatPlayState` mutates preview values temporarily and
   clears them; inspect whether prior non-base preview values must be restored.
   `GetUpgradedInfo` constructs/upgrades fresh models; verify allocator and hook
   purity rather than assuming clone construction is harmless.

Existing `tools/oracle/check_play.py::check` compares display-enabled and disabled
traces and repeated responses on one seeded Ironclad/Block Potion scenario. It is a
useful bounded regression, not full prompt/character/inspection certification. This
audit read the test; it did not execute it or claim a new passing result.

## Evidence/tool work required

Add a native UI-path observation probe that records rendered fields, inspection
ordering, visibility predicates, targets and option reasons separately from complete
state. Add a selector/scene probe capable of observing partial selections and
confirm/cancel callbacks without replacing them with atomic `ICardSelector` replies.
Add public renderers for reward sets, treasure, final event pages and continuations;
retain unoffered and hidden fields as absent instead of synthesizing them from flow
exports. Capture paired hidden-state cases with declared public-equality preconditions.
These are evidence-tool gaps and proposed work; this document does not implement
tools, a public JSON interface, or a C++ observation API.
