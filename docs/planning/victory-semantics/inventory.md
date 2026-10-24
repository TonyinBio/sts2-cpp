# Observed relic and potion semantic audit

Source-inspected planning evidence for the victory milestone. No implementation or branch parity is claimed. All 46 roots were read; `inventory.json` retains source paths, hashes, member locations and direct dependency distinctions. Citations below are to the pinned local decompilation, with ranges covering each inspected definition. Fixed dependencies require effects; random pools require full native generation/identity and explicit unsupported effects outside milestone scope.

## Shared potion and generation behavior

`PotionModel.cs:297–353`: remove potion from inventory before BeforePotionUsed, push source/choice context, begin effect scope, run OnUse through branching pause/completion, end effect scope in finally; only surviving owner receives finished/history/AfterPotionUsed/map-history/empty-hand/pop operations. Retain consumed source while suspended; do not model use as slot deletion plus immediate stat delta. All 15 roots declare CombatOnly. AnyPlayer means owner in scoped single-player; WeakPotion targets AnyEnemy, ExplosiveAmpoule and PotionOfBinding target AllEnemies.

`PotionFactory.cs:45–95`: out-of-combat generation uses unlocked character+shared pools, optional blacklist; per result roll NextFloat then rarity <=0.1 Rare, <=0.35 Uncommon, otherwise Common; NextItem in rarity subset; remove result before next draw. Alchemical Coffer uses CombatPotionGeneration despite executing outside combat.

`CardFactory.cs:119–164`: distinct combat generation applies player-count filter then CanBeGeneratedInCombat, excludes Basic/Ancient/Event rarity, distincts, TakeRandom, creates combat-owned mutable cards. Preserve actual source enumeration and RNG. `CardFactory.cs:89–109,214–320`: reward generation maintains per-offer blacklist, reward options/hooks, rarity and upgrade rolls; Sea Glass flags must not be treated as disabling every hook or upgrade.

`VakuuCardSelector.cs:16–26`: forced choices take first maxSelect offered options, reward chooses first card. Whispering Earring requires this policy as well as automatic play; a generic user prompt is not equivalent.

## Per-root requirements

### RELIC.ALCHEMICAL_COFFER

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/AlchemicalCoffer.cs:1–32` (SHA-256 in JSON).

On acquisition record old capacity; increase max potion count by 4; generate 4 distinct rarity-selected out-of-combat potions using CombatPotionGeneration; procure sequentially into old capacity+i slots.

Fixed dependencies: shared commands only. Random-output boundary: All unlocked character+shared potion pool outcomes; generation is mandatory, arbitrary potion effect implementations are not fixed dependencies..

Regression requirements: Capacity expansion, existing sparse slots, each rarity, all four procurement slots.

### RELIC.ARCHAIC_TOOTH

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/ArchaicTooth.cs:1–173` (SHA-256 in JSON).

Setup selects first deck card among five starter IDs and saves serialized starter/replacement; acquisition transforms current first matching starter. Fixed mapping Bash→Break, Neutralize→Suppress, Unleash→Protector, FallingStar→MeteorShower, Dualcast→Quadcast; preserve upgrade and cloned enchantment. Fallback replacement method creates Doubt; normal SetupForPlayer false prevents no-starter offer.

Fixed output-effect dependencies: Break, Suppress, Protector, MeteorShower, Quadcast. Bash, Neutralize, Unleash, FallingStar and Dualcast are identity predicates for matching existing starter cards; their effects are not invoked by the mapping. Doubt is a defensive helper fallback whose ordinary legal reachability is not established. Random-output boundary: none.

Regression requirements: All five starter mappings; upgraded+enchanted source; no matching starter; serialized descriptors and clone hover-tip isolation.

### RELIC.BAG_OF_MARBLES

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/BagOfMarbles.cs:1–31` (SHA-256 in JSON).

Before side turn start, if owner participates and owner turn <=1, apply Vulnerable 1 to current hittable enemies.

Fixed dependencies: VulnerablePower. Random-output boundary: none.

Regression requirements: Opening versus later turn; hittable subset; multi-enemy ordered application.

### RELIC.BAG_OF_PREPARATION

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/BagOfPreparation.cs:1–26` (SHA-256 in JSON).

