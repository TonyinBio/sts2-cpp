# Native prompts, restrictions and feedback rendering

This is a static source audit of the existing pinned v0.111.0 ILSpy decompilation.
It supplies rendering call sites and source-grounded golden-case specifications;
it does not supply captured native rendered goldens, certify any English literal,
or execute the game. Sources are local under
`build/combat-state-audit/decompiled/`. Citations below omit the common
`MegaCrit.Sts2.Core.` prefix. [prompts-feedback-sources.json](prompts-feedback-sources.json)
records exact paths, SHA-256 hashes and inspected ranges. The established assembly
identity is `0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`;
this sub-audit hashes existing source files rather than freshly decompiling the DLL.
No full game source, assembly, or localization tables are published here.

The previous [visibility audit](../public-visibility-audit/prompts.md) establishes
which prompt/option is shown and how its input works. This report concerns when
English is formatted, what substitutions reach it, and what evidence can certify it.
A localization key is an address, not evidence of the final text at that address.

## Formatting and emission paths

| Surface / source anchor | Native English/render behavior | Evidence implication |
| --- | --- | --- |
| Selection prompt preferences — `CardSelection/CardSelectorPrefs.cs:11–78` | Standard prompts use `card_selection` TO_TRANSFORM, TO_EXHAUST, TO_REMOVE, TO_ENCHANT, TO_DISCARD and TO_UPGRADE. Constructor supplies Amount and range MinCount/MaxCount; exact-count constructor also adds Amount. | Capture exact count 1, count >1 and variable range separately. Constructor values are not proof every screen supports every range; consumer behavior remains screen-specific. |
| Simple/pile/hand prompts — `Nodes.Screens.CardSelection/NSimpleCardSelectScreen.cs:168–186`; `Nodes.Screens.CardSelection/NCombatPileCardSelectScreen.cs:164–184`; `Nodes.Combat/NPlayerHand.cs:956–989` | Simple and pile screens assign `prefs.Prompt.GetFormattedText()` to their info labels at setup. Hand selection wraps the already formatted prompt in `[center]...[/center]`. | Match language, substituted values, line breaks and meaningful markup independently of alignment/animation. Preserve a prompt's captured stage; do not derive its historical wording later from a changed prefs object. |
| Deck and enchant prompt — `Nodes.Screens.CardSelection/NDeckCardSelectScreen.cs:193–230`; `Nodes.Screens.CardSelection/NDeckEnchantSelectScreen.cs:255–300` | Deck prompt formats on screen setup. Enchant setup creates a mutable enchantment copy, assigns amount, recalculates, then formats its title and DynamicDescription; prompt is centered. | Capture prompt plus enchantment title/description at each actual preview stage. Generic card text is insufficient to certify enchantment preview text. |
| Offer/continuation headings — `Nodes.Screens.CardSelection/NCardRewardSelectionScreen.cs:256–264,312–324`; `Nodes.Screens.CardSelection/NChooseABundleSelectionScreen.cs:209–222`; `Nodes.Screens/NChooseARelicSelection.cs:198–207`; `Nodes.CommonUi/NProceedButton.cs:244–246,371–375` | Card row heading uses gameplay_ui CHOOSE_CARD_HEADER via raw lookup; bundle heading CHOOSE_A_PACK via raw lookup; relic heading CHOOSE_RELIC_HEADER via raw lookup. Reward alternative button receives the alternative's formatted Title. Proceed and Skip use gameplay_ui PROCEED_BUTTON and CHOOSE_CARD_SKIP_BUTTON with formatted lookup. IsSkip is keyed by the localization entry, not string equality. | Raw lookup and formatted lookup are distinct call paths. Capture actual button/heading labels; a button's semantic role must not be inferred from an English spelling that may change with locale. Confirm/back captions not assigned in these inspected handlers may reside in scenes or base controls, requiring their own native resource evidence. |
| Event body — `Nodes.Rooms/NEventRoom.cs:260–288`; `Nodes.Events/NEventLayout.cs:324–339` | Title formats immediately. For an existing body description the room adds character details, IsMultiplayer and event DynamicVars before formatting; layout receives the resulting string. Finished events replace options with a PROCEED option. | Capture dynamic event body and finished-page text through this consumer, with absence distinguished from an existing empty description. Feeding the localization table directly misses context substitutions and fallback behavior. |
| Event options/locked branch — `Nodes.Events/NEventOptionButton.cs:284–341` | Event DynamicVars are added to title and description separately. Both format at button setup. Empty title shows just the description; nonempty title is bold gold, or bold red when locked, followed by newline and description. | Compare empty-title versus missing field, explicit line break, emphasis and locked styling meaning. A locked option's visible text is its supplied option text, not a generic reason automatically generated from legality. |
| Ancient dialogue — `Nodes.Events/NAncientDialogueLine.cs:136–143`; `Nodes.Events/NAncientEventLayout.cs:379–391,434–493`; `Nodes.Rooms/NEventRoom.cs:231–249` | Each dialogue line adds character details and Ancient DynamicVars, then stores formatted text in its label. Native room selects valid dialogue using Chaotic RNG and profile visit context. Layout separately advances visible lines and enables options at the last line. | A full hidden dialogue definition is not proof every line is currently displayed. Capture the actual selected dialogue and displayed stage. Reobserving must not choose another line set or leak future lines. |
| Rest descriptions — `Entities.RestSite/SmithRestSiteOption.cs:25–49`; `Entities.RestSite/HealRestSiteOption.cs:37–69`; `Nodes.RestSite/NRestSiteButton.cs:224–243,329–344` | Smith returns `.description` with Count when enabled, `.descriptionDisabled` otherwise. Heal creates a HealVar with base amount and modified PreviewValue, adds Character, and composes optional hook-provided ExtraText using newline joins or empty string. Button title formats during reload; description formats when focused or while executing. | Current hover text is live formatting of the option, not immutable event feedback. Test enabled/disabled and no-extra/multiple-extra text separately; missing and empty ExtraText must not be collapsed. |
| Reward descriptions — `Rewards/GoldReward.cs:36–51`; `Rewards/CardReward.cs:83–83`; `Rewards/PotionReward.cs:34–34`; `Rewards/RelicReward.cs:50–50`; `Nodes.Rewards/NRewardButton.cs:232–245` | Gold creates gameplay_ui COMBAT_REWARD_GOLD or COMBAT_REWARD_GOLD_STOLEN, adding `gold=Amount`. Card reward uses COMBAT_REWARD_ADD_CARD, potion/relic reward use their titles. Button reload formats the then-current Description. | Capture generated amount and stolen-back variant. Reward selection history is not the exact label that was shown before a later change. |
| Merchant prices — `Nodes.Screens.Shops/NMerchantCard.cs:148–160`; `Nodes.Screens.Shops/NMerchantPotion.cs:128–139`; `Nodes.Screens.Shops/NMerchantRelic.cs:150–161`; `Nodes.Screens.Shops/NMerchantCardRemoval.cs:129–154` | Price labels use native integer `Cost.ToString()`. Removal cost disappears when Used; affordability changes its color. | Do not invent currency symbols or label an absent used-service cost zero. Numeric formatting needs runtime culture/provenance; a localization template is not the source of these digits. |
| Merchant feedback, full path — `Nodes.Screens.Shops/NMerchantInventory.cs:374–394,431–435`; `Nodes.Screens.Shops/NMerchantDialogue.cs:108–146`; `Entities.Merchant/MerchantDialogueSet.cs:46–82`; `Nodes.Screens.Shops/NMerchantSlot.cs:391–413` | Inventory subscribes PurchaseFailed to ShowForPurchaseAttempt, and success calls the same dialogue path. MerchantDialogueSet maps Success, FailureGold, FailureSpace and FailureForbidden to distinct line pools. The dialogue picks a Chaotic random line, formats it now, and wraps it in `[fly_in]`. Slot also wiggles and sounds on failure. Out-of-stock is not mapped to an English pool in this method and falls through its exception branch. | **Native merchant feedback can include English dialogue.** A slot-local inspection alone misses it. Capture the actual selected key/text and status; enum names are not exact visible explanations. Do not manufacture a mapped sentence for statuses this consumer does not handle. Repeated observation must not call ShowRandom again. |

