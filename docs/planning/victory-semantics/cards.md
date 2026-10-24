# Card, enchantment and affliction semantic audit

Static source reading for the pinned v0.111.0 victory scope. All 71 card, three enchantment and two affliction root files were inspected, including offers not selected in the recording. This records implementation requirements and regression scenarios, not passing runtime tests or simulator support. `cards.json` holds every root, exact constructor parameters (cost/type/rarity/target), source hash, member/upgrade anchors, fixed model dependencies, random-output distinction and required future-behavior fields. Sources remain local decompilation; no game assemblies or full decompiled sources are published. Line ranges refer to those hashed files.

## Per-root inventory

Canonical numeric values below are pre-upgrade; dynamic modifiers and shared command hooks still apply. “Base state” includes instance identity, owner, scope, pile, keywords, energy modifiers, upgrade level, mutable DynamicVars and attachment state; it does not mean a Model ID is sufficient. All cards require clone/checkpoint preservation of any pending effect/selection and its source.

### CARD.APPARITION

Source: `MegaCrit.Sts2.Core.Models.Cards/Apparition.cs:1–39` (SHA-256 in `cards.json`).

Apply IntangiblePower 1 to owner. Ethereal and Exhaust.

Upgrade: Remove Ethereal; Exhaust remains.
Branches: Any legal owner state; keyword and power-duration semantics apply.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: IntangiblePower
Random outcomes: none
Required regressions: Base vs upgraded hand at turn end; played vs ethereal-exhausted; stacked Intangible interactions.

### CARD.ARMAMENTS

Source: `MegaCrit.Sts2.Core.Models.Cards/Armaments.cs:1–40` (SHA-256 in `cards.json`).

Gain block5, then base selects one upgradable hand card and upgrades if non-null; upgraded upgrades all currently upgradable cards in hand.

Upgrade: Branch to upgrading all eligible hand cards.
Branches: Empty/no-upgradable hand, one/many eligible, base selection; no selected card still preserves block.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Suspend after block before choice; clone/restore/resume; all-target eligibility/order and zero-candidate path.

### CARD.AUTOMATION

Source: `MegaCrit.Sts2.Core.Models.Cards/Automation.cs:1–32` (SHA-256 in `cards.json`).

Apply AutomationPower equal to EnergyVar1.

Upgrade: Energy cost -1 (1 to0).
Branches: AutomationPower is instanced: each owned card drawn decrements its internal cardsLeft counter from10; reaching0 grants Amount energy and resets counter10. fromHandDraw is not used as a filter; generated entry alone is not a draw.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: AutomationPower
Random outcomes: none
Required regressions: Ordinary hand draw and effect draw both count;9th/10th/11th draw; separate power instances/counters; clone/restore mid-counter; generated entry without draw does not count.

### CARD.BASH

Source: `MegaCrit.Sts2.Core.Models.Cards/Bash.cs:1–43` (SHA-256 in `cards.json`).

Attack selected target8, then apply Vulnerable2 to same target without card-local alive guard.

Upgrade: Damage+2; Vulnerable+1.
Branches: Lethal hit still reaches PowerCmd application; shared command determines resulting applicability.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: VulnerablePower
Random outcomes: none
Required regressions: Nonlethal/lethal, Artifact, application hooks after attack.

### CARD.BLOODLETTING

Source: `MegaCrit.Sts2.Core.Models.Cards/Bloodletting.cs:1–39` (SHA-256 in `cards.json`).

Deal self HP-loss3 with Unblockable|Unpowered|Move, then gain energy2.

Upgrade: Energy+1.
Branches: Zero/low HP, damage prevention/modification, death and shared command interruption semantics.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Block is bypassed; unpowered differs from attack; self-damage hooks; low-HP path.

### CARD.BLOOD_WALL

Source: `MegaCrit.Sts2.Core.Models.Cards/BloodWall.cs:1–47` (SHA-256 in `cards.json`).

Deal self HP-loss2 with Unblockable|Unpowered|Move, then gain block16.

Upgrade: Block+4.
Branches: Self-damage hooks precede block; low HP/ending shared command behavior.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Self-damage-triggered effects and block order; base/upgraded.

### CARD.BLUDGEON

Source: `MegaCrit.Sts2.Core.Models.Cards/Bludgeon.cs:1–36` (SHA-256 in `cards.json`).

Attack selected target32.

Upgrade: Damage+10.
Branches: Any legal target; shared attack pipeline.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Block/Strength/Vulnerable/Weak/Intangible; lethal target.

### CARD.BODY_SLAM

Source: `MegaCrit.Sts2.Core.Models.Cards/BodySlam.cs:1–42` (SHA-256 in `cards.json`).

Attack for CalculatedDamage with base0 plus extra1 times owner current Block.

Upgrade: Energy cost -1 (1 to0).
Branches: Zero and changing block; calculated damage/ordinary damage modifiers remain distinct.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Block sampled at calculation; Strength and Weak; no accidental block consumption.

### CARD.BOLAS

Source: `MegaCrit.Sts2.Core.Models.Cards/Bolas.cs:1–55` (SHA-256 in `cards.json`).

Attack selected target3; BeforeHandDraw returns this instance to Hand if its finished play happened last player turn and it is not already in Hand.

Upgrade: Damage+1.
Branches: Owner-specific hook, last-player-turn history, any current pile/null vs already Hand.
State: Base card state plus historical finished-play reference; pile membership may be changed before draw.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Multiple turns and instances; hand capacity/movement; play history identity rather than Model ID.

### CARD.BRAND

Source: `MegaCrit.Sts2.Core.Models.Cards/Brand.cs:1–56` (SHA-256 in `cards.json`).

Self HP-loss1 unblocked/unpowered Move; select one hand card; exhaust if non-null; apply Strength1.

Upgrade: Strength+1.
Branches: Empty hand still reaches Strength; exhaust triggers may suspend or change state before Strength.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: StrengthPower
Random outcomes: none
Required regressions: Nested exhaust triggers; restore before selection; HP loss then exhaust then Strength.

### CARD.BREAK

Source: `MegaCrit.Sts2.Core.Models.Cards/Break.cs:1–46` (SHA-256 in `cards.json`).

Attack selected target20, then Vulnerable5.

Upgrade: Damage+10; Vulnerable+2.
Branches: Shared target/application semantics after lethal hit.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: VulnerablePower
Random outcomes: none
Required regressions: Lethal/nonlethal; Artifact; base/upgraded.

### CARD.BREAKTHROUGH

