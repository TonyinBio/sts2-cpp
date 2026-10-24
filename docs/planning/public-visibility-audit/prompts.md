# Native prompts and noncombat visibility

Static source audit for the pinned v0.111.0 build. This is independent of the
complete-state recording, but is **not** a captured UI golden corpus or a passing
public-interface test. The sources are the existing ILSpy 9.1.0.7988 files under
`build/combat-state-audit/decompiled/`; short citations below are relative to that
directory and omit the common `MegaCrit.Sts2.Core.` prefix. Exact paths, SHA-256
hashes and inspected ranges are in [prompts-sources.json](prompts-sources.json).
The game identity is inherited from the established local audit provenance
(`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`);
this sub-audit hashes the source files, and does not freshly decompile or execute
the game. No full native source or assembly is published.

## Selection facts

| Native surface and source | Facts the public contract must preserve |
| --- | --- |
| `CardSelection/CardSelectorPrefs.cs:21–78` | `MinSelect`, `MaxSelect`, `RequireManualConfirmation` and `Cancelable` are separate. The range constructor defaults manual confirmation to `MinSelect >= 0 && MinSelect != MaxSelect`. Prompt substitution receives Amount/MinCount/MaxCount. Optional display comparison applies to simple grids only. Hand-only preview flags separately suppress external hooks and affordability highlighting. These flags do not change actual affordability. |
| `Commands/CardSelectCmd.cs:252–305,319–435,449–535,754–879` | A request need not become a public wait: empty choices can return empty, and several command paths auto-select all available candidates when their count is at most the minimum and manual confirmation is false. Ending-combat checks differ by command. Automated `Selector`/`LocalSelector` branches bypass the UI; their input/count behavior cannot certify native exposed choices. |
| `Nodes.Screens.CardSelection/NSimpleCardSelectScreen.cs:128–186,251–296` | Display input is copied and optionally sorted by the supplied comparison. The selected set is distinct (`HashSet`). Min zero enables confirm initially. Clicking a selected card deselects it; new selections stop at MaxSelect. Without manual confirmation reaching max completes immediately; with it confirm is enabled from min onward. There is no user cancel handler in this screen, despite the generic prefs containing Cancelable. |
| `Nodes.Screens.CardSelection/NCombatPileCardSelectScreen.cs:152–184,253–365` | Similar partial selection, but effective min/max are clamped to the number of displayed eligible cards. Pile changes remove no-longer-valid selected cards; no candidates or all remaining candidates already selected completes the choice. Draw choices are sorted rarity ascending then alphabet ascending; other piles use ascending native order. Do not use backing draw order as option order. |
| `Nodes.Combat/NPlayerHand.cs:956–1049,1159–1274` | Hand selection is a different presentation: eligible holders are shown during selection; peeking restores the other hand holders and ordinary previews. Selecting beyond max deselects the most recently selected card before adding the new one. Selection is an ordered list, not a repeated-card list. Confirm is enabled when count is within min/max, including zero when permitted. A filtered-out selected card is removed; if no eligible cards remain an empty result completes. Do not infer fixed-count auto-submit from the simple-grid implementation: the ordinary hand click handler updates confirmation, and the private `CheckIfSelectionComplete` method is also exposed through Godot dispatch. A runtime case must establish which UI input invokes it. |
| `Nodes.Screens.CardSelection/NDeckCardSelectScreen.cs:193–382` | Deck selection has **two cancellation levels**. Reaching max opens a preview; for variable count the grid confirmation opens that preview. Preview cancel clears selection and returns to the grid. Preview confirm returns selected cards. Closing the grid returns an empty result, and its close button is enabled only if Cancelable. Preview cancellation is available even when closing the overall request is forbidden. |
| `Nodes.Screens.CardSelection/NDeckUpgradeSelectScreen.cs:222–390` | Single selection opens an upgrade preview; multi-selection opens previews when max is reached. Preview cancel clears selection and returns to the grid; overall close returns empty only through its cancelable close affordance. Final confirm requires a nonempty selection and at least MaxSelect, unlike the generic deck minimum check. Do not promise arbitrary min/max combinations are supported identically by all screen types. |
| `Nodes.Screens.CardSelection/NDeckEnchantSelectScreen.cs:255–438` | Enchant screen shows enchantment title, dynamic description, icon and amount on a mutable preview copy. Grid/preview close-versus-cancel is distinct as above. Final confirm requires nonempty selection within min/max. |
| `Nodes.Screens.CardSelection/NDeckTransformSelectScreen.cs:202–345` | Transform grid uses selection and transformation preview stages. Preview cancel clears selection; close returns empty. `ConfirmSelection` may open the preview when manual confirmation is required; the preview completion handler submits. Do not expose random transformation results before the actual preview path reveals them. |
| `Nodes.Screens.CardSelection/NChooseACardSelectionScreen.cs:238–329,342–379`; `Commands/CardSelectCmd.cs:252–305` | Generated-card row is distinct from deck selection. Choose returns one card; skip returns empty only when `canSkip` exposes the skip control. Command rejects more than three options and handles empty options without showing a screen. Automated selectors request 0–1 even when `canSkip` is false, demonstrating why test-hook ranges are not a public visibility oracle. |
| `Nodes.Screens.CardSelection/NChooseABundleSelectionScreen.cs:209–376`; `Nodes.Cards/NCardBundle.cs:144–209`; `Commands/CardSelectCmd.cs:965–1010` | Bundles are offered in supplied sequence, and their cards originate in bundle sequence. Clicking one opens its card preview; preview cancel restores the bundle row and clears the tentative bundle; confirm submits that entire bundle. There is no user skip or whole-request cancellation handler in this screen. Preview cards are laid out with descending x coordinates as their source index increases, so array sequence must not be mislabeled left-to-right preview order. Individual preview cards can be inspected. TestMode chooses the first bundle without this UI, and a negative remote index can return empty; neither establishes a native skip affordance. |
| `Nodes.Screens/NChooseARelicSelection.cs:174–302`; `Commands/RelicSelectCmd.cs:28–53` | Relics are displayed in supplied sequence, select submits one immediately, and Skip is explicitly shown and returns empty. No selection-preview confirmation step exists here. TestMode returns no UI instance. A null command result means the chosen skip path only when grounded in the actual exposed screen, rather than an arbitrary absent reference. |

