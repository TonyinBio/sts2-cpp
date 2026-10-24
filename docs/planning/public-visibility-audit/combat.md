# Combat visibility and inspection source audit

This is a source audit for **Audit player-visible fields, reveal rules and prompt
semantics**, not a passing public-observation corpus. No live oracle or native UI
was run for this audit. Proposed cases below are test specifications awaiting captures.
Complete-state victory snapshots cannot supply their expected public output.

## Provenance and source anchors

The installed `data_sts2_windows_x86_64/sts2.dll` was SHA-256 checked during this
audit: `0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.
Native citations refer to the existing local ILSpy 9.1.0.7988 decompilation at
`build/combat-state-audit/decompiled/`; it was not regenerated in this audit.
`combat-sources.json` records hashes for the files inspected here. Native
source and the game assembly are local prerequisites, not published artifacts.
Each row below gives its namespace directory, type, method and source lines.

| Key | Native source under the decompilation root | Anchor |
| --- | --- | --- |
| C1 | `MegaCrit.Sts2.Core.Nodes.Screens/NCardPileScreen.cs` | `OnPileContentsChanged`, lines 222–238 |
| C2 | `MegaCrit.Sts2.Core.Nodes.Cards/NCardGrid.cs` | `SortingAlgorithms`, 385–435; `GetCardRarityComparisonValue`, 598–613; `SetCards`, 832–861 |
| C3 | `MegaCrit.Sts2.Core.Nodes.Combat/NCombatCardPile.cs` | `Initialize`, count update around 237; `OnRelease`, 245–277 |
| C4 | `MegaCrit.Sts2.Core.Nodes.Combat/NExhaustPileButton.cs` | `_Ready`, 87–96; `Initialize`, 98–107; `AddCard`, 112–120 |
| C5 | `MegaCrit.Sts2.Core.Nodes.Combat/NPlayerHand.cs` | `GetHandInsertIndex`, 691–707 |
| C6 | `MegaCrit.Sts2.Core.Nodes.Screens.CardSelection/NCombatPileCardSelectScreen.cs` | `UpdatePileContents`, filtered cards, completion and sorting, 313–365 |
| C7 | `MegaCrit.Sts2.Core.Commands/CardSelectCmd.cs` | `FromCombatPile`, 444–520 |
| C8 | `MegaCrit.Sts2.Core.Nodes.Combat/NCreature.cs` | `UpdateIntent`, 617–646; `RefreshIntents`/`RevealIntents`, 663–680 |
| C9 | `MegaCrit.Sts2.Core.Nodes.Combat/NIntent.cs` | `UpdateIntent`, 224–230; `UpdateVisuals`, 240–260; `OnHovered`, 294–300 |
| C10 | `MegaCrit.Sts2.Core.MonsterMoves.Intents/HiddenIntent.cs` | entire type, lines 1–12 |
| C11 | `MegaCrit.Sts2.Core.MonsterMoves.Intents/AttackIntent.cs` | `GetIntentDescription`, 74–80; `GetSingleDamage`, 84–93 |
| C12 | `MegaCrit.Sts2.Core.Nodes.Combat/NPowerContainer.cs` | `Add`, 104–113; `SetCreature`, 147–158 |
| C13 | `MegaCrit.Sts2.Core.Models/PowerModel.cs` | `IsVisible`, 150–167; `DisplayAmount`, 218 |
| C14 | `MegaCrit.Sts2.Core.Nodes.Combat/NPower.cs` | `RefreshAmount`, 229–240 |
| C15 | `MegaCrit.Sts2.Core.Models.Powers/AmbergrisPower.cs` | `IsVisibleInternal`, 14 |
| C16 | `MegaCrit.Sts2.Core.Models/CardModel.cs` | `CanPlayTargeting`, 1696–1703; `CanPlay`, 1725–1754; `IsValidTarget`, 1762–1785 |
| C17 | `MegaCrit.Sts2.Core.Models/PotionModel.cs` | `IsValidTarget`, 260–295 |
| C18 | `MegaCrit.Sts2.Core.Nodes.Combat/NTargetManager.cs` | `AllowedToTargetNode`/`AllowedToTargetCreature`, 491–538; `OnCreatureHovered`, around 594 |
| C19 | `MegaCrit.Sts2.Core.Entities.Cards/UnplayableReasonExtensions.cs` | `GetPlayerDialogueLine`, 18–55 |
| C20 | `MegaCrit.Sts2.Core.Nodes.Combat/NMouseCardPlay.cs` | failed play feedback, 274–284 |
| C21 | `MegaCrit.Sts2.Core.Nodes.Cards/NCard.cs` | `UpdateEnergyCostVisuals`, 980–1004 |
| C22 | `MegaCrit.Sts2.Core.Models.Potions/DropletOfPrecognition.cs` | `OnUse`, 21–30 |

## Observed source facts

### Inspectable piles and ordering

Passive inspection makes a **copy** of `Pile.Cards`. For draw only, it sorts by
raw `CardRarity` then ordinal `Id.Entry`; discard and exhaust keep backing sequence.
It passes `SortingOrders.Ascending` to `NCardGrid.SetCards`, which copies the list
without sorting it again. Thus passive draw inspection does not reveal unrestricted
draw order, and sorting by localized title would change this particular display.
[C1, C2]

The passive draw comparator returns zero for two instances of the same model and
rarity, regardless of upgrade, affliction, enchantment or other visible differences.
It uses `List.Sort`, without a stable tie break. **Do not assert that every backing
draw permutation is publicly equivalent.** Equal-key visibly different duplicates
can have order affected by the backing list and the native runtime's sort algorithm.
The exact native output for such ties requires a captured case (including sufficiently
large lists to exercise sort behavior), rather than substituting instance-ID order.
This does not authorize publishing backing positions as IDs or unrestricted draw order.

Draw **selection** is a different display. The native selection screen uses
`RarityAscending` (the grid's explicit rarity mapping), then `AlphabetAscending`
(localized `CardModel.Title` under `LocManager.CultureInfo`), then model `Id` as
fallback. Equal values still have no instance tie break. In contrast, an injected
`CardSelectCmd.Selector`/`LocalSelector` receives LINQ ordering by rarity then `Id`.
Those adapter sequences do not independently certify the graphical choice order.
Other combat-pile selections preserve source sequence. [C2, C6, C7]

Opening an empty draw/discard pile produces an empty-pile thought bubble rather than
opening an empty grid. Exhaust initially hides/disables its button until a card exists;
after first reveal it becomes visible/enabled through `AddCard`. Presence of a zero
count alone cannot infer whether a button is displayed. Opening a pile also cancels
active targeting, so a graphical inspection gesture is not the same operation as the
contract's pure observation query. [C3, C4]

Hand holder insertion translates the backend hand order while accounting for cards
temporarily moved into selection/drag/queue containers. At settled ordinary combat
input, hand sequence is grounded by hand-pile order; partial-selection display needs
the separate selected-holder sequence too. [C5]

### Intents and powers

Native intent nodes follow `NextMove.Intents` in order. Labels are produced for
attack and status intents; hover details require `HasIntentTip`. `HiddenIntent`
has no sprite and no tooltip. An internal `Hidden` enum is therefore not a visible
intent icon or a player-facing explanation. Attack tooltip damage is per-hit with
repeat count, dynamically calculated using the local player's damage hooks. Never
replace this with future rolled damage or expose move-machine state. [C8–C11]

Native power nodes filter `PowerModel.IsVisible`; for local player/enemy targets this
delegates to `IsVisibleInternal`, while other target contexts may be hidden. For
example, `AmbergrisPower` explicitly remains hidden. The power amount label is
`DisplayAmount` only for `Counter` stack type, and empty otherwise. Publishing every
power's raw `Amount` conflates hidden powers, hidden labels and visible zero. [C12–C15]

### Playability, targets and feedback

Card playability combines unplayable keyword, resources, living-ally availability,
`Hook.ShouldPlay`, and card-specific logic. It returns a reason bitmask and first
preventer. Failed mouse play displays a **single prioritized dialogue**, with priority
no living allies, energy, stars, blocking hook, then unplayable/card logic. The hook
case may show the blocking model's localized title. Visible feedback should follow
that native path rather than serialize the bitmask/preventer internals. Disabled
cards remain in hand and receive cost/icon visual changes; they are not removed
from the public hand. [C16, C19–C21]

Card and potion target validity must not share a guessed generic predicate. Cards
accept null for their non-single-target forms, and reject a non-null target except
for `AnyEnemy`/`AnyAlly`; potion `Self` explicitly requires the owner creature.
Potion `AnyPlayer` accepts a living player creature; potion `AnyAlly` excludes self.
Native UI `NTargetManager` additionally applies an optional node filter and
`Hook.ShouldAllowTargeting` during hover, and its ally UI excludes the local player.
The model and UI checks must both be audited for an offered target; raw living-enemy
enumeration is insufficient. [C16–C18]

### Explicit reveal scope and purity limits

`DropletOfPrecognition.OnUse` requests one card from the draw pile and moves the
selected instance to hand. Its granted selection exposes eligible draw contents in
the selection display, not unrestricted draw sequence or future draw results. The
choice scope ends with that selection; the moved card is then visible through hand.
`FromCombatPile` can return immediately for zero candidates or when candidates fit
the minimum and manual confirmation is not required; do not fabricate a prompt in
these cases. [C7, C22]

Passive pile sorting works on a copied list, with no gameplay mutation/RNG call in
`OnPileContentsChanged`/`SetCards`. This establishes a narrow source fact, not global
inspection purity. Native `RevealIntents` uses `Rng.Chaotic` for animation delay;
animation work must not be executed by a pure C++ public projection. Preview hooks,
localization, and all other formatting paths still require before/after state and
RNG verification. A requirement that pure observation consume no RNG is a simulator
contract requirement, not proof that the graphical UI never draws cosmetic RNG.

## Source-backed golden case specifications

All rows remain **capture pending**. Native UI output/accessibility projection must
be captured independently, with matching pinned English resources, alongside the
privileged setup/checkpoint used only to establish preconditions.

| Case | Preconditions and expected public distinction | Source basis |
| --- | --- | --- |
| combat-pile-order | Draw contains at least three different rarity/model keys in non-display order; inspection shows rarity + ordinal model entry, not backing draw order. Discard and exhaust retain their sequence. | C1, C2 |
| combat-draw-equal-keys | Same-model duplicates with different visible upgrades/enchantments; capture native order for short and longer draw piles. Preserve native tie behavior without inventing hidden instance-order fields. | C1, C2 |
| combat-selection-order | Draw selection includes names whose English title order differs from model entry order and status/curse/token rarities. Capture native grid versus selector adapter; their output order need not agree. | C2, C6, C7 |
| combat-empty-piles | Empty draw/discard yields thought feedback; never-used exhaust button is absent; empty-after-reveal exhaust is a distinct UI history case. | C3, C4 |
| combat-powers | Visible counter zero, visible non-counter, hidden AmbergrisPower: respectively preserve visible zero, empty amount label, and absence of hidden power. | C12–C15 |
| combat-intents | Single attack, multiattack, status, composite intent list and HiddenIntent: capture ordered icons/labels/tips with damage modifiers. Hidden has no fabricated public type label. | C8–C11 |
| combat-disabled-card | Card has simultaneous energy and hook restrictions; hand retains it, cost/icon disabled state is visible, failed play feedback selects native priority and localized blocker name only where used. | C16, C19–C21 |
| combat-targets | Living enemy, dead enemy, self, other player, node-filtered or hook-blocked target; compare card and potion forms including null/self difference. Restrict production fixtures to reachable single-player states; other-player branch is source-only. | C16–C18 |
| combat-draw-choice-scope | Droplet: 0/1/many eligible draw cards, manual/nonmanual confirmation variants. Capture whether there is a prompt and its order, then observe selected card in hand and absence of any granted ordered-draw view. | C7, C22 |
| combat-query-purity | Repeated observation/actions/previews/pile queries leave complete state, pending prompt, gameplay RNG, identity allocator and boundary unchanged; native graphical open/target-cancel is recorded separately. | C1–C3, C8; contract |

## Paired-hidden-state cases

These are proposed noninterference tests, not claims that the pairs have already
been captured or that arbitrary modified checkpoints are legal reachable states.

- Permute backing draw cards with distinct passive sort keys, retaining identical
  visible card instances and modifications, all other visible state, and native
  sorted display. Public passive inspection/actions must match. Keep equal-key
  visibly distinct duplicate permutations out of this pair unless native output
  proves them observationally equivalent.
- Alter unrevealed future RNG/AI history while preserving the current native
  `NextMove.Intents` and all visible resources; current intent output must match.
  Do not pair states where changed hooks alter displayed damage.
- Vary an internal hidden power amount while preserving every displayed effect,
  action eligibility, visible reason and current-turn timing. Public power lists
  must match. Ambergris variants that change current available actions are not an
  equivalent pair simply because the power icon is absent.
- Insert hidden allocations while preserving visible object relationships and
  exposure history. Public IDs and ordering must match under the chosen public
  identity contract; backing allocation indices must not leak. This is an API
  noninterference requirement, not a native UI numeric-ID observation.

## Evidence still required

No native graphical captures, reachable fixture constructors, matching public
observation replay harness, or automated noninterference runner were supplied by
this sub-audit. Existing dense checkpoints certify complete-state replay only.
Exact English/rendered dynamic values await the separate English-source audit.
The listed explicit draw-choice reveal is an audited example, **not a complete
catalogue of reveal mechanics for five characters and every reachable model**.
Unknown mechanic visibility remains unsupported until audited. Equal-key sort
behavior and native selection-versus-adapter ordering are concrete capture gaps,
not licenses to infer expected observations from filtered complete-state JSON.