Source: `MegaCrit.Sts2.Core.Models.Cards/Breakthrough.cs:1–37` (SHA-256 in `cards.json`).

Self HP-loss1 with Unblockable|Unpowered|Move, then attack all opponents9.

Upgrade: Damage+4.
Branches: Hittable enemy set/shared attack pipeline; self-damage may trigger effects before attack.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Self-damage before AoE; multiple deaths/revival; no targets.

### CARD.BURN

Source: `MegaCrit.Sts2.Core.Models.Cards/Burn.cs:1–37` (SHA-256 in `cards.json`).

Unplayable non-upgradable Status; end-in-hand deals owner damage2 with Unpowered|Move, not Unblockable.

Upgrade: MaxUpgradeLevel0.
Branches: End-in-hand effect only; block can absorb damage.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Retained Burn vs exhaust/discard; block and damage hooks; multiple copies.

### CARD.BURNING_PACT

Source: `MegaCrit.Sts2.Core.Models.Cards/BurningPact.cs:1–39` (SHA-256 in `cards.json`).

Select one hand card, exhaust non-null selection, then draw2.

Upgrade: Draw+1.
Branches: Empty hand continues to draw; exhaustion hooks happen before draws.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Zero/one/many candidates; nested exhaustion; reshuffle/hand cap; resume after selection.

### CARD.CATASTROPHE

Source: `MegaCrit.Sts2.Core.Models.Cards/Catastrophe.cs:1–42` (SHA-256 in `cards.json`).

Repeat2: select first of a stable Shuffle-RNG shuffle of playable-keyword draw-pile cards; if none, shuffle all draw cards and select first; autoplay selected card if any.

Upgrade: Repeat+1.
Branches: Empty draw pile; only Unplayable cards; draw-pile changes after each autoplay; random target and nested prompts.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: Selects existing card instances; no new model pool expansion. Uses RunState.Rng.Shuffle.
Required regressions: Preserve two-stage selection/RNG, per-iteration reevaluation, autoplay of fallback Unplayable, empty pile.

### CARD.CINDER

Source: `MegaCrit.Sts2.Core.Models.Cards/Cinder.cs:1–46` (SHA-256 in `cards.json`).

Attack selected target18, then CombatCardSelection.NextItem over current Hand and exhaust selected card if any.

Upgrade: Damage+6.
Branches: Empty Hand; lethal attack then subsequent random exhaust; shared ending behavior.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: Existing hand card instance only; CombatCardSelection RNG.
Required regressions: Selection RNG and order, nested exhaust triggers, empty hand.

### CARD.CLUMSY

Source: `MegaCrit.Sts2.Core.Models.Cards/Clumsy.cs:1–20` (SHA-256 in `cards.json`).

Unplayable and Ethereal Curse.

Upgrade: MaxUpgradeLevel0.
Branches: In-hand turn-end ethereal exhaustion; removal/exhaust before that.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Correct keyword lifecycle and exhaustion hooks.

### CARD.COLOSSUS

Source: `MegaCrit.Sts2.Core.Models.Cards/Colossus.cs:1–43` (SHA-256 in `cards.json`).

Gain block4, then apply ColossusPower1.

Upgrade: Block+3.
Branches: ColossusPower halves powered-attack damage to owner only when non-null dealer has Vulnerable; its Amount decrements at Enemy side end. Block is gained before power application.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: ColossusPower
Random outcomes: none
Required regressions: Vulnerable dealer vs non-Vulnerable, unpowered damage, null dealer, multiple duration stacks and Enemy-side decrement.

### CARD.CONFLAGRATION

Source: `MegaCrit.Sts2.Core.Models.Cards/Conflagration.cs:1–47` (SHA-256 in `cards.json`).

Attack all opponents for damage2 with4 hits.

Upgrade: Hits+1.
Branches: Shared command handles hit order and targetability changes after deaths/revivals.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: AoE multi-hit interleaving, shields/retaliation/kill hooks, empty target set.

### CARD.CRIMSON_MANTLE

Source: `MegaCrit.Sts2.Core.Models.Cards/CrimsonMantle.cs:1–35` (SHA-256 in `cards.json`).

Apply CrimsonMantlePower7; if returned instance is non-null, IncrementSelfDamage on it.

Upgrade: Power amount+3.
Branches: Existing/new power and blocked/no instance; increment happens after apply.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: CrimsonMantlePower
Random outcomes: none
Required regressions: Repeated plays and stack amount vs self-damage counter; clone/restore.

### CARD.DEBT

Source: `MegaCrit.Sts2.Core.Models.Cards/Debt.cs:1–31` (SHA-256 in `cards.json`).

Unplayable non-upgradable Curse; end-in-hand lose min(10,current gold).

Upgrade: MaxUpgradeLevel0.
Branches: Gold0/<10/>=10; gold-loss hooks.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Multiple copies/order; never charge more current gold.

### CARD.DEFEND_IRONCLAD

Source: `MegaCrit.Sts2.Core.Models.Cards/DefendIronclad.cs:1–33` (SHA-256 in `cards.json`).

Gain block5; Defend tag.

Upgrade: Block+3.
Branches: Shared block modifiers.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Dexterity, Block hooks, Defend-tag consumers.

### CARD.DISMANTLE

Source: `MegaCrit.Sts2.Core.Models.Cards/Dismantle.cs:1–43` (SHA-256 in `cards.json`).

At play inspect whether selected target currently has Vulnerable; attack8 with2 hits if yes else1.

Upgrade: Damage+2.
Branches: Target power presence drives hit count once; glow queries any hittable vulnerable enemy.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: VulnerablePower
Random outcomes: none
Required regressions: No/present Vulnerable including during hit chain, target death, observation purity.

### CARD.DOMINATE

Source: `MegaCrit.Sts2.Core.Models.Cards/Dominate.cs:1–49` (SHA-256 in `cards.json`).

Apply Vulnerable1 to target; read resulting Vulnerable amount (or0); apply that much Strength to owner. Exhaust.

Upgrade: Vulnerable+1.
Branches: Existing stacks, Artifact prevented application, modified application. StrengthPerVulnerable dynamic var exists but code uses actual amount directly.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: VulnerablePower, StrengthPower
Random outcomes: none
Required regressions: No/existing Vulnerable, Artifact, hooks triggered between application and read.

### CARD.DRUM_OF_BATTLE

Source: `MegaCrit.Sts2.Core.Models.Cards/DrumOfBattle.cs:1–47` (SHA-256 in `cards.json`).