`HashSet` use is evidence of distinct selection, not proof that selection order has
no downstream effect. The consuming mechanic and synchronization code must establish
its semantic order; the public API must retain the order actually offered/submitted.
Native selection indices and mutable-deck handles are transport mechanisms, not a
safe public stable-ID policy.

## Noncombat visibility and action matrix

| Stop / source | Visible fields and ordering | Input and disabled/absent distinctions |
| --- | --- | --- |
| Standard setup — `Nodes.Screens.CharacterSelect/NCharacterSelectScreen.cs:811–909,987–1045` | Selected unlocked character displays title/description, starting HP/gold and starting relic name/description. Native random selection displays unknown HP/gold and empty relic text. Profile locks display their own placeholders, not zeros. | Standard character-select seed/modifier change handlers throw; `BeginRun` ignores nonempty modifiers. Menu/profile locks are outside the map's simulator scope. The source for explicit seed/modifier editing is the custom-run screen below; do not invent those controls on the standard screen. |
| Explicit seed / modifiers — `Nodes.Screens.CustomRun/NCustomRunScreen.cs:326–353,392–433,495–535,589–605,632–677,764–777`; `Multiplayer.Game.Lobby/StartRunLobby.cs:726–738`; `Helpers/SeedHelper.cs:36–44` | Native custom screen exposes character buttons, seed LineEdit, ascension panel, modifier list and randomize/confirm. Empty input becomes a null seed; nonempty input is retained in lobby until start canonicalizes uppercase, O→0, I→1 and trims. Missing/random seed and an entered string are distinct. | Randomize picks an unlocked character plus two good/one bad modifier using Chaotic RNG; inspection must not rerun it. Embark disables editable controls, marks ready and eventually starts `GameMode.Custom` with actual modifiers. A seeded standard run supplied through the adapter is not proof that the native standard menu has a seed editor. Audit this UI without changing the map's existing accepted game-mode scope. |
| Modifier options — `Nodes.Screens.MainMenu/NCustomRunModifiersList.cs:104–233`; `Nodes.Screens.CustomRun/NRunModifierTickbox.cs:78–131` | List order follows GoodModifiers then BadModifiers. CharacterCards expands to one mutable variant per character in AllCharacters order; displayed title/description use each variant. Checked output follows list order. | Ticking an option unticks mutually exclusive options of other types; the list supports independent same-type parameter variants. Host/single-player edits differ from read-only client/load states. Do not report an incompatible option as permanently unavailable merely because selecting it would untick another choice. |
| Map — `Nodes.Screens.Map/NMapScreen.cs:735–805,874–929`; `Map/MapTravel.cs:14–22` | All current-map points and paths are rendered, with starting point, boss and optional second boss distinct. Visited path gets separate travel state. | Normal travel is from children; the free-travel hook instead offers the entire next row. Final ordinary row unlocks boss and first boss can unlock second boss. A drawn node is not necessarily travelable. These source facts do not justify exposing future encounter/event assignment or other acts' maps. |
| Rewards — `Nodes.Screens/NRewardsScreen.cs:371–461,486–614,641–650` | The RewardsSet sequence becomes visible reward buttons; linked rewards use a separate control. Claimed buttons are removed. Skipped buttons have separate tracking. | `DisallowSkipping` disables the skip/proceed affordance while it is a skip. Empty nonterminal rewards close their overlay; empty terminal rewards expose proceed. Nonterminal means return to the interrupted activity; terminal means last screen of this room, **not run completion**. |
| Card reward — `Nodes.Screens.CardSelection/NCardRewardSelectionScreen.cs:230–390` | Card options render in their supplied order; alternative rewards render separately in their supplied order. Inspection opens the selected card's inspect view without selecting it. | Selecting a card completes with its card index; selecting an alternative completes with an offset alternative index. Alternative/skip/reroll semantics belong to the actual supplied `CardRewardAlternative`, not a universal cancel flag. The completion result's nullable type alone does not certify user cancellation. |
| Shop — `Entities.Merchant/MerchantEntry.cs:23–100`; `Entities.Merchant/MerchantPotionEntry.cs:69–105`; `Nodes.Screens.Shops/NMerchantSlot.cs:347–418` | Current stock and current hook-modified price are distinct from future refill stock. Affordability is derived from displayed cost and gold. | Native purchase can fail because of stock, gold, forbidden procurement or no potion space. Potion procurement precedes charging; failed procurement spends zero and retains stock. This slot-local failure path wiggles/plays sound. The [follow-up English audit](../english-rendering-audit/prompts-feedback.md) establishes that a separate inventory subscription can also show localized merchant dialogue; do not turn internal enum names into purported exact native reasons. Native clickable attempt versus simulator malformed/stale rejection remain separate. |
| Shop removal — `Entities.Merchant/MerchantCardRemovalEntry.cs:19–87`; `Multiplayer.Game/OneOffSynchronizer.cs:87–102`; `Nodes.Screens.Shops/NMerchantCardRemoval.cs:116–172`; `Commands/CardSelectCmd.cs:739–800` | Removal price depends on ascension and prior uses. Removal candidates require IsRemovable; the removal command sorts curses ahead of other deck positions. Used service hides cost and disables its hitbox/focus; it is not a fresh purchasable option with price zero. | Removal invokes cancelable/manual deck selection. Gold loss, removal and removal-count increment happen only after a selected card is returned. Cancel does not spend gold. Used state is per merchant entry. Preview cancel and whole removal close remain distinct. |
| Event/Ancient — `Events/EventOption.cs:17–79,113–139,183–197`; `Nodes.Events/NEventOptionButton.cs:284–341,369–410` | Supplied option title/description are dynamically formatted; locked titles get red styling, retained text and hover information. Ancient relic-associated options expose a relic icon. | A null OnChosen means locked and native release returns without choosing. Some options intentionally retain re-clickability (`DisableOnChosen=false`) for cancellable confirmation. Lethal-option glow is visible; single-player still allows the choice. Do not import the multiplayer death-prevention refusal into single-player legality. Hidden branches absent from the offered list are not visible disabled options. |
| Rest — `Entities.RestSite/RestSiteOption.cs:19–89`; `Entities.RestSite/SmithRestSiteOption.cs:25–76`; `Nodes.Rooms/NRestSiteRoom.cs:460–497`; `Nodes.RestSite/NRestSiteButton.cs:216–284` | Generated option list is rendered in list order, including disabled options. Initial standard options are Heal then Smith; hooks can alter the actual list. Smith has a distinct disabled description when no cards can upgrade. | Disabled rest options remain inspectable. Smith opens cancelable/manual upgrade selection, returning failure without consuming successful rest selection when empty/cancelled; UI re-enables choices on unsuccessful selection. **Generate is documented to mutate RNG/state**; repeated observation must inspect the stored list instead of regenerating it. |
| Treasure — `Nodes.Rooms/NTreasureRoom.cs:205–231,248–339` | Closed chest initially hides the relic collection; opening resolves normal/extra rewards then initializes/reveals relics. Collection-open and collection-finished are separate states. | Chest input disables the chest; after reveal the single-player skip affordance is enabled after an animation delay, then proceed returns after collection ends. Logical boundaries should represent the resulting offered choice, not add a gameplay timer merely to copy animation. Before opening, do not expose the later relic content. |
| Transition / nested return — `Runs/RunManager.cs:1308–1375,1408–1463` | Room stack and current public room distinguish reward return to parent event from opening map. | Proceed from terminal room rewards resumes an event parent when combat's `ShouldResumeParentEventAfterCombat` allows it; otherwise map is opened. Act advance enters next act or Architect event; it is not victory itself. `WinRun` records victory before guaranteed player killing; do not classify the later native HP result as a loss. |
| Run terminal — `Nodes.Screens.GameOverScreen/NGameOverScreen.cs:433–547,647–738` | Score lines are floors, gold, elites if count positive, bosses if count positive, ascension if positive, in that order. Banner/quote and victory damage payload depend on win. | Continue opens a summary; view-run/menu/platform/profile discovery are distinct surrounding UI operations. Gameplay cannot resume from acknowledgment. Loss quotes use Chaotic RNG during screen creation; repeated headless observation must not reconstruct this screen and consume RNG. Global/personal damage and unlock text depend on excluded platform/profile services; do not silently declare them certified in-scope public fields. |