Modify owner hand draw by +2 only while owner turn <=1; other players/later turns unchanged.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: First draw with hand cap, reshuffle, draw-modifier combinations.

### RELIC.BLOOD_VIAL

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/BloodVial.cs:1–24` (SHA-256 in JSON).

After owner player-turn-start late, turn <=1 heal 2.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Full/partial HP; ordering after early start hooks; later turns do not heal.

### RELIC.BURNING_BLOOD

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/BurningBlood.cs:1–24` (SHA-256 in JSON).

After combat victory, heal 6 only if owner is not dead.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Victory/death distinction, HP cap, victory hook order.

### RELIC.BURNING_STICKS

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/BurningSticks.cs:1–62` (SHA-256 in JSON).

Combat-room entry resets once/combat flag and activates status. First own exhausted Skill clones exact card and adds generated clone to Hand; after awaited add mark used. Combat end resets flag/status. Ethereal cause is not excluded.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: First/second Skill exhaust, Attack exhaust, Ethereal exhaust, full hand, clone modifications/identity, nested triggers before used flag assignment.

### RELIC.CENTENNIAL_PUZZLE

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/CentennialPuzzle.cs:1–58` (SHA-256 in JSON).

After damage received in ongoing combat, own creature and UnblockedDamage>0 and unused: mark used BEFORE drawing 3 via three awaited single draws; reset combat end.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Blocked/zero/non-owner damage, damage outside combat, lethal damage, nested draw/exhaust hooks and reshuffle.

### RELIC.CHOSEN_CHEESE

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/ChosenCheese.cs:1–21` (SHA-256 in JSON).

After combat END (not victory-only), gain 1 maximum HP.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Victory/death end hooks; max-HP/current-HP command behavior.

### RELIC.DISTINGUISHED_CAPE

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/DistinguishedCape.cs:1–55` (SHA-256 in JSON).

Acquisition selects 2 curses without replacement from unlocked CurseCardPool cards whose CanBeGeneratedByModifiers is true, sorted Id, using Niche; add curses to Deck sequentially; then create/add 3 Apparitions.

Fixed dependencies: Apparition. Random-output boundary: Eligible curse models via Niche; preserve full eligibility, ordering, no replacement..

Regression requirements: Two distinct curses and RNG; acquisition hooks per add; Apparition exact effects; empty/changed eligibility.

### RELIC.GOLDEN_PEARL

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/GoldenPearl.cs:1–21` (SHA-256 in JSON).

Acquisition gains 150 gold.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Gold modifiers/history, overflow/limits, acquisition order.

### RELIC.GORGET

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Gorget.cs:1–29` (SHA-256 in JSON).

After entering CombatRoom apply Plating 4 to owner via ThrowingPlayerChoiceContext.

Fixed dependencies: PlatingPower. Random-output boundary: none.

Regression requirements: Combat vs noncombat rooms, combined initial powers and turn start.

### RELIC.HORN_CLEAT

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/HornCleat.cs:1–28` (SHA-256 in JSON).

After own block cleared, if owner turn==2 gain 14 Unpowered block.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Turn 1/2/3; retained/preexisting block; exact block-cleared hook placement.

### RELIC.JOSS_PAPER

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/JossPaper.cs:1–149` (SHA-256 in JSON).

Own non-Ethereal exhaust increments persistent CardsExhausted and draws floor(counter/5) then counter%=5 when >=5. Ethereal exhaust instead accumulates EtherealCount; after own side turn end add deferred count, clear it, draw threshold amounts after flush. Combat end clears deferred count only.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Cross-combat remainder; >=10 batch exhaust; Ethereal draw after flush; nested draws before modulo; saved counter vs transient display activation.