## Combat restrictions and emitted feedback

`Models/CardModel.cs:1725–1755` accumulates a bitmask of reasons: unplayable
keyword, insufficient resources, no living allied target, hook prevention and
card-specific logic. Missing combat/player combat context can return false with no
public reason. These flags are structured diagnostics; they are not themselves
the player-facing English explanation.

`Entities.Cards/UnplayableReasonExtensions.cs:19–55` chooses one native dialogue
line with this precedence:

1. NoLivingAllies → combat_messages NO_LIVING_ALLIES.
2. EnergyCostTooHigh → NOT_ENOUGH_ENERGY.
3. StarCostTooHigh → NOT_ENOUGH_STARS.
4. BlockedByHook → UNPLAYABLE if preventer is absent; otherwise BLOCKED_BY_HOOK
   with BlockingHook set to the **formatted visible title** of card, relic, power,
   enchantment or affliction preventer.
5. HasUnplayableKeyword or BlockedByCardLogic → UNPLAYABLE.
6. No matching flag → no LocString, rather than empty-text feedback.

Unknown preventer types log internally and substitute `<UNKNOWN>` in this native
path; this is a native fallback requiring an explicit unsupported/evidence case,
not permission to expose an internal object description or invent a better reason.
Combined flags do not produce concatenated English reasons.