On play draw2. When this exact card is exhausted with non-null CombatState, GeneratePlayCount(combat,null) then gain energy2 once per generated play.

Upgrade: Energy+1.
Branches: Exhaust caused by Ethereal or other effects has no extra filter; play-count modifiers affect exhaust bonus.
State: Base card state and exhaustion event identity; GeneratePlayCount may consume modifier state.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Exhaust from non-play source, Glam/duplication interaction, restored retained card references.

### CARD.ENTROPY

Source: `MegaCrit.Sts2.Core.Models.Cards/Entropy.cs:1–32` (SHA-256 in `cards.json`).

Apply EntropyPower1.

Upgrade: Add Innate.
Branches: Power asks hand transformation choices at owner turn start.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: EntropyPower
Random outcomes: none
Required regressions: Turn-start choice and transform RNG, empty hand and replacement identity.

### CARD.FEEL_NO_PAIN

Source: `MegaCrit.Sts2.Core.Models.Cards/FeelNoPain.cs:1–38` (SHA-256 in `cards.json`).

Apply FeelNoPainPower3.

Upgrade: Amount+1.
Branches: Exhaustion triggers handled by power.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: FeelNoPainPower
Random outcomes: none
Required regressions: Self and other-card exhaustion, Ethereal, multiple stacks.

### CARD.FLAME_BARRIER

Source: `MegaCrit.Sts2.Core.Models.Cards/FlameBarrier.cs:1–45` (SHA-256 in `cards.json`).

Gain block12, then apply FlameBarrierPower4.

Upgrade: Block+4; retaliation+2.
Branches: FlameBarrierPower retaliates against non-null dealer after owner receives powered-attack damage even when the damage result is fully blocked; removes after the side opposite its owner ends.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: FlameBarrierPower
Random outcomes: none
Required regressions: Blocked and leaking multi-hit incoming attacks, no retaliation for unpowered damage, owner-relative expiry, stacked amounts.

### CARD.FRANTIC_ESCAPE

Source: `MegaCrit.Sts2.Core.Models.Cards/FranticEscape.cs:1–52` (SHA-256 in `cards.json`).

Find first enemy with SandpitPower; find its SandpitPower targeting owner; modify that amount+1 if found; then increase this instance cost+1 this combat.

Upgrade: MaxUpgradeLevel0.
Branches: Missing enemy/power or power targets another entity still increases cost; cannot be generated in combat.
State: CardEnergyCost combat modifier grows on every play.
Fixed model dependencies: SandpitPower
Random outcomes: none
Required regressions: Repeated use, cost persistence/reset, correct targeted sandpit identity.

### CARD.GOLD_AXE

Source: `MegaCrit.Sts2.Core.Models.Cards/GoldAxe.cs:1–41` (SHA-256 in `cards.json`).

Attack using calculated damage base0 plus extra1 times count of all combat finished-card-play history entries.

Upgrade: Add Retain.
Branches: History count is not restricted to owner/type/turn by this card.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Autoplays/replays count, current play not yet finished, history retained references.

### CARD.HAVOC

Source: `MegaCrit.Sts2.Core.Models.Cards/Havoc.cs:1–28` (SHA-256 in `cards.json`).

AutoPlayFromDrawPile count1 Top with forceExhaust true.

Upgrade: Energy cost -1 (1 to0).
Branches: Empty pile, draw-pile autoplay, automatic targeting, nested choice, forced exhaustion.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: No energy payment shortcut changes; stack resumed after nested selection; force-exhaust destination.

### CARD.HEADBUTT

Source: `MegaCrit.Sts2.Core.Models.Cards/Headbutt.cs:1–40` (SHA-256 in `cards.json`).

Attack selected target9; choose one discard-pile card; move non-null selection to top of Draw.

Upgrade: Damage+3.
Branches: Empty discard, selection after attack-trigger mutations.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Suspend after attack; stale choices; discard identity/order and top placement.

### CARD.HOWL_FROM_BEYOND

Source: `MegaCrit.Sts2.Core.Models.Cards/HowlFromBeyond.cs:1–45` (SHA-256 in `cards.json`).

On play attack all opponents18; on owner AfterAutoPostPlayPhaseEntered, if this instance is in Exhaust, autoplay it.

Upgrade: Damage+6.
Branches: Hook checks Exhaust membership, not printed keyword (card has no canonical Exhaust); can be exhausted by another effect.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Owner vs other turn hook, repeated turn autoplay from Exhaust, changed destination/empty enemies.

### CARD.IMPERVIOUS

Source: `MegaCrit.Sts2.Core.Models.Cards/Impervious.cs:1–33` (SHA-256 in `cards.json`).

Gain block30. Exhaust.

Upgrade: Block+10.
Branches: Shared block and exhaust pipeline.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Block then exhaust hooks and multi-play interactions.

### CARD.INFECTION

Source: `MegaCrit.Sts2.Core.Models.Cards/Infection.cs:1–41` (SHA-256 in `cards.json`).

Unplayable Status; end-in-hand deal owner damage3 with Unpowered|Move (not Unblockable).

Upgrade: MaxUpgradeLevel0.
Branches: Hand-only turn-end effect.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Block absorption and damage hooks; multiple statuses.

### CARD.INFLAME

Source: `MegaCrit.Sts2.Core.Models.Cards/Inflame.cs:1–45` (SHA-256 in `cards.json`).

Apply Strength2 to owner.

Upgrade: Strength+1.
Branches: Shared power stacking/application.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: StrengthPower
Random outcomes: none
Required regressions: Negative/existing Strength and modifiers.

### CARD.IRON_WAVE

Source: `MegaCrit.Sts2.Core.Models.Cards/IronWave.cs:1–41` (SHA-256 in `cards.json`).

Gain block5 first, then attack target5.

Upgrade: Block+2 and Damage+2.
Branches: Block hooks may affect subsequent attack.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Block-before-attack ordering; retaliation and death.

### CARD.JACK_OF_ALL_TRADES

Source: `MegaCrit.Sts2.Core.Models.Cards/JackOfAllTrades.cs:1–39` (SHA-256 in `cards.json`).

Generate distinct colorless cards count1 into Hand, excluding JackOfAllTrades; Exhaust.

Upgrade: Generated count+1.
Branches: Unlocked/player-count/combat-generation filters; finite pool; Hand capacity.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: Unlocked ColorlessCardPool minus JackOfAllTrades; CardFactory.GetDistinctForCombat; CombatCardGeneration RNG. Non-root outcomes keep native probability and may remain explicitly unsupported effects.
Required regressions: Exact pool/ordering/RNG; independent generated identities; hand overflow.