### RELIC.LEAD_PAPERWEIGHT

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/LeadPaperweight.cs:1–37` (SHA-256 in JSON).

Acquisition creates 2 Colorless reward choices with source Other and RegularEncounter rarity odds; choose canSkip; add selected to Deck, and append not-picked CardChoiceHistoryEntry for every unselected including both on skip.

Fixed dependencies: shared commands only. Random-output boundary: ColorlessCardPool reward generation and all native reward/upgrade/modifier hooks..

Regression requirements: Choose either/skip; rewards RNG/odds and upgrade; unchosen history; nested reward modification.

### RELIC.LETTER_OPENER

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/LetterOpener.cs:1–142` (SHA-256 in JSON).

Before combat reset Skill counter; owner side turn start resets except turn1. After own Skill played in ongoing combat increment; each multiple of 3 deal 5 Unpowered damage to hittable enemies. Combat end resets display activation/status.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Skill 3/6, duplicated plays, combat ending during card, turn resets, multi-target death/hook ordering.

### RELIC.LORDS_PARASOL

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/LordsParasol.cs:1–113` (SHA-256 in JSON).

On MerchantRoom entry asynchronously purchase owner local inventory for free: stocked character cards in order, stocked colorless in order, relic entries then potion entries using purchase wrappers. Finally if removal entry exists and run ongoing purchase noncancelable free removal. Input-blocking/UI operations surround purchase sequence.

Fixed dependencies: shared commands only. Random-output boundary: Actual generated shop contents; arbitrary shop item effects are random outcomes, wrapper/payment/selection semantics mandatory..

Regression requirements: Full potion slots; out-of-stock card branch; relic pickup prompts before following entry; free noncancelable removal; run ends mid-chain; continuation barrier before next player input.

### RELIC.LUCKY_FYSH

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/LuckyFysh.cs:1–33` (SHA-256 in JSON).

On any own card changing into Deck, gain 15 gold; generation excludes shops and after-act3-treasure eligibility via IsBeforeAct3TreasureChest.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: New add/transform/move into Deck; repeated transitions; gold modifier hooks.

### RELIC.MINIATURE_TENT

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/MiniatureTent.cs:1–19` (SHA-256 in JSON).

ShouldDisableRemainingRestSiteOptions returns false for owner, true for other player; owner flashes.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Multiple rest choices, exhausted individual option versus remaining options; other-player branch excluded single-player but preserve owner semantics.

### RELIC.MUMMIFIED_HAND

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/MummifiedHand.cs:1–49` (SHA-256 in JSON).

After own Power played during ongoing combat, select hand candidate using CombatCardSelection: prefer base energy>0 or base star>0 AND currently costs; fallback any currently costs; fallback base-cost-positive set; fallback any hand. Set selected card free this turn, null-safe.

Fixed dependencies: shared commands only. Random-output boundary: No new model output: selection among existing cards..

Regression requirements: All four candidate tiers, empty hand, energy/star/global costs, exact RNG calls including empty NextItem cases, duplicated powers.

### RELIC.NUNCHAKU

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Nunchaku.cs:1–109` (SHA-256 in JSON).

After own Attack played increment saved AttacksPlayed even if combat no longer ongoing; if ongoing and multiple of 10 gain 1 energy. Persistent counter has no combat reset override.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: 9→10 across combats; killing tenth Attack no energy; repeated/autoplay attacks; counter codec.

### RELIC.PENDULUM

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Pendulum.cs:1–117` (SHA-256 in JSON).

Before own hand draw set saved TurnsSeen=(TurnsSeen+1)%3. Modify own hand draw +1 when TurnsSeen==0; combat end resets status only, not saved counter.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Cross-combat 3-turn cycle; modifier before BeforeHandDraw, codec at remainder, no accidental counter reset.

### RELIC.PERMAFROST

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Permafrost.cs:1–54` (SHA-256 in JSON).

Combat room entry clears ActivatedThisCombat. After first own Power played while combat ongoing gain 7 Unpowered block then mark activated.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: First/second Power, killing Power ends combat, nested block-trigger reentrancy before flag assignment.

### RELIC.PUNCH_DAGGER

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PunchDagger.cs:1–34` (SHA-256 in JSON).

Acquisition selects exactly one eligible deck card through FromDeckForEnchantment using canonical Momentum/amount5; enchant chosen card with mutable Momentum5.

Fixed dependencies: Momentum. Random-output boundary: none.