## Presentation paths for all eleven victory event roots

All eleven roots in the recorded scope were checked for layout overrides and choice
consumers. None declares `EventLayoutType.Custom`. The base source maps default,
combat and ancient layouts to `res://scenes/events/default_event_layout.tscn`,
`combat_event_layout.tscn` and `ancient_event_layout.tscn`; only Custom would use
`events/custom/<event-id>` and `ICustomEventNode.Initialize`.
Sources: `Models/EventModel.cs:164–180,332–350,648–661`;
`Models/AncientEventModel.cs:108–108`; `Nodes.Rooms/NEventRoom.cs:190–250`.
The Ancient room initializes dialogue from valid character/visit dialogue using
Chaotic RNG. It must be captured once at event entry, not regenerated by observation.

| Root and source | Actual presentation/input path |
| --- | --- |
| Brain Leech — `Models.Events/BrainLeech.cs:19–73` | Default event options; Share Knowledge opens exact-one simple reward grid from five offers, with no whole-request cancellation; Rip opens a custom reward overlay then returns to event completion. |
| Field of Man-Sized Holes — `Models.Events/FieldOfManSizedHoles.cs:20–66` | Default event; Resist requests deck removal of two before adding a curse, Enter Your Hole uses the deck enchant selector. Enchantment VFX is presentation, not a bespoke event decision screen. |
| Neow — `Models.Events/Neow.cs:18–30,215–314` | Ancient layout. Ordinary generation produces two positive offers then a curse offer; modifier-backed start instead exposes generated modifier options one at a time in modifier order. Unoffered alternatives remain absent. |
| Orobas — `Models.Events/Orobas.cs:11–94,189–214` | Ancient layout. Supplied relic event options include a visible locked third-pool placeholder if player-dependent candidates are unavailable; this is not the separate relic-choice screen. |
| Relic Trader — `Models.Events/RelicTrader.cs:16–95,112–170` | Default event Top/Middle/Bottom options refer to offered owned/new relic pairs. Dynamic text and hover tips expose both sides of each offered trade. No separate relic grid; do not expose unused backing offers beyond the presented options. |
| Room Full of Cheese — `Models.Events/RoomFullOfCheese.cs:20–53` | Default event; Gorge opens the exact-two simple reward grid over eight generated common cards. |
| Self Help Book — `Models.Events/SelfHelpBook.cs:20–141` | Default event; attack/skill/power enchant branches retain locked placeholders when another branch is available. If none is eligible, the event supplies a single no-options continuation instead. Enchant branches use the deck enchant selector. |
| The Architect — `Models.Events/TheArchitect.cs:30–54,150–172,306–358` | Combat event layout with a special encounter, dialogue continuation buttons and final proceed invoking WinRun. It is not an ordinary combat end-turn boundary. Dialogue selection uses profile-dependent visit/win context; capture the actual selected presentation once. |
| The Legends Were True — `Models.Events/TheLegendsWereTrue.cs:20–63` | Default event; map-card preview or damage plus potion reward overlay, then finished event. |
| Tinker Time — `Models.Events/TinkerTime.cs:18–160` | Default event pages. Initial continuation reveals two sampled card types, selected type reveals two sampled riders, and hover tips preview Mad Science with the offered type/rider. No hidden third option fabricated as disabled; no custom screen. |
| Vakuu — `Models.Events/Vakuu.cs:16–61,146–165` | Ancient layout; one option from each shuffled pool in pool order. Relic acquisition may itself suspend for a child prompt. Ancient options are not interchangeable with generic choose-a-relic selection. |