### CARD.JUGGLING

Source: `MegaCrit.Sts2.Core.Models.Cards/Juggling.cs:1–26` (SHA-256 in `cards.json`).

Apply JugglingPower1.

Upgrade: Add Innate.
Branches: Power counts attacks and creates clones of third attack.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: JugglingPower
Random outcomes: none
Required regressions: Applied after attacks already played, third vs fourth attack, generated clone mutations.

### CARD.MAD_SCIENCE

Source: `MegaCrit.Sts2.Core.Models.Cards/MadScience.cs:1–303` (SHA-256 in `cards.json`).

Legal Attack: damage12 (3 hits for Violence); Skill: block8; Power: Expertise Strength2 then Dexterity2, Curious cost-reduction1 or Improvement1. After Attack/Skill base effect, riders run: Sapping Weak2 then Vulnerable2; Choking Strangle6; Energized energy2; Wisdom draw3; Chaos one generated own-pool card free this turn.

Upgrade: Add Innate.
Branches: Event legal matrix: Attack=Sapping/Violence/Choking; Skill=Energized/Wisdom/Chaos; Power=Expertise/Curious/Improvement. Type/rider must persist; reject invalid restored combinations per supported input contract; source switch includes None interim event previews.
State: Saved TinkerTimeType, saved TinkerTimeRider; _mockedChaosCard is test-only injection, not native gameplay state.
Fixed model dependencies: WeakPower, VulnerablePower, StranglePower, StrengthPower, DexterityPower, CuriousPower, ImprovementPower
Random outcomes: Chaos: unlocked owner Character.CardPool, GetDistinctForCombat count1, CombatCardGeneration RNG; generated card SetToFreeThisTurn. Do not make mocked test card a gameplay dependency.
Required regressions: All nine legal variants base/upgraded; nested Wisdom draw/generation hooks; Chaos RNG; type-dependent target/action observation; clone/restore type and rider.

### CARD.MANGLE

Source: `MegaCrit.Sts2.Core.Models.Cards/Mangle.cs:1–48` (SHA-256 in `cards.json`).

Attack selected target20, then apply ManglePower10.

Upgrade: Damage+6; temporary Strength loss+5.
Branches: TemporaryStrengthPower shared superclass controls negative Strength application and reversal.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: ManglePower
Random outcomes: none
Required regressions: Artifact, temporary negative-strength stacking, end-side reversal after power removal.

### CARD.MOLTEN_FIST

Source: `MegaCrit.Sts2.Core.Models.Cards/MoltenFist.cs:1–49` (SHA-256 in `cards.json`).

Attack target10; after hit if target IsAlive read Vulnerable, else use0; apply that amount again if positive. Exhaust.

Upgrade: Damage+4.
Branches: Explicit alive guard unlike Bash; absent Vulnerable no application.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: VulnerablePower
Random outcomes: none
Required regressions: Dead/reviving/live target; doubling existing stacks; Artifact on added amount.

### CARD.OFFERING

Source: `MegaCrit.Sts2.Core.Models.Cards/Offering.cs:1–41` (SHA-256 in `cards.json`).

Self HP-loss6 Unblockable|Unpowered|Move, gain energy2, then draw3. Exhaust.

Upgrade: Draw+2.
Branches: Self-damage hooks precede energy and draw; low-HP/ending shared behavior.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: HP/energy/draw/exhaust order; full Hand; death/revival.

### CARD.ONE_TWO_PUNCH

Source: `MegaCrit.Sts2.Core.Models.Cards/OneTwoPunch.cs:1–31` (SHA-256 in `cards.json`).

Apply OneTwoPunchPower1.

Upgrade: Power amount+1.
Branches: Power adds one extra attack play, decrements after modifying play count and removes at owner side end.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: OneTwoPunchPower
Random outcomes: none
Required regressions: Stack counts vs replay counts, Glam/duplication, expiration.

### CARD.PACTS_END

Source: `MegaCrit.Sts2.Core.Models.Cards/PactsEnd.cs:1–46` (SHA-256 in `cards.json`).

Attack all opponents18 only when Exhaust-pile card count>=3 at play time.

Upgrade: Damage+6.
Branches: Below threshold does no attack but is still a legal card play; threshold evaluated dynamically.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: 2/3 cards, replay with changed exhaust count, all dead but revivable enemies.

### CARD.PERFECTED_STRIKE

Source: `MegaCrit.Sts2.Core.Models.Cards/PerfectedStrike.cs:1–51` (SHA-256 in `cards.json`).

Attack for calculated base6 plus extra2 times number of Strike-tagged cards in PlayerCombatState.AllCards. This card has Strike tag.

Upgrade: ExtraDamage multiplier+1 (2 to3).
Branches: Counts all combat cards, not just deck/hand; animation-only damage>12 branch is not gameplay.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Generated/exhausted Strike-tagged instances, multiple tags, calculation modifiers.

### CARD.PILLAGE

Source: `MegaCrit.Sts2.Core.Models.Cards/Pillage.cs:1–40` (SHA-256 in `cards.json`).

Attack target6; do draw one at least once, continuing while returned card is Attack and Hand is below max.

Upgrade: Damage+3.
Branches: Stop on null, non-Attack, or Hand cap; draw hooks/reshuffle apply each iteration.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: First non-Attack vs Attack chain, full Hand, empty piles, generated/draw hooks.

### CARD.POMMEL_STRIKE

Source: `MegaCrit.Sts2.Core.Models.Cards/PommelStrike.cs:1–41` (SHA-256 in `cards.json`).

Attack target9 then draw1. Strike tag.

Upgrade: Damage+1; draw+1.
Branches: Draw occurs after attack triggers.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Lethal/nonlethal draw ordering; max Hand and reshuffle; tag consumers.

### CARD.PURITY

Source: `MegaCrit.Sts2.Core.Models.Cards/Purity.cs:1–38` (SHA-256 in `cards.json`).

Select 0..3 hand cards; sequentially exhaust selected cards. Retain and Exhaust.

Upgrade: Max selected+2.
Branches: Zero selection is legal; ordered selected set; nested exhaust triggers; fewer candidates.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Zero/partial/max selection, confirm semantics, parent/sibling checkpoint independence.

### CARD.SECOND_WIND

Source: `MegaCrit.Sts2.Core.Models.Cards/SecondWind.cs:1–46` (SHA-256 in `cards.json`).