Regression requirements: No eligible/one/many candidates selection helper behavior; existing enchantment compatibility; amount5 and upgrade interaction.

### RELIC.RAINBOW_RING

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/RainbowRing.cs:1–132` (SHA-256 in JSON).

Before own side turn reset attack/skill/power counts and activation count. Own played cards in ongoing combat increment type count until first activation; once all three positive apply Strength1 THEN Dexterity1 THEN increment activation. Combat end resets all counters.

Fixed dependencies: StrengthPower, DexterityPower. Random-output boundary: none.

Regression requirements: All six orderings, duplicate plays, once/turn guard, nested power-hook ordering before activation increment.

### RELIC.RED_SKULL

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/RedSkull.cs:1–86` (SHA-256 in JSON).

On combat room entry and any current-HP change while combat ongoing evaluate OWNER HP <=50% max. If newly low add Strength3; if above and previously applied subtract3; combat end clears latch/status. Hp-change hook does not filter changed creature.

Fixed dependencies: StrengthPower. Random-output boundary: none.

Regression requirements: Exactly half/odd maxHP, heal/damage crossings, other-creature HP changes, maxHP change consequences, clone latch.

### RELIC.SEA_GLASS

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/SeaGlass.cs:1–94` (SHA-256 in JSON).

Saved CharacterId selects character pool/title; missing ID defaults Ironclad on pickup. Generate 5 Common,5 Uncommon,5 Rare (Cards15/3) reward cards, uniform odds, NoRarityModification|NoCardPoolModifications; concatenate in that order; choose 0..15 using grid reward selector; add each chosen to Deck.

Fixed dependencies: Ironclad character fallback. Random-output boundary: Selected character pool; generation flags do not disable upgrade rolls or all modify hooks..

Regression requirements: All character variants, null fallback, select none/some/all; reward RNG and upgrade; selection/add order and actual flags.

### RELIC.SILKEN_TRESS

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/SilkenTress.cs:1–84` (SHA-256 in JSON).

Acquisition loses all current gold. Late reward-options modifier applies only owner, IsCardReward flag, unused: for each Glam-compatible offer clone card then enchant Glam1 and record modification; returns true even if no compatible options. AfterModifyingCardRewardOptions sets saved IsUsed, disables status.

Fixed dependencies: Glam. Random-output boundary: none.

Regression requirements: No-compatible reward still consumes use; wrong-source flag does not; first/next reward, cloned identity and enchant eligibility, gold loss hooks.

### RELIC.SPARKLING_ROUGE

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/SparklingRouge.cs:1–38` (SHA-256 in JSON).

After owner block cleared on turn3, apply Strength1 then Dexterity1.

Fixed dependencies: StrengthPower, DexterityPower. Random-output boundary: none.

Regression requirements: Turn2/3/4, ordering with block-cleared hooks and repeated clear events.

### RELIC.VEXING_PUZZLEBOX

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/VexingPuzzlebox.cs:1–26` (SHA-256 in JSON).

After own player turn start on turn1, distinct combat-generation sample1 from unlocked character card pool using CombatCardGeneration; set free this turn, add generated Hand.

Fixed dependencies: shared commands only. Random-output boundary: Character pool after native combat/player-count filters..

Regression requirements: Full pool identity/RNG, hand cap, start ordering, generated nonroot result explicit unsupported effect, generated/root upgraded variants.

### RELIC.WHISPERING_EARRING

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/WhisperingEarring.cs:1–107` (SHA-256 in JSON).

Modify owner max energy +1. First-turn AfterAutoPrePlayPhaseEnteredLate installs VakuuCardSelector; at most13 iterations: stop if combat ending, owner ready-to-end, turn changed, or no CanPlay card. Choose first playable Hand card, target leftmost hittable enemy/owner for AnyPlayer/random eligible other-player ally via CombatTargets; SpendResources then AutoPlay Default skipXCapture:true. Dispose selector; after any plays emit warning if cap reached else approval.

Fixed dependencies: VakuuCardSelector. Random-output boundary: Existing hand cards only; ally branch is unreachable in scoped single-player..

Regression requirements: Resource/X capture and play order; 0/1/13 plays; turn-changing/ending card; nested selection uses top-left up to max and first reward; restore selector stack; no manual-choice substitution.

### POTION.BLOCK_POTION

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/BlockPotion.cs:1–31` (SHA-256 in JSON).