Mouse/controller attempts format the chosen line and pass the resulting string to
NThoughtBubbleVfx: `Nodes.Combat/NMouseCardPlay.cs:263–285` and
`Nodes.Combat/NControllerCardPlay.cs:177–198`. Autoplay hook-prevention also
emits through this path after moving the card to its result pile:
`Commands/CardCmd.cs:51–73`. Consequently feedback can originate from an automatic
effect, not just a submitted public action. The normal hand affordance does not
mean the native game permanently displays all these sentences alongside disabled
cards. Card icons separately use HasResourceCostReason:
`Nodes.Cards/NCard.cs:989–1002,1060–1073`.

| Emitter / source | Captured versus live values |
| --- | --- |
| `Commands/ThinkCmd.cs:17–33`; `Commands/TalkCmd.cs:18–47` | Both call GetFormattedText before creating the bubble. Think derives timing from formatted length with a 1.5s minimum. Talk rejects dead speakers and derives duration from plain character count or preset duration/fast-mode preference, with a 0.5s minimum. Timing does not change the supplied text. |
| `Nodes.Vfx/NThoughtBubbleVfx.cs:150–179,195–222,256–265`; `Nodes.Vfx/NSpeechBubbleVfx.cs:194–224,297–308,334–342` | Bubble creation stores the supplied string, not a live LocString or model pointer for future substitutions. Ready wraps the stored text in centered fly_in markup. Thought can carry an image instead of text; absent text is distinct from an empty text string. TestMode suppresses bubble creation. |
| `Nodes.Vfx/NDamageNumVfx.cs:102–163` | Damage number is captured from DamageResult: UnblockedDamage plus OverkillDamage except against Osty, or the supplied explicit int overload; integer ToString is stored before Ready. Position/velocity/scale/rotation consume Chaotic RNG, but cannot change stored digits. |
| `Nodes.Vfx/NHealNumVfx.cs:94–119` | The provided decimal amount is cast to int then ToString at Create. It is not computed by subtracting the target's HP during a later observation. Native creature interactability can suppress the visual entirely. |
| `Nodes.Combat/NCombatCardPile.cs:249–268` | Empty-pile click formats the configured empty-pile message at the attempt, while the ordinary count label uses the current count. Zero pile count and a generated feedback sentence are different surfaces. |
| `Models.Relics/WhisperingEarring.cs:71–90` | After automatic plays, no-play returns without a line; otherwise warning versus approval uses relics WHISPERING_EARRING.warning/.approval according to the 13-play cap and emits purple speech through TalkCmd. A later changed hand or count must not rewrite this already emitted line. |

For historical feedback, retain either the final string plus meaningful markup and
locale identity, or an immutable emission record containing the chosen key and
captured scalar/title substitutions sufficient to reproduce that exact string.
Keeping a live DynamicVars/model reference is insufficient. This is a planning
implication of these native call sites and the existing decision contract, not a
new engine implementation or a claim every native VFX is in scope.

## Terminal and profile/presentation limits

`Nodes.Screens.GameOverScreen/NGameOverScreen.cs:433–547,647–738` computes score
once at screen setup and formats score lines in floors/gold/elites/bosses/ascension
order, omitting zero elites, zero bosses and zero ascension. Labels add their numeric
amounts through LocString; score values are separately signed strings or an ascension
multiplier string. A result with no elite line is distinct from a visible elite line
containing zero. The summary's label/value pair must be captured, not reconstructed
from total score alone.

Win display includes personal/global Architect damage and conditional ascension-unlock
text, drawing on platform/profile state. Loss chooses banner and quotation using
Chaotic RNG during initialization. Later encounter quotation and summary are separate
presentation stages. Ancients also select dialogue with profile context, and
`Models.Events/TheArchitect.cs:306–346` uses profile wins/visits and dialogue speaker
to select continued button text or explicit NextButtonText. None of this establishes
that profile progression or platform services moved into simulator scope.

Exact-English certification must declare the covered in-run result fields and the
profile/presentation preconditions used by each fixture. A single pinned game seed
does not fix Chaotic merchant/loss dialogue or profile-based dialogue. Capturing the
actual chosen presentation once can ground a fixture; silently hard-coding one line
as universally exact cannot. If matching those presentation choices would require a
contract change, that remains a user decision; this source task grants no exception.

## Source-grounded native golden cases to capture

These cases specify independent consumer captures. They are **not existing runtime
goldens**. Record locale, game/resource identities, selected key, immutable substitution
inputs, final formatted markup/text, actually visible stage and emission order. A
headless TestMode run that never creates the UI is not equivalent evidence.