Snapshot list of non-Attack hand cards; for each exhaust it then gain block5.

Upgrade: Block+2.
Branches: Snapshot excludes later generated cards; each exhaustion precedes its block award.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: 0/1/many non-Attacks, exhaust-generated card, trigger ordering and death.

### CARD.SETUP_STRIKE

Source: `MegaCrit.Sts2.Core.Models.Cards/SetupStrike.cs:1–45` (SHA-256 in `cards.json`).

Attack target7, then apply SetupStrikePower3. Strike tag.

Upgrade: Damage+2; temporary Strength+1.
Branches: Temporary Strength apply/stack/reversal handled by superclass.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: SetupStrikePower
Random outcomes: none
Required regressions: Attack-before-Strength, multi-play, side-end reversal.

### CARD.SHOCKWAVE

Source: `MegaCrit.Sts2.Core.Models.Cards/Shockwave.cs:1–48` (SHA-256 in `cards.json`).

For each current HittableEnemy in order apply Weak3 then Vulnerable3. Exhaust.

Upgrade: Amount+2 for both.
Branches: Power hooks interleave by enemy; not two global passes.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: WeakPower, VulnerablePower
Random outcomes: none
Required regressions: Artifact1/2, Vicious-trigger draw mid-loop, target disappearance.

### CARD.SHRUG_IT_OFF

Source: `MegaCrit.Sts2.Core.Models.Cards/ShrugItOff.cs:1–36` (SHA-256 in `cards.json`).

Gain block8 then draw1.

Upgrade: Block+3.
Branches: Draw hooks after block.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Block-trigger effects before draw; full Hand/empty piles.

### CARD.SLIMED

Source: `MegaCrit.Sts2.Core.Models.Cards/Slimed.cs:1–35` (SHA-256 in `cards.json`).

Draw1. Exhaust; Status with TargetType.None.

Upgrade: MaxUpgradeLevel0.
Branches: Playable status unlike Burn/Infection; no target.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Action eligibility and cost1, draw then Exhaust hooks.

### CARD.SPLASH

Source: `MegaCrit.Sts2.Core.Models.Cards/Splash.cs:1–66` (SHA-256 in `cards.json`).

Generate3 distinct Attack offers from unlocked other-character pools (remove own only if >1 unlocked pools); if upgraded upgrade each offer before choose; optional choose/skip; chosen card becomes free this turn and enters Hand.

Upgrade: Upgrade all offers before choice.
Branches: Single unlocked pool fallback; skip creates no chosen card; actual generated models may be outside roots.
State: _mockGeneratedCard is test-only injection; pending generated offer identities must be retained by continuation.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: Unlocked character card pools, Attack filter, GetDistinctForCombat count3, CombatCardGeneration RNG; additional non-root effects explicitly unsupported until implemented.
Required regressions: All pool filters/RNG, skip, upgrade before selection, hand overflow and free-cost lifetime.

### CARD.SPOILS_MAP

Source: `MegaCrit.Sts2.Core.Models.Cards/SpoilsMap.cs:1–125` (SHA-256 in `cards.json`).

Non-upgradable Unplayable Quest. AfterCreated sets act index1. While in Deck for matching act, replace map with SpoilsActMap; late map hook remembers first Treasure coord; after-map hook attaches quest. Removal hook removes quest only in current matching act with coord. Quest completion grants gold600, completes quest, removes card from Deck, returns gold value.

Upgrade: MaxUpgradeLevel0.
Branches: Missing Deck membership, other act, missing treasure/coord/map point; removal before/after target act.
State: Saved SpoilsActIndex, runtime SpoilsCoord; map point quest reference/ownership.
Fixed model dependencies: SpoilsActMap
Random outcomes: none
Required regressions: Alternate map exact topology/RNG; attach/remove quest; complete once; no deck card left after reward.

### CARD.STAMPEDE

Source: `MegaCrit.Sts2.Core.Models.Cards/Stampede.cs:1–31` (SHA-256 in `cards.json`).

Apply StampedePower1.

Upgrade: Energy cost -1 (2 to1).
Branches: At owner auto-post-play phase chooses playable Attack in Hand per stack with Shuffle RNG and autoplays.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: StampedePower
Random outcomes: none
Required regressions: Nested choices, shrinking/changed Hand, no eligible Attack, multi-stack.

### CARD.STRIKE_IRONCLAD

Source: `MegaCrit.Sts2.Core.Models.Cards/StrikeIronclad.cs:1–41` (SHA-256 in `cards.json`).

Attack target6. Strike tag.

Upgrade: Damage+3.
Branches: Shared attack pipeline.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Tag consumers and general damage/target legality.

### CARD.SWORD_BOOMERANG

Source: `MegaCrit.Sts2.Core.Models.Cards/SwordBoomerang.cs:1–36` (SHA-256 in `cards.json`).

Attack random opponents damage3 with3 hits.

Upgrade: Hits+1.
Branches: Random target policy delegated to AttackCommand; targets may change after each hit.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Per-hit RNG, one/many/zero targets, death/revival target exclusion.

### CARD.TAUNT

Source: `MegaCrit.Sts2.Core.Models.Cards/Taunt.cs:1–44` (SHA-256 in `cards.json`).

Gain block6 then apply Vulnerable1 to target.

Upgrade: Block+1; Vulnerable+1.
Branches: Targeted Skill still requires enemy; block before power hooks.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: VulnerablePower
Random outcomes: none
Required regressions: Target legality; Artifact; block-before-Vicious draw.

### CARD.TEAR_ASUNDER

Source: `MegaCrit.Sts2.Core.Models.Cards/TearAsunder.cs:1–47` (SHA-256 in `cards.json`).

Attack target5 with hit count 1 + number of DamageReceived history entries whose receiver is owner and UnblockedDamage>0.

Upgrade: Damage+2.
Branches: History count is events, not HP amount, not restricted to current turn or attack damage.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Self-damage/retaliation/multiple hits, blocked0 event excluded, history across turns.

### CARD.THE_GAMBIT

Source: `MegaCrit.Sts2.Core.Models.Cards/TheGambit.cs:1–33` (SHA-256 in `cards.json`).

Gain block50 then apply TheGambitPower1.

Upgrade: Block+25.
Branches: Power kills owner only after positive unblocked powered-attack damage; not arbitrary HP loss.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: TheGambitPower
Random outcomes: none
Required regressions: Blocked attack, self-loss, unpowered damage, first leaked powered attack.

### CARD.THRASH

Source: `MegaCrit.Sts2.Core.Models.Cards/Thrash.cs:1–87` (SHA-256 in `cards.json`).