Gain 12 Unpowered block on AnyPlayer target.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Legal target and invalid target; effect order; power stacking/removal and interaction with supported hooks; consumed source/history

### POTION.COLORLESS_POTION

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/ColorlessPotion.cs:1–35` (SHA-256 in JSON).

Generate 3 distinct combat-eligible unlocked colorless cards using CombatCardGeneration; choose canSkip, if chosen set free this turn then add generated card to Hand (base.Owner).

Fixed dependencies: shared commands only. Random-output boundary: Full native eligible card pool; effect implementations outside roots/semantic closure remain unsupported.

Regression requirements: Each offer and skip, exact RNG/pool/filters, hand cap/free-cost lifetime, consumed potion retained through suspended choice, clone/restore/resume

### POTION.CURE_ALL

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/CureAll.cs:1–38` (SHA-256 in JSON).

Gain 1 energy on target Player THEN draw2. Name does not imply cleanse/heal.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Legal target and invalid target; effect order; power stacking/removal and interaction with supported hooks; consumed source/history

### POTION.DEXTERITY_POTION

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/DexterityPotion.cs:1–31` (SHA-256 in JSON).

Apply Dexterity2 to AnyPlayer target, source owner creature.

Fixed dependencies: DexterityPower. Random-output boundary: none.

Regression requirements: Legal target and invalid target; effect order; power stacking/removal and interaction with supported hooks; consumed source/history

### POTION.DUPLICATOR

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/Duplicator.cs:1–24` (SHA-256 in JSON).

Apply Duplication1 to AnyPlayer target with target as applier.

Fixed dependencies: DuplicationPower. Random-output boundary: none.

Regression requirements: Legal target and invalid target; effect order; power stacking/removal and interaction with supported hooks; consumed source/history

### POTION.EXPLOSIVE_AMPOULE

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/ExplosiveAmpoule.cs:1–38` (SHA-256 in JSON).

Capture current HittableEnemies list; deal 10 Unpowered damage to those targets, dealer owner creature.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Legal target and invalid target; effect order; power stacking/removal and interaction with supported hooks; consumed source/history

### POTION.GAMBLERS_BREW

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/GamblersBrew.cs:1–30` (SHA-256 in JSON).

From target hand select 0..999999999 discard candidates with potion source; then DiscardAndDraw selected list and list.Count.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Zero/all/partial hand selection and reshuffle; retained consumed potion as selection source

### POTION.HEART_OF_IRON

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/HeartOfIron.cs:1–35` (SHA-256 in JSON).

Apply Plating7 to AnyPlayer target, source owner creature.

Fixed dependencies: PlatingPower. Random-output boundary: none.

Regression requirements: Legal target and invalid target; effect order; power stacking/removal and interaction with supported hooks; consumed source/history

### POTION.LIQUID_BRONZE

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/LiquidBronze.cs:1–31` (SHA-256 in JSON).

Apply Thorns3 to AnyPlayer target, source owner creature.

Fixed dependencies: ThornsPower. Random-output boundary: none.

Regression requirements: Legal target and invalid target; effect order; power stacking/removal and interaction with supported hooks; consumed source/history

### POTION.POTION_OF_BINDING

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/PotionOfBinding.cs:1–47` (SHA-256 in JSON).

Capture HittableEnemies; apply Weak using VulnerablePower IntValue(1), THEN Vulnerable using WeakPower IntValue(1). Preserve native cross-key lookup/order.

Fixed dependencies: WeakPower, VulnerablePower. Random-output boundary: none.

Regression requirements: Legal target and invalid target; effect order; power stacking/removal and interaction with supported hooks; consumed source/history

### POTION.SKILL_POTION

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/SkillPotion.cs:1–36` (SHA-256 in JSON).

Generate 3 distinct combat-eligible unlocked target character Skills using CombatCardGeneration; choose canSkip; if chosen set free this turn then add generated card to Hand (base.Owner).