| Case | Native consumer and expected distinction |
| --- | --- |
| E01 Count/range prompts | Card selection exact 1, exact 2 and 0–2; capture instantiated prompt after Amount/MinCount/MaxCount substitution in simple grid, pile and hand. Verify hand center wrapper without silently changing meaningful inline markup. |
| E02 Preview prompts | Deck removal/upgrade/enchant initial grid, preview, preview cancel, overall close. Capture enchantment amount/title/description on the recalculated preview copy. Confirm/back captions require native scene/base-control capture if not set by these handlers. |
| E03 Offer/continue labels | Card reward, bundle, relic choice, reward alternative, Skip and Proceed; compare raw lookup versus formatted consumer and preserve semantic button role independently of string spelling. |
| E04 Event body/locked text | Self Help Book enabled/locked/no-options pages and Tinker Time generated hover previews. Record body substitutions, character context, title-empty case, newline and locked/emphasis spans; internal legality categories do not replace native strings. |
| E05 Rest live hover | Smith enabled Count versus disabled description; Heal with no extra hook text versus two extra lines and a modified heal preview. Mutate relevant live value before a fresh hover; expect a fresh description while earlier emitted feedback stays fixed. |
| E06 Rewards | Gold 0, 1 and larger amount where legally reachable, ordinary versus stolen-back; potion/relic title versus generic card-reward label; unpopulated gold's sentinel is not a shown negative reward. |
| E07 Reason priority | Native attempt with energy+hook flags and star+hook flags; capture one selected dialogue by precedence. Include unplayable keyword/card logic and missing preventer versus a visible named preventer. Capture legal supported setups rather than force arbitrary flags into the public interface. |
| E08 Feedback emission freeze | Emit BLOCKED_BY_HOOK, then change/remove/rename a mutable source or its dynamic values before requesting retained feedback. Captured line/title must retain emission-time wording. Repeat for a damage amount followed by more damage and a heal followed by changed HP. |
| E09 Automatic feedback | Autoplay blocked by a hook; Whispering Earring no-play/approval/cap-warning branches. Verify lines can originate between two public inputs, retain native emission order, and are not fabricated on every observation. |
| E10 Merchant full feedback | Open shop; successful purchase; failures for gold, space and forbidden procurement. Capture actual selected merchant key, displayed formatted line, price label and nontext wiggle/sound separately. FailureOutOfStock is not an assumed fifth localized pool. Repeated inspect must not pick another random line. |
| E11 Numeric feedback | Damage including overkill, Osty overkill exclusion, integer explicit overload and fractional heal amount. Capture actual stored numeric text before further HP changes; capture suppressed visual as absence. Presentation position RNG must not alter digits or gameplay randomness. |
| E12 Text/empty/image | Empty pile click feedback; thought image-only payload; a visible empty string versus no text payload versus no VFX. Verify optional feedback does not invent English to fill an absent text field. |
| E13 Terminal line set | Natural loss and victory; zero/nonzero elites/bosses/ascension; capture ordered score label/value pairs and the actual selected loss quote. Reobserve after summary acknowledgment without repeating final effects or rerolling text. |
| E14 Dialogue stages | Ancient early line versus last line, Architect continue/respond/explicit override/proceed and visit-context variants. Capture selected dialogue and only currently visible lines; no future-line leakage through the text formatter. |
| E15 Inspection purity | After creating each pending prompt or feedback, alternate observation/action enumeration, formatting enabled/disabled, repeated English rendering and pending advance; compare whole owned semantic state, RNG, boundary and prompt identity. Formatting cached emission text must not reexecute UI setup, random dialogue selection or cost calculation that rolls stock. |

## Remaining evidence/tool gaps

- Need a native rendering capture path at the actual consumers above, with TestMode
  bypass disabled or explicitly compensated by a verified UI-equivalent harness.
  Exporting complete state and filtering keys cannot establish these strings or their
  emission order. Existing oracle display is not automatically this independent path.
- Native tables/resource provenance is a separate prerequisite. A key found in source
  and a similarly named upstream localization entry do not certify the pinned English.
  Runtime culture, scene/base-control captions, markup normalization and nontext
  feedback categories must be recorded by the eventual evidence format.
- This report anchors representative selection, event, reward, rest, shop and terminal
  paths plus combat rejection/speech/numbers. It does not claim exhaustive coverage of
  every content-authored combat message, every custom event screen, or all card/relic/
  power descriptions. New supported content requires its own reachable consumer cases.
- Chaotic dialogue and profile/platform context need explicit fixture preconditions.
  Tests may freeze a captured presentation choice, but must not silently turn that
  fixture technique into a simulator behavior rule or approve an exactness exception.

No engine/text renderer/public transport implementation, new glossary term, ADR change,
or acceptance exception is introduced by this source audit.