Attack target for2 hits of damage4; randomly choose current Hand Attack using CombatCardSelection. If selected, get CalculatedDamage(null), else Damage base, else OstyDamage base, else0; apply Hook.ModifyDamage(All) to captured damage, add result to this Damage.BaseValue and ExtraDamage, then exhaust chosen card.

Upgrade: Damage+2; AfterDowngraded re-adds accumulated ExtraDamage to rebuilt DynamicVar.
Branches: No eligible Attack; multiple damage-var shapes; targetless calculation; exhaustion hooks run after accumulation.
State: Private decimal _extraDamage plus mutable Damage.BaseValue; both are future-behavior state.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: Existing Hand Attack selected by CombatCardSelection; no generated model expansion.
Required regressions: Repeat plays and downgrade preserve growth; Strength modifier captured; wrong-use of expected damage; clone/restore accumulated damage.

### CARD.THUNDERCLAP

Source: `MegaCrit.Sts2.Core.Models.Cards/Thunderclap.cs:1–40` (SHA-256 in `cards.json`).

Attack all opponents4, then apply Vulnerable1 to current HittableEnemies.

Upgrade: Damage+3.
Branches: Post-attack hittable set may differ from attacked set.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: VulnerablePower
Random outcomes: none
Required regressions: Lethal/nonlethal mixed targets, revival and Artifact.

### CARD.TREMBLE

Source: `MegaCrit.Sts2.Core.Models.Cards/Tremble.cs:1–37` (SHA-256 in `cards.json`).

Apply Vulnerable3 to selected target. Exhaust.

Upgrade: Vulnerable+1.
Branches: Shared power hooks then exhaust.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: VulnerablePower
Random outcomes: none
Required regressions: Artifact and Vicious draw followed by Exhaust triggers.

### CARD.TRUE_GRIT

Source: `MegaCrit.Sts2.Core.Models.Cards/TrueGrit.cs:1–51` (SHA-256 in `cards.json`).

Gain block7; base chooses random Hand card with CombatCardSelection and exhausts if non-null; upgraded asks hand selection1 then exhausts non-null choice.

Upgrade: Block+2 and random-to-selected branch.
Branches: Empty Hand, random vs selection; gain block before either branch.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: Base only selects existing Hand instance using CombatCardSelection; upgraded uses choice.
Required regressions: Base RNG; upgraded pending choice restore; exhaust-trigger interaction.

### CARD.TWIN_STRIKE

Source: `MegaCrit.Sts2.Core.Models.Cards/TwinStrike.cs:1–36` (SHA-256 in `cards.json`).

Attack target damage5 with2 hits. Strike tag.

Upgrade: Damage+2.
Branches: Shared per-hit damage/target pipeline.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Intermediate death/revival/retaliation; tag consumers.

### CARD.ULTIMATE_STRIKE

Source: `MegaCrit.Sts2.Core.Models.Cards/UltimateStrike.cs:1–35` (SHA-256 in `cards.json`).

Attack target14. Strike tag.

Upgrade: Damage+6.
Branches: Shared attack pipeline.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: General damage hooks, Strike-tag counts.

### CARD.UNRELENTING

Source: `MegaCrit.Sts2.Core.Models.Cards/Unrelenting.cs:1–38` (SHA-256 in `cards.json`).

Attack target14 then apply FreeAttackPower1.

Upgrade: Damage+6.
Branches: FreeAttack cost-late modification and use occur only for owner Attack in Hand/Play.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: FreeAttackPower
Random outcomes: none
Required regressions: Next Attack vs Skill, stacked free counts, out-of-Hand autoplay.

### CARD.UPPERCUT

Source: `MegaCrit.Sts2.Core.Models.Cards/Uppercut.cs:1–53` (SHA-256 in `cards.json`).

Attack target13, then Weak1, then Vulnerable1.

Upgrade: Both power amounts+1.
Branches: Artifact consumption and power-change hooks obey this order.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: WeakPower, VulnerablePower
Random outcomes: none
Required regressions: Artifact1/2, lethal hit application, Vicious-trigger draw after Weak.

### CARD.VICIOUS

Source: `MegaCrit.Sts2.Core.Models.Cards/Vicious.cs:1–33` (SHA-256 in `cards.json`).

Apply ViciousPower1.

Upgrade: Power draw amount+1.
Branches: Power reacts to positive Vulnerable amount changes whose applier is owner.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: ViciousPower
Random outcomes: none
Required regressions: Applying vs stacking Vulnerable, Artifact prevented, other-applier changes.

### ENCHANTMENT.GLAM

Source: `MegaCrit.Sts2.Core.Models.Enchantments/Glam.cs:1–57` (SHA-256 in `cards.json`).

EnchantPlayCount returns original+Times1 while unused; AfterCardPlayed for exact enchanted card sets used and Disabled status.

Upgrade: Not a card upgrade.
Branches: Ignore other cards; once used returns original play count. EnchantmentModel amount/dynamic-var initialization remains shared base contract.
State: Private bool _usedThisCombat; enchantment status; attached Card reference.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: First vs subsequent play, replay chains, clone/restore used flag and combat-copy reset.

### ENCHANTMENT.PERFECT_FIT

Source: `MegaCrit.Sts2.Core.Models.Enchantments/PerfectFit.cs:1–18` (SHA-256 in `cards.json`).

During non-initial shuffle, if shuffled list contains enchanted Card, remove it and insert at index0.

Upgrade: Not a card upgrade.
Branches: Initial shuffle unchanged; absent card unchanged; multiple PerfectFit modifiers require native hook order.
State: Base card state; applied AutomationPower has per-instance internal Data.cardsLeft independent of its energy Amount.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Initial vs reshuffle, several enchanted cards, pile index orientation and shuffle RNG consumption.

### ENCHANTMENT.SHARP

Source: `MegaCrit.Sts2.Core.Models.Enchantments/Sharp.cs:1–23` (SHA-256 in `cards.json`).

Can enchant Attack only; additive damage returns Amount only when ValueProp.IsPoweredAttack, else0.

Upgrade: Not a card upgrade.
Branches: Non-powered damage and non-Attacks excluded; additive modifier uses source enchantment amount.
State: Enchantment Amount and attached Card reference.
Fixed model dependencies: No additional fixed model referenced by the local gameplay methods.
Random outcomes: none
Required regressions: Positive/negative/zero amount and multi-hit Attack, no self-damage bonus.

### AFFLICTION.RINGING