Fixed dependencies: shared commands only. Random-output boundary: Full native eligible card pool; effect implementations outside roots/semantic closure remain unsupported.

Regression requirements: Each offer and skip, exact RNG/pool/filters, hand cap/free-cost lifetime, consumed potion retained through suspended choice, clone/restore/resume

### POTION.STABLE_SERUM

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/StableSerum.cs:1–31` (SHA-256 in JSON).

Apply RetainHand2 to AnyPlayer target, source owner creature.

Fixed dependencies: RetainHandPower. Random-output boundary: none.

Regression requirements: Legal target and invalid target; effect order; power stacking/removal and interaction with supported hooks; consumed source/history

### POTION.STRENGTH_POTION

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/StrengthPotion.cs:1–34` (SHA-256 in JSON).

Apply Strength2 to AnyPlayer target, source owner creature.

Fixed dependencies: StrengthPower. Random-output boundary: none.

Regression requirements: Legal target and invalid target; effect order; power stacking/removal and interaction with supported hooks; consumed source/history

### POTION.SWIFT_POTION

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/SwiftPotion.cs:1–30` (SHA-256 in JSON).

Draw3 for AnyPlayer target Player.

Fixed dependencies: shared commands only. Random-output boundary: none.

Regression requirements: Legal target and invalid target; effect order; power stacking/removal and interaction with supported hooks; consumed source/history

### POTION.WEAK_POTION

Source: `build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/WeakPotion.cs:1–34` (SHA-256 in JSON).

Apply Weak3 to AnyEnemy target, source owner creature.

Fixed dependencies: WeakPower. Random-output boundary: none.

Regression requirements: Legal target and invalid target; effect order; power stacking/removal and interaction with supported hooks; consumed source/history

## Limits and remaining evidence

- Per-model source inspection does not prove dispatch ordering among different models; parent shared-hook audit is required.
- No live branch probes were run; regression lists are requirements.
- Source paths and SHA-256 permit local matching; game decompilation is not redistributed.
- Semantic dependencies listed are direct; fixed transitive closure is owned by parent audit.

The lifetime audit owns how saved and unsaved runtime fields survive clone/restore, including per-combat latches, counters and suspended sources. Presentation-only activation flags must be classified against actual public observations; do not discard them solely because they are unsaved. The public-visibility/English audits own rendering certification.

## Fixed non-root and helper evidence

### POWER.RETAIN_HAND_POWER

`build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/RetainHandPower.cs:1–35`. StableSerum fixed effect: Counter buff, owner ShouldFlush=false; AfterSideTurnEnd when owner participates decrement power. Does not itself override Ethereal exhaustion.

Fixed dependencies: shared operations / existing runtime objects. Regression requirements: Retain two owner turns, stack durations, end-side order, Ethereal cards, clone countdown.

### ENCHANTMENT.MOMENTUM

`build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Enchantments/Momentum.cs:1–48`. PunchDagger fixed effect: Attack-only eligibility; private ExtraDamage accumulator; each OnPlay adds enchantment Amount; powered-attack EnchantDamageAdditive returns accumulator, other props zero. This definition has no explicit reset hook.

Fixed dependencies: shared operations / existing runtime objects. Regression requirements: First/repeated/replayed attack, multi-hit application, non-powered damage, clone/combat-copy accumulator lifetime.

### VakuuCardSelector

`build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/VakuuCardSelector.cs:1–28`. WhisperingEarring fixed selector policy: take first maxSelect options; rewards choose first option card or null. Scoped selector must survive nested effects and be restored when disposed.

Fixed dependencies: shared operations / existing runtime objects. Regression requirements: Nested discard/exhaust selections, optional choices, max=0 and no options, scope restoration.

### CARD.SUPPRESS

`build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Suppress.cs:1–56`. ArchaicTooth Neutralize replacement: Ancient Attack energy0 AnyEnemy Innate; damage11 then Weak3; upgrade damage+6 and Weak+2. Required conditional on legal Silent-owner variant; fixed mapping rather than random output.

Fixed dependencies: POWER.WEAK_POWER. Regression requirements: Upgraded17/Weak5, lethal attack before debuff, innate placement, enchantment-copy transformation.

### CARD.PROTECTOR

`build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Protector.cs:1–52`. ArchaicTooth Unleash replacement: Ancient Attack energy1 AnyEnemy OstyAttack; calculated damage base10 plus ExtraDamage1 times live Osty.MaxHp (zero if absent/dead), FromOsty. OnPlay skips attack if Osty missing; upgrade energy-1 and calculation base+5. Required conditional on Necrobinder-owner variant.

Fixed dependencies: MONSTER.OSTY. Regression requirements: Osty absent/dead/live with changing maxHP, upgrade energy0/base15, source-specific damage modifiers and enchantment copy.

### CARD.METEOR_SHOWER

`build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/MeteorShower.cs:1–48`. ArchaicTooth FallingStar replacement: Ancient Attack energy0 star2 AllEnemies; attack14 all opponents then Weak2 to current hittable enemies then Vulnerable2 to current hittable enemies; upgrade damage+7 only. Required conditional on Regent-owner variant.

Fixed dependencies: POWER.WEAK_POWER, POWER.VULNERABLE_POWER. Regression requirements: Star payment, partial kills change post-hit targets, upgrade21, ordered debuff blockers.

### CARD.QUADCAST

`build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Quadcast.cs:1–42`. ArchaicTooth Dualcast replacement: Ancient Skill energy1 Self; no-op if no orbs; four EvokeNext calls, dequeue=false first3 true fourth. Upgrade energy-1. Required conditional on Defect-owner variant; existing front-orb effects and orbit queue/hook semantics mandatory. No concrete orb model is generated by this card.

Fixed dependencies: shared operations / existing runtime objects. Regression requirements: No-orb case, same orb evoked3 then removed fourth, death/combat-end between repeats, mutable orb effects/after-evoke hooks, upgraded cost0.

### CARD.DOUBT

`build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Doubt.cs:1–38`. ArchaicTooth replacement helper fallback for unmapped source ID; normal first-matching starter lookup makes this fallback defensive, not a randomly selected legal offer. Unplayable Curse energy-1, no upgrade; turn-end in Hand apply Weak1 and set SkipNextDurationTick only when no Weak existed and resulting power non-null.

Fixed dependencies: POWER.WEAK_POWER. Regression requirements: New/existing Weak, blocked application, tick skipping and duplicate curses. Do not invent a normal no-starter acquisition path.

### MONSTER.OSTY

`build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Osty.cs:1–79`. Protector dependency: pet monster initial HP1, self-loop NOTHING_MOVE; IsHealthBarVisible tracks alive; CheckMissingWithAnim returns owner.IsOstyMissing. Player.cs:125-142 retains dead pet identity and defines missing as absent OR dead. Summoning/full pet lifecycle is a shared capability; this definition does not summon Osty.

Fixed dependencies: shared operations / existing runtime objects. Regression requirements: Distinguish absent/dead/live retained pet, attack dealer/ownership and maxHP calculation.

### OrbCmd.EvokeNext

`build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Commands/OrbCmd.cs:1–171`. Quadcast operation dependency: select current first orb every call; Evoke returns immediately if combat ending/queue empty. On dequeue remove orb before Evoke; push source context, invoke orb effect, pop, AfterOrbEvoked, then RemoveInternal if removed and player combat still exists. It dispatches the existing orb model rather than generating a model.

Fixed dependencies: shared operations / existing runtime objects. Regression requirements: Mutating orb queue between calls, effect death, source retained across awaited hooks.

`Imbued.cs:20–25` autoplays its card during the ordinary first-turn preplay hook. WhisperingEarring explicitly uses the Late hook so such prior work finishes first. This ordering obligation does **not** make Imbued a fixed generated/applied dependency of WhisperingEarring; it must not be added merely because a source comment mentions it. ArchaicTooth’s character-specific transformation outputs are fixed legal branches, not broad random-pool outcomes. The first Ironclad route exercises only Bash→Break; that fact does not waive the other legal owner variants. Parent closure inventory must include these conditional model/capability requirements.