Crystal Sphere's `Nodes.Events.Custom.CrystalSphere/NCrystalSphereScreen.cs` and
Fake Merchant's `Nodes.Events.Custom/NFakeMerchant.cs` are discoverable bespoke
screens, but neither event is among these eleven roots. They remain expansion-scope
audits, rather than unexplained missing native paths for the current victory roots.

## Source-grounded golden cases to capture

These are **test specifications with source-derived expected relationships**, not
already captured goldens. Every capture must record its native UI/inspection path,
build/source identity, initial setup, actual rendered fields, offered input and
post-input state separately from privileged diagnostic state.

| Case | Setup and expected relation |
| --- | --- |
| P01 Fixed versus optional simple grid | Same three offered cards: exact 1 auto-submits on click; 0–2 remains partial and permits zero confirmation. Selected card toggles off; max blocks an additional selection. |
| P02 Hand differs from grid | Native discard/retain/upgrade hand prompt: capture preselection, select, deselect, replace-at-cap and explicit confirm. Include unpowered-preview and pretend-playable flags. Certify actual clicked path rather than the automated selector. |
| P03 Deck two-level cancellation | Shop removal: enter selection, select card, cancel preview, select another card, confirm; alternate branch closes grid. Preview cancel retains the pending purchase and empties partial selection. Whole close returns to shop without spending or incrementing removal count. Confirm spends once. |
| P04 Disabled rest option | Deck with no upgradable cards: Smith remains visible/inspectable with disabled description. A valid Smith branch cancelled at selection returns to usable rest options. Repeated inspect leaves stored options/RNG untouched. |
| P05 Dynamic eligible pile | Pile choice with filtered candidates: changing eligible membership removes invalid selected cards; reduced remaining candidates clamp completion count; none remaining completes empty. Capture a naturally reachable supported mechanic before claiming runtime coverage. |
| P06 Reward states | Ordered mixed rewards, one linked set, card alternatives, skipped/claimed states and all-claimed state. Terminal rewards retain proceed; a nonterminal custom reward returns to owner context. Include a DisallowSkipping set. |
| P07 Shop accepted failure | Full potion slots, enough gold, stocked potion: native attempt does not procure, charge or clear stock; preserve visible failure feedback. Compare with insufficient-gold presentation and used-removal presentation; don't fabricate English reasons. |
| P08 Event option shape | One native locked branch, one omitted branch, one lethal single-player branch and one repeatable cancellable confirmation. Capture actual visible option sequence and lock text. Cancellation must not make a repeatable branch unusable. |
| P09 Treasure before/reveal/after | Inspect closed chest, reveal, collect or skip, proceed. Relic identity/content is absent before native reveal and remains stable once revealed. Additional reward overlays must return to treasure collection. |
| P10 Nested room return | Event starts combat, rewards proceed resumes the original event; compare ordinary combat rewards proceeding to map. Same room type must not erase return context. |
| P11 Map reachability | Ordinary children, free-travel next row, boss and second-boss special cases. Keep visible topology fixed while varying hidden unknown-room resolution; actions remain unchanged until revelation. |
| P12 Terminal classification and fields | Natural loss and Architect victory, score-line presence/order including zero counts. Repeated inspect and acknowledgment do not reroll loss quote, replay final effects or alter outcome. Capture UI score fields independently from native save serialization. |
| P13 Bundle choice | Offer several distinct bundles; click one, inspect a card, cancel preview, choose another, confirm. Bundle row order is preserved; preview physical order is mapped explicitly. Preview cancel does not mean skip, grant cards or complete the owning effect. |
| P14 Relic choice | Offer several relics; choose one versus press the visible Skip. Immediate selection and empty skip are separate from bundle preview/confirm. IDs survive inspection and no unoffered pool relic appears. |
| P15 Explicit configuration | Enter ` oi `, clear seed, tick two mutually exclusive modifier types, tick CharacterCards variants, randomize once, inspect repeatedly and embark. Check native displayed entry, canonical start seed `01`, checkbox changes, modifier order and null/random seed distinction. This certifies the custom editor facts without asserting new game-mode scope. |
| P16 Event page and hover visibility | Tinker Time reveals only the sampled two types and then only the sampled two riders; Self Help Book exposes locked text versus sole no-options continuation; Relic Trader hover tips expose the actual offered pair. Neow modifier options appear serially, and Architect dialogue uses continue/respond/proceed rather than normal combat inputs. |