Source: `MegaCrit.Sts2.Core.Models.Afflictions/Ringing.cs:1–9` (SHA-256 in `cards.json`).

No local effect method; HasExtraCardText. Most behavior lives in RingingPower.

Upgrade: Not a card upgrade.
Branches: RingingPower afflicts initially unafflicted owned combat cards and newly entering unafflicted cards; while affected, a Ringing-afflicted card passes ShouldPlay only if owner has no CardPlaysStarted this turn. Removal clears Ringing from owned combat cards; owner-side end removes power.
State: Affliction base Amount and attached Card reference.
Fixed model dependencies: RingingPower
Random outcomes: none
Required regressions: First vs later play this turn, preexisting other affliction remains, generated card entry, automatic play predicate, removal clears only Ringing.

### AFFLICTION.TAINTED

Source: `MegaCrit.Sts2.Core.Models.Afflictions/Tainted.cs:1–23` (SHA-256 in `cards.json`).

Stackable, extra card text; CanAfflictCardType only Skill. Most behavior lives in TaintedPower.

Upgrade: Not a card upgrade.
Branches: Attack/Power cannot be afflicted by this predicate. VitalSparkPower.AfterCardPlayed applies TaintedPower Amount to the owner of a Tainted-afflicted played card; the amount comes from VitalSparkPower, not the affliction. TaintedPower adds Amount to powered-attack damage received by its owner and removes at Enemy side end. The affliction itself has no OnPlay override.
State: Stacked Affliction Amount and attached Card reference.
Fixed model dependencies: TaintedPower
Random outcomes: none
Required regressions: Repeated affliction stacks, source Skill eligibility, resulting power application via shared hook, powered/unpowered incoming damage and Enemy-side removal; clone/restore amounts.

## Required nonroot power closure

These fixed classes execute an observed card's own legal behavior. They require effect implementations even if no instance appeared in the route. Symbolic class names below identify definitions; derive concrete Model IDs through the pinned model registry rather than inventing casing. TemporaryStrengthPower is an abstract shared superclass, not a selectable content model. OriginModel references point back to the root card for attribution/text and do not recursively demand unrelated content.

### EntropyPower

Source: `MegaCrit.Sts2.Core.Models.Powers/EntropyPower.cs:1–34`; SHA-256 and member anchors in JSON.

Owner AfterPlayerTurnStart selects Amount cards from Hand, then TransformToRandom sequentially using CombatCardSelection. Choice count and transformations preserve actual selected identities; shared selector handles undersized hand.

State: Amount, owner, selected cards/continuation; no extra local fields.

Further fixed dependencies: None additional in local gameplay methods.

Random outcomes: CardFactory.GetDefaultTransformationOptions/filtered transformation rules, not FilterForCombat alone.

Required regressions: Owner vs other player;0/undersized/many candidates; replacement identity, pile position, transform hooks and RNG.

### JugglingPower

Source: `MegaCrit.Sts2.Core.Models.Powers/JugglingPower.cs:1–63`; SHA-256 and member anchors in JSON.

AfterApplied initializes attack counter from owner CardPlaysStarted this turn. BeforeCardPlayed for owner Attack increments; when exactly3, clone the current Attack Amount times into Hand. AfterSideTurnEnd with owner participant resets counter0.

State: Internal Data.attacksPlayedThisTurn plus source/play identity and generated clone state.

Further fixed dependencies: None additional in local gameplay methods.

Random outcomes: No random pool; clones the actual Attack instance.

Required regressions: Power applied after1/2/3 attacks; third vs fourth; nested copies; multi-play events and exact history scope.

### ManglePower

Source: `MegaCrit.Sts2.Core.Models.Powers/ManglePower.cs:1–10`; SHA-256 and member anchors in JSON.

TemporaryStrengthPower with IsPositive=false; OriginModel Mangle. Inherits immediate signed Strength application, subsequent stack deltas and side-end removal/reversal.

State: Amount and applied temporary Strength/owner/applier/source identity.

Further fixed dependencies: TemporaryStrengthPower, StrengthPower

Random outcomes: none

Required regressions: Initial vs stacked negative effect; Artifact; side-turn removal then reverse Strength order.

### SetupStrikePower

Source: `MegaCrit.Sts2.Core.Models.Powers/SetupStrikePower.cs:1–8`; SHA-256 and member anchors in JSON.

TemporaryStrengthPower default positive sign; OriginModel SetupStrike. Inherits initial Strength, subsequent stack deltas, owner-side removal then reverse Strength.

State: Amount and applied temporary Strength/owner/applier/source identity.

Further fixed dependencies: TemporaryStrengthPower, StrengthPower

Random outcomes: none

Required regressions: Initial/stacked effect; removal and reversal; owner participation.

### TemporaryStrengthPower

Source: `MegaCrit.Sts2.Core.Models.Powers/TemporaryStrengthPower.cs:1–156`; SHA-256 and member anchors in JSON.

Abstract shared base, not extra content ID. BeforeApplied applies signed Strength with ThrowingPlayerChoiceContext, silent. AfterPowerAmountChanged when power==this and delta!=current Amount applies signed delta. AfterSideTurnEnd containing owner removes this, then applies -Sign*Amount Strength.

State: Owner, Amount, Source/Applier and hook continuation; origin model title references are not random output dependencies.

Further fixed dependencies: StrengthPower

Random outcomes: none

Required regressions: Signed buffs/debuffs, first application vs stack, Artifact behavior delegated to outer PowerCmd, exact removal-before-reversal.

### OneTwoPunchPower

Source: `MegaCrit.Sts2.Core.Models.Powers/OneTwoPunchPower.cs:1–44`; SHA-256 and member anchors in JSON.

ModifyCardPlayCount for owner Attack adds1. AfterModifyingCardPlayCount decrements Amount. At side end containing owner removes power.

State: Amount, owner and modifier-consumption ordering in suspended play-count generation.

Further fixed dependencies: None additional in local gameplay methods.

Random outcomes: none

Required regressions: Multiple charges and duplicate modifiers; Skill unaffected; side-end expiry.

### FreeAttackPower

Source: `MegaCrit.Sts2.Core.Models.Powers/FreeAttackPower.cs:1–65`; SHA-256 and member anchors in JSON.

Late cost modifier sets owner Attack in Hand/Play to0. BeforeCardPlayed decrements for same eligibility. Other card types, owners and piles unchanged.

State: Amount and owner; actual source-card pile determines eligibility.

Further fixed dependencies: None additional in local gameplay methods.

Random outcomes: none

Required regressions: Cost query purity; apply vs consume; Hand/Play vs Draw/Exhaust autoplay; stacked charges.

### StampedePower

Source: `MegaCrit.Sts2.Core.Models.Powers/StampedePower.cs:1–35`; SHA-256 and member anchors in JSON.

Owner AfterAutoPostPlayPhaseEntered loops Amount times; each iteration gathers current Hand Attack without Unplayable, chooses via Shuffle.NextItem and autoplays if non-null.

State: Amount, loop cursor, pending autoplay/target/choice continuation.

Further fixed dependencies: None additional in local gameplay methods.

Random outcomes: Existing hand instances only; Shuffle RNG.

Required regressions: Changing hand, nested generated cards,0 candidates, multiple charges; native RNG per iteration.

### TheGambitPower

Source: `MegaCrit.Sts2.Core.Models.Powers/TheGambitPower.cs:1–25`; SHA-256 and member anchors in JSON.

Single Debuff. AfterDamageReceived on owner, if powered attack and UnblockedDamage>0, removes self, then Kill owner.

State: Power identity until removed; damage result and continuation.

Further fixed dependencies: None additional in local gameplay methods.

Random outcomes: none

Required regressions: Blocked0, unpowered, self HP loss, actual powered leak; removal before death hooks.

### ViciousPower

Source: `MegaCrit.Sts2.Core.Models.Powers/ViciousPower.cs:1–27`; SHA-256 and member anchors in JSON.

AfterPowerAmountChanged: delta>0, applier==owner and power is VulnerablePower => draw Amount.

State: Amount, owner and suspended draw/trigger state.

Further fixed dependencies: VulnerablePower

Random outcomes: none

Required regressions: New/stack Vulnerable, prevented application, negative delta, another applier; multiple target applications.

### CuriousPower

Source: `MegaCrit.Sts2.Core.Models.Powers/CuriousPower.cs:1–34`; SHA-256 and member anchors in JSON.

ModifyEnergyCostInCombat for owner Power with originalCost>0 subtracts Amount and clamps0; other cards unchanged.

State: Amount and owner; query must not consume state.

Further fixed dependencies: None additional in local gameplay methods.

Random outcomes: none

Required regressions: Positive/zero costs, other owner/type, stacked cost modifiers and preview purity.

### ImprovementPower

Source: `MegaCrit.Sts2.Core.Models.Powers/ImprovementPower.cs:1–32`; SHA-256 and member anchors in JSON.

AfterCombatEnd gathers current upgradable permanent Deck cards; repeats Amount times, stopping if empty; CombatCardSelection.NextItem, removes candidate then Upgrade. Selection without replacement.

State: Amount, deck identities, upgraded flags; remaining candidate set and loop if command can suspend.

Further fixed dependencies: None additional in local gameplay methods.

Random outcomes: Existing permanent-deck candidates only, CombatCardSelection RNG.

Required regressions: 0/fewer/more eligible than Amount; upgrade persists to Deck, without replacement and exact RNG.

### StranglePower

Source: `MegaCrit.Sts2.Core.Models.Powers/StranglePower.cs:1–66`; SHA-256 and member anchors in JSON.

InstancedPerApplier Debuff. BeforeCardPlayed from applier player records this Amount in dictionary keyed by CardModel. AfterCardPlayed removes captured entry and deals captured unblocked/unpowered damage to owner (no source card). Removes at side end containing owner.

State: Internal dictionary<CardModel,int> amountsForPlayedCards; captured amounts and per-applier identity survive pending nested plays.

Further fixed dependencies: None additional in local gameplay methods.

Random outcomes: none

Required regressions: Applying/stacking card must not trigger its own new amount; nested/repeated source identities; applier ownership; side-end cleanup.

## Generation and variant rules requiring shared support

- `MegaCrit.Sts2.Core.Factories/CardFactory.cs:119–129,159–162`: `GetDistinctForCombat` first applies player-count filtering, then combat eligibility (`CanBeGeneratedInCombat`, excluding Basic/Ancient/Event, with distinct models), samples with the supplied RNG and creates mutable owned combat instances. A test-only RNG override exists; do not make that override native gameplay semantics. Retain full native candidate definitions/eligibility even when their effects are not yet implemented.
- `CardFactory.cs:170–214`: transformation uses original pool, or Colorless for Quest/Event/Ancient/Token; unlocked/multiplayer constraints; source rarity-dependent filter, combat-generation eligibility only when in combat, exclusion of original Model ID, player-count filter and explicit failure if no options. This differs from `GetDistinctForCombat`; EntropyPower cannot reuse an incorrectly narrowed generation pool.
- `MegaCrit.Sts2.Core.Commands/CardCmd.cs:371–464`: transformation captures original pile/index, obtains replacement, removes original, sorts by pile index, adds the replacement into Deck or same combat index, records generation/transform history and fires entry/pile hooks; calls original `AfterTransformedFrom` then replacement `AfterTransformedTo`. Future tests must cover permanent vs combat identity/ownership and history. The source contains additional presentation work after these gameplay steps; this audit is not an English/rendering certification.
- `MegaCrit.Sts2.Core.Models.Events/TinkerTime.cs:113–160`: for each chosen type the event samples two of the three legal riders with event RNG, then selected type/rider is saved on newly created MadScience. Nine legal combinations require full semantics; the preview/transient `None` rider does not justify arbitrary invalid persisted type/rider combinations.
- `MegaCrit.Sts2.Core.Map/SpoilsActMap.cs:90–108`: alternate map initializes independent `Rng(runState.Rng.Seed,"spoils_map")`, takes act map point counts, builds hourglass paths/types and prunes/repairs. SpoilsMap's hard edge to the complete alternative map implementation is a semantic dependency; preserve its full topology and RNG, not the route's visited nodes only. The parent shared-capability audit owns the full map algorithm requirement.

## Audit limits

Every requested root source and the fixed power classes above was read. Member order in a card establishes only that card's calls; generic damage, selection, death/combat-ending, power application, hook dispatch, play-count consumption, card-copy lifetime and attachment scaling are shared behavior and must use the parent shared-capability/source audit. No passing route, observed ownership or offer is treated as effect verification. Test-only mock fields in Splash/MadScience and factory override are explicitly separated from live state. This static audit does not certify runtime timing, complete public observation fields, English, or all cross-content interactions. Randomly generated nonroot outcomes retain full pools and probabilities; effects outside the eventual declared supported closure must fail explicitly rather than being silently replaced or filtered.