For paired-hidden-state cases, preserve visible offered options and their identity
history while changing future reward pools, unknown-room assignments, future RNG,
unrevealed treasure contents or irrelevant suspended execution storage. Require equal
public fields, stable IDs, exposed restrictions and option order until native revelation.
Do not build expected observations by filtering complete snapshots. At reveal, only
the newly visible field may distinguish the pair; after dismissal inaccessible current
payloads must disappear while any legitimately visible history follows its native scope.

## Evidence gaps and tool requirements

1. Native UI paths above are static facts. Existing headless TestMode/automated selector
   branches bypass them. A native UI capture or a deliberately instrumented equivalent
   must export actual option visibility, order, partial selection, preview stage, enabled
   buttons and already-rendered text. Instrument the UI consumer, not merely its model
   or the complete-state exporter. Capture final settled UI state after animation.
2. Selection fixtures must preserve whether a command auto-selected without a user wait.
   A command invocation, reserved network choice ID or awaited test selector is not proof
   that native users saw that prompt. Need native fixtures for each reachable screen
   family and prefs combination; arbitrary constructor combinations are not coverage.
3. Exact English text and conditional feedback require the separate English-source and
   rendering evidence task. No English literals or reasons here are certified. The native
   shop source demonstrates feedback without English; public structured failure categories
   must not be confused with visible sentence text.
4. Stable public identities are a simulator contract requirement, not established by
   native list indices. Need an identity adapter whose allocation depends on visibility
   history, paired-state tests, and reappearance cases across preview/return/claim.
5. Standard and explicit seeded/custom setup consumers are source-audited above. Their
   actual rendered controls and checkbox changes still need native capture. A source
   fact that custom mode offers seed/modifier editing does not itself expand the existing
   simulator matching scope or certify every modifier's runtime behavior.
6. Bundle and relic selectors, and all eleven root event layout paths, are source-audited
   above. Their native rendered goldens remain uncaptured. Individual card-reward
   alternative mechanics and bespoke events outside those roots need additional consumer
   cases as their supported closure expands; generic selection rules cannot certify them.
7. Terminal native display includes platform/profile dependent material excluded by this
   map, and loss presentation consumes Chaotic RNG at initialization. The eventual text
   contract must identify retained in-run result fields and presentation randomness; do
   not recreate excluded external values or certify deterministic wording from one run.

No engine implementation, new public transport, acceptance exception, ADR change or
glossary term is introduced by this audit.
