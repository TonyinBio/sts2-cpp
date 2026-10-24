# Powers, monsters, encounters and event semantic dependencies

Source audit for the victory scope. Every root has an authored semantic summary and source hash in `encounters.json`; metadata extraction is an index, not proof of semantic completeness. No simulator implementation or reference experiment was performed. Source locations below are relative to `build/combat-state-audit/decompiled/` and refer to the pinned local decompilation.

## Reading the evidence

- `required_dependency_types` records authored fixed effects, finite encounter composition alternatives and shared base behavior; it does not mean a tooltip/type check is an unconditional content dependency.
- `type_reference_candidates` is deliberately separate: generation candidate declarations, optional compatibility predicates and tooltip references must not expand scope by textual occurrence alone.
- Root summaries cover single-player semantics. A8 ToughEnemies/A9 DeadlyEnemies branches are recorded where declared; multiplayer-only scaling remains outside this map.
- Full legal branches include defeated retained entities, empty target windows, special death interception and alternative event options; the dense route proves only its captured path.
- Visual delay, animation, audio and scene assets are not kernel dependencies. Presentation branches may still consume RNG or gate callbacks: shared adapter semantics must establish safe headless behavior.

## Closure and random generation

Fixed additions identified here: Dazed from EyeWithTeeth, Wound from PainfulStabs, Normality from the unchosen FieldOfManSizedHoles Resist option, Nimble/Swift from SelfHelpBook, and TheArchitectEventEncounter/Architect from the ending event. Decimillipede segment subclasses require their shared base. BowlbugNectar is the finite authored alternative in the entered BowlbugsWeak encounter; full legal encounter composition requires its implementation rather than treating one recorded roll as the entire encounter. Acceptance owns the corresponding test requirements.

Neow, Orobas and Vakuu list random offer pools. Keep every native candidate, eligibility check and RNG call, but do not classify every unoffered relic effect as fixed closure. RelicTrader cached pulls, BrainLeech reward cards, Cheese common cards and Legends potion rewards also use broad native generation: unimplemented additional outcomes may remain explicitly unsupported under the existing scoped-milestone rule; never filter or substitute results. TinkerTime is different: all three card types and all nine authored riders are variants of required MadScience semantics, even though each stage offers only two.

Optional compatibility queries (PaperPhrog/PaperKrane, Cruelty/Debilitate, Omnislice, Imbued, pet Osty, Vantom/Parafright visual special cases) are not unconditional generation dependencies. Their predicates must be represented correctly whenever those models are separately supported; no claim is made that all such models are implemented by this milestone.

## Cross-cutting regressions

1. Simultaneous/multihit lethals: compare Thorns before damage versus FlameBarrier after damage; PainfulStabs Wounds count positive-unblocked hits; PaperCuts max-HP loss; caps/Intangible and block depletion.
2. Death is not removal: Illusion full heal and saved follow-up, Adaptable first/second resurrection and removal before final death, Reattach all-dead fatal versus one-live heal; no-target player stop is legal.
3. Delayed turn hooks: Plating early end block then later decrement, Ritual first-enemy skip, Nemesis alternating flag, Hatch countdown and clear-except-Minion, Ringing uses started-play history and cleans card afflictions.
4. Persistent references: Swipe exact DeckVersion return and extra reward; Possess dictionaries restore signed stolen amounts only on nonprevented death; Infested queued creature additions and slots.
5. Event suspension: each alternate option, selection cancellation/null behavior, nested reward completion, cached RelicTrader arrays and bags, TinkerTime type/rider intermediate state, natural Architect victory.
6. Reproduce encounter composition and initial AI for every branch and slot including singleton candidate RNG calls, offsets and repeat restrictions; HP boundaries at A0/A8/A9, then integrate into full A0–A10 contract.

## Per-model requirements

### POWER.ADAPTABLE_POWER

Keeps dead TestSubject and this power, prevents hits while reviving and prevents combat completion; death sets internal reviving flag and immediate boss dead-state; DoRevive clears flag.

Source: `MegaCrit.Sts2.Core.Models.Powers/AdaptablePower.cs:1–71`; SHA-256 in JSON.

Fixed/finite dependencies: `TestSubject`.

### POWER.ARTIFACT_POWER

Cancels visible debuff amount received by owner and decrements only after modification; invisible debuffs and non-debuff signed amounts bypass.

Source: `MegaCrit.Sts2.Core.Models.Powers/ArtifactPower.cs:1–47`; SHA-256 in JSON.

### POWER.AUTOMATION_POWER

Instanced draw counter starts at ten; every owned card draw decrements, including draws not from hand-draw phase; at zero grants Amount energy then resets ten.

Source: `MegaCrit.Sts2.Core.Models.Powers/AutomationPower.cs:1–52`; SHA-256 in JSON.

### POWER.COLOSSUS_POWER

Halves powered attack damage received only when non-null dealer has Vulnerable; decrements at enemy turn end.

Source: `MegaCrit.Sts2.Core.Models.Powers/ColossusPower.cs:1–55`; SHA-256 in JSON.

Fixed/finite dependencies: `VulnerablePower`.

### POWER.CRIMSON_MANTLE_POWER

At owner player turn start, deals growing unpowered unblockable self damage then grants unpowered Amount block; IncrementSelfDamage changes dynamic value.

Source: `MegaCrit.Sts2.Core.Models.Powers/CrimsonMantlePower.cs:1–41`; SHA-256 in JSON.

### POWER.DEXTERITY_POWER

Allows negative amounts; additive block applies only to powered card/monster-move block and the appropriate owner/card owner.

Source: `MegaCrit.Sts2.Core.Models.Powers/DexterityPower.cs:1–39`; SHA-256 in JSON.

### POWER.DUPLICATION_POWER

Owner card play-count gets one extra play; decrement after modification; remove at turn end when owner participated.

Source: `MegaCrit.Sts2.Core.Models.Powers/DuplicationPower.cs:1–39`; SHA-256 in JSON.

### POWER.ENRAGE_POWER

After any skill card played, applies owner Amount Strength; do not add owner-player filter absent from source.

Source: `MegaCrit.Sts2.Core.Models.Powers/EnragePower.cs:1–23`; SHA-256 in JSON.

Fixed/finite dependencies: `StrengthPower`.

### POWER.ESCAPE_ARTIST_POWER

Visual countdown for Hopper; decrements participating owner turn-end only while Amount>1, then pulses at one; power does not itself escape.

Source: `MegaCrit.Sts2.Core.Models.Powers/EscapeArtistPower.cs:1–35`; SHA-256 in JSON.

### POWER.FEEL_NO_PAIN_POWER

After owned card exhaust, gain Amount unpowered block regardless of the ignored exhaust flag.

Source: `MegaCrit.Sts2.Core.Models.Powers/FeelNoPainPower.cs:1–26`; SHA-256 in JSON.

### POWER.FLAME_BARRIER_POWER

After receiving a powered attack from non-null dealer, retaliate Amount unpowered damage, including fully blocked attack; remove on opposite-side turn end.

Source: `MegaCrit.Sts2.Core.Models.Powers/FlameBarrierPower.cs:1–33`; SHA-256 in JSON.

### POWER.FLUTTER_POWER

Half powered attack damage; only nonzero unblocked powered hits decrement; depletion stuns using next-state lookup from state log/MonsterAi and grounds Hopper.

Source: `MegaCrit.Sts2.Core.Models.Powers/FlutterPower.cs:1–62`; SHA-256 in JSON.

Fixed/finite dependencies: `ThievingHopper`.

### POWER.HARD_TO_KILL_POWER

Per-damage cap Amount for owner; modifier notification only flashes.

Source: `MegaCrit.Sts2.Core.Models.Powers/HardToKillPower.cs:1–29`; SHA-256 in JSON.

### POWER.HATCH_POWER

Decrements at side turn end when owner participated; hatch behavior belongs to ToughEgg state machine.

Source: `MegaCrit.Sts2.Core.Models.Powers/HatchPower.cs:1–25`; SHA-256 in JSON.

### POWER.ILLUSION_POWER

On apply ensures Minion. Dead owner remains, preserves buffs and temporary debuffs according to native filter, becomes untargetable, receives mandatory revive move then full heal and saved follow-up state.

Source: `MegaCrit.Sts2.Core.Models.Powers/IllusionPower.cs:1–135`; SHA-256 in JSON.

Fixed/finite dependencies: `MinionPower`.

### POWER.IMBALANCED_POWER

A fully blocked attack given by owner either flags BowlbugRock off balance or stuns generic owner.

Source: `MegaCrit.Sts2.Core.Models.Powers/ImbalancedPower.cs:1–32`; SHA-256 in JSON.

Fixed/finite dependencies: `BowlbugRock`.

### POWER.INFESTED_POWER

Death not prevented spawns exactly four Wrigglers, StartStunned=true in authored slots; stops completion while power is active.

Source: `MegaCrit.Sts2.Core.Models.Powers/InfestedPower.cs:1–55`; SHA-256 in JSON.

Fixed/finite dependencies: `PhrogParasiteElite`, `Wriggler`.

### POWER.INTANGIBLE_POWER

Caps owner damage for block/preview and HP loss after pet interception at one during live combat; Amount duration decrements enemy turn end; sub-one HP loss preserved.

Source: `MegaCrit.Sts2.Core.Models.Powers/IntangiblePower.cs:1–73`; SHA-256 in JSON.

### POWER.MINION_POWER

Marks secondary enemy; survives owner death and suppresses fatal triggers; no independent move/effect.

Source: `MegaCrit.Sts2.Core.Models.Powers/MinionPower.cs:1–24`; SHA-256 in JSON.

### POWER.NEMESIS_POWER

Persistent toggle at participating owner turn end alternates apply one Intangible and remove existing Intangible.

Source: `MegaCrit.Sts2.Core.Models.Powers/NemesisPower.cs:1–36`; SHA-256 in JSON.

Fixed/finite dependencies: `IntangiblePower`.

### POWER.PAINFUL_STABS_POWER

Keeps corpse/power; after owner powered attack against opposing side, add Amount Wounds to discard per positive-unblocked hit per player; fully blocked hits give none.

Source: `MegaCrit.Sts2.Core.Models.Powers/PainfulStabsPower.cs:1–69`; SHA-256 in JSON.

Fixed/finite dependencies: `Wound`.

### POWER.PAPER_CUTS_POWER

Each positive-unblocked powered hit by owner against player loses Amount max HP.

Source: `MegaCrit.Sts2.Core.Models.Powers/PaperCutsPower.cs:1–23`; SHA-256 in JSON.

### POWER.PLATING_POWER

Enemy initial first-round block, early owner turn-end block before damage effects, then decrement after subsequent owner turn starts; native owner/round/turn exclusions matter.

Source: `MegaCrit.Sts2.Core.Models.Powers/PlatingPower.cs:1–89`; SHA-256 in JSON.

### POWER.PLOW_POWER

After positive unblocked hit leaves owner HP<=Amount, remove all TemporaryStrength instances and Strength, stun (special CeremonialBeast follow-up), then remove Plow.

Source: `MegaCrit.Sts2.Core.Models.Powers/PlowPower.cs:1–53`; SHA-256 in JSON.

Fixed/finite dependencies: `CeremonialBeast`, `StrengthPower`, `TemporaryStrengthPower`.

### POWER.POSSESS_SPEED_POWER

Tracks negative Dexterity changes applied by owner to players in per-creature dictionary; death not prevented restores each stolen amount.

Source: `MegaCrit.Sts2.Core.Models.Powers/PossessSpeedPower.cs:1–68`; SHA-256 in JSON.

Fixed/finite dependencies: `DexterityPower`.

### POWER.POSSESS_STRENGTH_POWER

Tracks negative Strength changes applied by owner to players in per-creature dictionary; death not prevented restores each stolen amount.

Source: `MegaCrit.Sts2.Core.Models.Powers/PossessStrengthPower.cs:1–68`; SHA-256 in JSON.

Fixed/finite dependencies: `StrengthPower`.

### POWER.REATTACH_POWER

Keeps dead segment/power, blocks hits while reviving, switches immediate dead state; heals Amount on reattach only while another segment lives; fatal only when all other reattach segments dead.

Source: `MegaCrit.Sts2.Core.Models.Powers/ReattachPower.cs:1–160`; SHA-256 in JSON.

Fixed/finite dependencies: `DecimillipedeSegment`.

### POWER.RINGING_POWER

Afflicts all owner combat cards lacking affliction plus newly entered cards; permits only first started owner card play of turn for Ringing cards; remove at owner participating turn-end and clear those afflictions.

Source: `MegaCrit.Sts2.Core.Models.Powers/RingingPower.cs:1–76`; SHA-256 in JSON.

Fixed/finite dependencies: `Ringing`.

### POWER.RITUAL_POWER

After owner turn end gain Amount Strength, except first enemy-owned application turn-end is skipped using retained flag.

Source: `MegaCrit.Sts2.Core.Models.Powers/RitualPower.cs:1–58`; SHA-256 in JSON.

Fixed/finite dependencies: `StrengthPower`.

### POWER.SANDPIT_POWER

Instanced target countdown decrements late enemy-turn-start; removal while owner/target alive force-kills target player and Osty; owner/target death avoids finisher. Position fields are presentation-only.

Source: `MegaCrit.Sts2.Core.Models.Powers/SandpitPower.cs:1–202`; SHA-256 in JSON.

### POWER.SHRINK_POWER

Powered attack damage by owner multiplied by 0.7; negative Amount means infinite single stack, finite durations decrement owner turn-end; applier death not prevented removes; captures applier title.

Source: `MegaCrit.Sts2.Core.Models.Powers/ShrinkPower.cs:1–108`; SHA-256 in JSON.

### POWER.STRENGTH_POWER

Signed additive damage on powered attacks by owner only.

Source: `MegaCrit.Sts2.Core.Models.Powers/StrengthPower.cs:1–28`; SHA-256 in JSON.

### POWER.SWIPE_POWER

Retains stolen combat card and target; steal removes DeckVersion from permanent deck and marks history; before owner death returns exact deck object and adds special card reward/history loot-return flag.

Source: `MegaCrit.Sts2.Core.Models.Powers/SwipePower.cs:1–79`; SHA-256 in JSON.

Fixed/finite dependencies: `ThievingHopperWeak`.

### POWER.TAINTED_POWER

Powered damage received by owner gets Amount additive increase; removes at enemy turn end.

Source: `MegaCrit.Sts2.Core.Models.Powers/TaintedPower.cs:1–40`; SHA-256 in JSON.

### POWER.TERRITORIAL_POWER

After owner participated in side turn, gains Amount Strength.

Source: `MegaCrit.Sts2.Core.Models.Powers/TerritorialPower.cs:1–29`; SHA-256 in JSON.

Fixed/finite dependencies: `StrengthPower`.

### POWER.THORNS_POWER

Before incoming powered attack (or Omnislice special case), retaliates Amount unpowered damage; differs in phase from FlameBarrier.

Source: `MegaCrit.Sts2.Core.Models.Powers/ThornsPower.cs:1–25`; SHA-256 in JSON.

### POWER.VITAL_SPARK_POWER

Before combat afflicts allied player skills Tainted; entering un-afflicted skills also afflict; after tainted card play apply TaintedPower; amount changes synchronize affliction amounts; removal clears.

Source: `MegaCrit.Sts2.Core.Models.Powers/VitalSparkPower.cs:1–99`; SHA-256 in JSON.

Fixed/finite dependencies: `Tainted`, `TaintedPower`.

### POWER.VULNERABLE_POWER

Powered incoming attack multiplier 1.5, modified by optional PaperPhrog/Cruelty/Debilitate compatibility; ticks duration at enemy turn end.

Source: `MegaCrit.Sts2.Core.Models.Powers/VulnerablePower.cs:1–67`; SHA-256 in JSON.

### POWER.WEAK_POWER

Powered attacks by owner multiply by .75, with optional target PaperKrane/owner Debilitate compatibility; ticks duration at enemy turn end.

Source: `MegaCrit.Sts2.Core.Models.Powers/WeakPower.cs:1–56`; SHA-256 in JSON.

### MONSTER.BOWLBUG_EGG

Repeats Bite attack then self Protect block. HP21–22 (A8:23–24); attack7/block7 (A9:8/8).

Source: `MegaCrit.Sts2.Core.Models.Monsters/BowlbugEgg.cs:1–91`; SHA-256 in JSON.

### MONSTER.BOWLBUG_ROCK

Starts Imbalanced; Headbutt15 (A9:16) loops unless fully-blocked flag chooses Dizzy stun then returns. HP45–48 (A8:46–49).

Source: `MegaCrit.Sts2.Core.Models.Monsters/BowlbugRock.cs:1–143`; SHA-256 in JSON.

Fixed/finite dependencies: `ImbalancedPower`.

### MONSTER.BYRDONIS

Starts Territorial1, alternates Peck3x3 (A9:4x3) and Swoop17 (A9:19). HP81–84 (A8:90).

Source: `MegaCrit.Sts2.Core.Models.Monsters/Byrdonis.cs:1–86`; SHA-256 in JSON.

Fixed/finite dependencies: `TerritorialPower`.

### MONSTER.CEREMONIAL_BEAST

Stamp applies Plow150 (A9:160), Plow18 (A9:20) then Strength2 loops until threshold stun. Mandatory stun then BeastCry/Ringing, Stomp15 (17), Crush17 (19)+Strength3 (4), repeat. HP252 (A8:262).

Source: `MegaCrit.Sts2.Core.Models.Monsters/CeremonialBeast.cs:1–325`; SHA-256 in JSON.

Fixed/finite dependencies: `PlowPower`, `RingingPower`, `StrengthPower`.

### MONSTER.DECIMILLIPEDE_SEGMENT_BACK

Inherits complete DecimillipedeSegment semantics; subclass changes segment animation only.

Source: `MegaCrit.Sts2.Core.Models.Monsters/DecimillipedeSegmentBack.cs:1–15`; SHA-256 in JSON.

Fixed/finite dependencies: `DecimillipedeSegment`.

### MONSTER.DECIMILLIPEDE_SEGMENT_FRONT

Inherits complete DecimillipedeSegment semantics; subclass changes segment animation only.

Source: `MegaCrit.Sts2.Core.Models.Monsters/DecimillipedeSegmentFront.cs:1–15`; SHA-256 in JSON.

Fixed/finite dependencies: `DecimillipedeSegment`.

### MONSTER.DECIMILLIPEDE_SEGMENT_MIDDLE

Inherits complete DecimillipedeSegment semantics; subclass changes segment animation only.

Source: `MegaCrit.Sts2.Core.Models.Monsters/DecimillipedeSegmentMiddle.cs:1–18`; SHA-256 in JSON.

Fixed/finite dependencies: `DecimillipedeSegment`.

### MONSTER.DEVOTED_SCULPTOR

ForbiddenIncantation Ritual9 then repeated Savage12 (A9:15); HP162 (A8:172).

Source: `MegaCrit.Sts2.Core.Models.Monsters/DevotedSculptor.cs:1–60`; SHA-256 in JSON.

Fixed/finite dependencies: `RitualPower`.

### MONSTER.EXOSKELETON

HardToKill9 on room add. Four slot-dependent starts: Skitter, Mandibles, Enrage, random. Skitter1x3 (A9:x4) -> no-repeat random; Mandibles8 (9) -> Enrage Strength2 -> random. HP24–28 (A8:26–30).

Source: `MegaCrit.Sts2.Core.Models.Monsters/Exoskeleton.cs:1–125`; SHA-256 in JSON.

Fixed/finite dependencies: `HardToKillPower`, `StrengthPower`.

### MONSTER.EYE_WITH_TEETH

HP6, Illusion on add, repeated Distract adds three Dazed to discard; revived under Illusion rather than removed.

Source: `MegaCrit.Sts2.Core.Models.Monsters/EyeWithTeeth.cs:1–64`; SHA-256 in JSON.

Fixed/finite dependencies: `Dazed`, `IllusionPower`.

### MONSTER.FOGMOG

Summons fixed EyeWithTeeth in illusion slot, Swipe8 (A9:9)+Strength1; weighted nonrepeat random Swipe(.4)/Headbutt(.6), then authored follow-ups. HP74 (A8:78).

Source: `MegaCrit.Sts2.Core.Models.Monsters/Fogmog.cs:1–113`; SHA-256 in JSON.

Fixed/finite dependencies: `EyeWithTeeth`, `StrengthPower`.

### MONSTER.INFESTED_PRISM

VitalSpark2 (A9:3), Jab15 (17), Radiate11 (13)+block11 (13), Whirlwind5x3 (6x3), Pulsate8 (10)+block20 (A8:22)+VitalSpark, cycle. HP161 (A8:171).

Source: `MegaCrit.Sts2.Core.Models.Monsters/InfestedPrism.cs:1–140`; SHA-256 in JSON.

Fixed/finite dependencies: `VitalSparkPower`.

### MONSTER.LEAF_SLIME_M

Starts StickyShot adding two Slimed to discard, alternates ClumpShot8 (A9:9). HP32–35 (A8:33–36).

Source: `MegaCrit.Sts2.Core.Models.Monsters/LeafSlimeM.cs:1–75`; SHA-256 in JSON.

Fixed/finite dependencies: `Slimed`.

### MONSTER.LEAF_SLIME_S

Starts Goop one Slimed; random cannot-repeat Goop/Tackle3 (A9:4). HP11–15 (A8:12–16).

Source: `MegaCrit.Sts2.Core.Models.Monsters/LeafSlimeS.cs:1–57`; SHA-256 in JSON.

Fixed/finite dependencies: `Slimed`.

### MONSTER.MECHA_KNIGHT

Artifact3 on add, Charge25 (A9:30), Flamethrower8 (12)+four Burn to hand, Windup block15 then Strength5, HeavyCleave35 (40), repeat from Flamethrower; isWoundUp retained. HP300 (A8:320).

Source: `MegaCrit.Sts2.Core.Models.Monsters/MechaKnight.cs:1–182`; SHA-256 in JSON.

Fixed/finite dependencies: `ArtifactPower`, `Burn`, `StrengthPower`.

### MONSTER.NIBBIT

IsAlone starts Butt; paired front starts Slice and back Hiss. Butt12 (A9:13), Slice6 (7)+block5 (A8:6), Hiss Strength2 (A9:3), cycle. HP42–46 (A8:44–48).

Source: `MegaCrit.Sts2.Core.Models.Monsters/Nibbit.cs:1–139`; SHA-256 in JSON.

Fixed/finite dependencies: `StrengthPower`.

### MONSTER.OVICOPTER

Lay three ToughEggs in last unused authored slots and Minion them; Smash16 (17), Tenderizer7 (8)+Vulnerable2, then Lay if alive teammate count<=3 else Strength3 (4) buff. HP124–130 (A8:126–132).

Source: `MegaCrit.Sts2.Core.Models.Monsters/Ovicopter.cs:1–148`; SHA-256 in JSON.

Fixed/finite dependencies: `MinionPower`, `StrengthPower`, `ToughEgg`, `VulnerablePower`.

### MONSTER.PHROG_PARASITE

Infested4 on add; randomized initial Infect three Infection discard or Lash4x4 (A9:5x4), then alternate; on death fixed four stunned Wrigglers. HP61–64 (A8:66–68).

Source: `MegaCrit.Sts2.Core.Models.Monsters/PhrogParasite.cs:1–79`; SHA-256 in JSON.

Fixed/finite dependencies: `Infection`, `InfestedPower`.

### MONSTER.SCROLL_OF_BITING

PaperCuts2. Seeded StarterMoveIdx chooses Chomp14 (A9:16), Chew5x2 (6x2), or MoreTeeth Strength2; Chomp->buff->Chew->random with native repeat rules. HP30–37 (A8:33–39).

Source: `MegaCrit.Sts2.Core.Models.Monsters/ScrollOfBiting.cs:1–148`; SHA-256 in JSON.

Fixed/finite dependencies: `PaperCutsPower`, `StrengthPower`.

### MONSTER.SHRINKER_BEETLE

Shrink infinite (-1), then alternate Chomp7 (8)/Stomp13 (14); applier link controls shrink removal. HP38–40 (A8:40–42).

Source: `MegaCrit.Sts2.Core.Models.Monsters/ShrinkerBeetle.cs:1–74`; SHA-256 in JSON.

Fixed/finite dependencies: `ShrinkPower`.

### MONSTER.SLIMED_BERSERKER

Vomit ten Slimed discard, Pummeling4x4 (A9:5x4), Hug Weak3 then Strength3, Smother30 (33), repeat. HP261 (A8:281).

Source: `MegaCrit.Sts2.Core.Models.Monsters/SlimedBerserker.cs:1–122`; SHA-256 in JSON.

Fixed/finite dependencies: `Slimed`, `StrengthPower`, `WeakPower`.

### MONSTER.TEST_SUBJECT

Death transition increments run ExtraFields.TestSubjectKills. Three forms100/200/300 HP (A8:111/212/313). Adaptable1+Enrage2 (A9:3). First Bite20 (22)/SkullBash14 (16)+Vulnerable1. First revive applies PainfulStabs and loops MultiClaw10 (11) with growing hit count; second revive applies Nemesis, removes Adaptable/PainfulStabs, then Lacerate10x3 (11x3), Pounce45, Growl three Burn (A9:5)+Strength2 (3).

Source: `MegaCrit.Sts2.Core.Models.Monsters/TestSubject.cs:1–423`; SHA-256 in JSON.

Fixed/finite dependencies: `AdaptablePower`, `Burn`, `EnragePower`, `NemesisPower`, `PainfulStabsPower`, `StrengthPower`, `VulnerablePower`.

### MONSTER.THE_FORGOTTEN

PossessSpeed; alternates Miasma steals Dexterity2, self block8 then Dex2; Dread13 (A9:15)+current own Dexterity. HP106 (A8:111).

Source: `MegaCrit.Sts2.Core.Models.Monsters/TheForgotten.cs:1–88`; SHA-256 in JSON.

Fixed/finite dependencies: `DexterityPower`, `PossessSpeedPower`.

### MONSTER.THE_INSATIABLE

Liquify adds instanced Sandpit4 target, three FranticEscape draw randomly placed and three discard, then Thrash8x2 (A9:9x2), Bite28 (31), Strength2 (3), two Thrashes, repeat. HP321 (A8:341).

Source: `MegaCrit.Sts2.Core.Models.Monsters/TheInsatiable.cs:1–212`; SHA-256 in JSON.

Fixed/finite dependencies: `FranticEscape`, `SandpitPower`, `StrengthPower`.

### MONSTER.THE_LOST

PossessStrength; alternates debuff Strength -2 to targets/self Strength2 and EyeLasers4x2 (A9:5x2). HP93 (A8:99).

Source: `MegaCrit.Sts2.Core.Models.Monsters/TheLost.cs:1–82`; SHA-256 in JSON.

Fixed/finite dependencies: `PossessStrengthPower`, `StrengthPower`.

### MONSTER.THIEVING_HOPPER

EscapeArtist5. Select draw/discard DeckVersion card using rarity/Imbued priority and CombatCardGeneration RNG; remove combat/deck card, retain Swipe, then attack17 (A9:19). Flutter5, HatTrick21 (23), Nab14 (16), Escape. HP79 (A8:84).

Source: `MegaCrit.Sts2.Core.Models.Monsters/ThievingHopper.cs:1–329`; SHA-256 in JSON.

Fixed/finite dependencies: `EscapeArtistPower`, `FlutterPower`, `SwipePower`.

### MONSTER.TOUGH_EGG

Hatch countdown1 outside enemy side or2 during enemy side; mandatory hatch clears powers except Minion, replaces max/current HP by Niche roll19–22 (A8:20–23), then Nibble4 (A9:5) loop. Initial HP14–18 (A8:15–19); IsHatched and posthatch move retained.

Source: `MegaCrit.Sts2.Core.Models.Monsters/ToughEgg.cs:1–209`; SHA-256 in JSON.

Fixed/finite dependencies: `HatchPower`.

### MONSTER.TWIG_SLIME_M

Starts StickyShot one Slimed discard; weighted random Pounce11 (A9:12) max-repeat2 versus nonrepeat StickyShot. HP26–28 (A8:27–29).

Source: `MegaCrit.Sts2.Core.Models.Monsters/TwigSlimeM.cs:1–80`; SHA-256 in JSON.

Fixed/finite dependencies: `Slimed`.

### MONSTER.TWIG_SLIME_S

Repeats Tackle4 (A9:5). HP7–11 (A8:8–12).

Source: `MegaCrit.Sts2.Core.Models.Monsters/TwigSlimeS.cs:1–39`; SHA-256 in JSON.

### MONSTER.WRIGGLER

StartStunned inserts Spawned no-op then slot1/3 Bite6 (A9:7), slot2/4 Wriggle; alternates bite and one Infection discard then Strength2. HP17–21 (A8:18–22).

Source: `MegaCrit.Sts2.Core.Models.Monsters/Wriggler.cs:1–111`; SHA-256 in JSON.

Fixed/finite dependencies: `Infection`, `StrengthPower`.

### ENCOUNTER.BOWLBUGS_WEAK

Two slots: fixed BowlbugRock odd, native random Egg/Nectar even. Both finite authored variants must be represented; Nectar is required for full legal encounter composition despite absent recording; acceptance owns the regression evidence.

Source: `MegaCrit.Sts2.Core.Models.Encounters/BowlbugsWeak.cs:1–35`; SHA-256 in JSON.

Fixed/finite dependencies: `BowlbugEgg`, `BowlbugNectar`, `BowlbugRock`.

### ENCOUNTER.BYRDONIS_ELITE

Creates the fixed named monster with no randomized composition; retain declared RoomType, weakness and tags.

Source: `MegaCrit.Sts2.Core.Models.Encounters/ByrdonisElite.cs:1–17`; SHA-256 in JSON.

Fixed/finite dependencies: `Byrdonis`.

### ENCOUNTER.CEREMONIAL_BEAST_BOSS

Creates the fixed named monster with no randomized composition; retain declared RoomType, weakness and tags.

Source: `MegaCrit.Sts2.Core.Models.Encounters/CeremonialBeastBoss.cs:1–35`; SHA-256 in JSON.

Fixed/finite dependencies: `CeremonialBeast`.

### ENCOUNTER.DECIMILLIPEDE_ELITE

Fixed front/middle/back slots; one Rng.NextInt(3) starter, offsets+1/+2 mod3. Preserve slot order and distinct instances.

Source: `MegaCrit.Sts2.Core.Models.Encounters/DecimillipedeElite.cs:1–50`; SHA-256 in JSON.

Fixed/finite dependencies: `DecimillipedeSegmentBack`, `DecimillipedeSegmentFront`, `DecimillipedeSegmentMiddle`.

### ENCOUNTER.DEVOTED_SCULPTOR_WEAK

Creates the fixed named monster with no randomized composition; retain declared RoomType, weakness and tags.

Source: `MegaCrit.Sts2.Core.Models.Encounters/DevotedSculptorWeak.cs:1–19`; SHA-256 in JSON.

Fixed/finite dependencies: `DevotedSculptor`.

### ENCOUNTER.EXOSKELETONS_NORMAL

Four distinct Exoskeleton instances in first/second/third/fourth slots; slot drives initial AI.

Source: `MegaCrit.Sts2.Core.Models.Encounters/ExoskeletonsNormal.cs:1–38`; SHA-256 in JSON.

Fixed/finite dependencies: `Exoskeleton`.

### ENCOUNTER.FOGMOG_NORMAL

Only Fogmog initially; EyeWithTeeth possible through summon, illusion slot reserved.

Source: `MegaCrit.Sts2.Core.Models.Encounters/FogmogNormal.cs:1–29`; SHA-256 in JSON.

Fixed/finite dependencies: `EyeWithTeeth`, `Fogmog`.

### ENCOUNTER.INFESTED_PRISMS_ELITE

Creates the fixed named monster with no randomized composition; retain declared RoomType, weakness and tags.

Source: `MegaCrit.Sts2.Core.Models.Encounters/InfestedPrismsElite.cs:1–20`; SHA-256 in JSON.

Fixed/finite dependencies: `InfestedPrism`.

### ENCOUNTER.MECHA_KNIGHT_ELITE

Creates the fixed named monster with no randomized composition; retain declared RoomType, weakness and tags.

Source: `MegaCrit.Sts2.Core.Models.Encounters/MechaKnightElite.cs:1–28`; SHA-256 in JSON.

Fixed/finite dependencies: `MechaKnight`.

### ENCOUNTER.NIBBITS_NORMAL

Two Nibbits; front IsFront=true and back default. Stable front/back slots.

Source: `MegaCrit.Sts2.Core.Models.Encounters/NibbitsNormal.cs:1–31`; SHA-256 in JSON.

Fixed/finite dependencies: `Nibbit`.

### ENCOUNTER.NIBBITS_WEAK

One Nibbit IsAlone=true.

Source: `MegaCrit.Sts2.Core.Models.Encounters/NibbitsWeak.cs:1–24`; SHA-256 in JSON.

Fixed/finite dependencies: `Nibbit`.

### ENCOUNTER.OVICOPTER_NORMAL

Only Ovicopter initially; five egg slots reserved for repeated ToughEgg summons.

Source: `MegaCrit.Sts2.Core.Models.Encounters/OvicopterNormal.cs:1–42`; SHA-256 in JSON.

Fixed/finite dependencies: `Ovicopter`, `ToughEgg`.

### ENCOUNTER.PHROG_PARASITE_ELITE

Only Phrog initially; four Wriggler slots reserved for Infested death spawn.

Source: `MegaCrit.Sts2.Core.Models.Encounters/PhrogParasiteElite.cs:1–42`; SHA-256 in JSON.

Fixed/finite dependencies: `PhrogParasite`, `Wriggler`.

### ENCOUNTER.SCROLLS_OF_BITING_WEAK

Three scrolls; one seeded starter index then offsets1/2 mod3.

Source: `MegaCrit.Sts2.Core.Models.Encounters/ScrollsOfBitingWeak.cs:1–33`; SHA-256 in JSON.

Fixed/finite dependencies: `ScrollOfBiting`.

### ENCOUNTER.SHRINKER_BEETLE_WEAK

Creates the fixed named monster with no randomized composition; retain declared RoomType, weakness and tags.

Source: `MegaCrit.Sts2.Core.Models.Encounters/ShrinkerBeetleWeak.cs:1–22`; SHA-256 in JSON.

Fixed/finite dependencies: `ShrinkerBeetle`.

### ENCOUNTER.SLIMED_BERSERKER_NORMAL

Creates the fixed named monster with no randomized composition; retain declared RoomType, weakness and tags.

Source: `MegaCrit.Sts2.Core.Models.Encounters/SlimedBerserkerNormal.cs:1–17`; SHA-256 in JSON.

Fixed/finite dependencies: `SlimedBerserker`.

### ENCOUNTER.SLIMES_NORMAL

Fixed medium Twig/Leaf then one each small type; random bool swaps small order.

Source: `MegaCrit.Sts2.Core.Models.Encounters/SlimesNormal.cs:1–46`; SHA-256 in JSON.

Fixed/finite dependencies: `LeafSlimeM`, `LeafSlimeS`, `TwigSlimeM`, `TwigSlimeS`.

### ENCOUNTER.SLIMES_WEAK

Draw small type, remove it from local candidates, draw remaining type, draw medium type; output small/medium/other-small. Preserve even singleton draw RNG consumption.

Source: `MegaCrit.Sts2.Core.Models.Encounters/SlimesWeak.cs:1–60`; SHA-256 in JSON.

Fixed/finite dependencies: `LeafSlimeM`, `LeafSlimeS`, `TwigSlimeM`, `TwigSlimeS`.

### ENCOUNTER.TEST_SUBJECT_BOSS

Creates the fixed named monster with no randomized composition; retain declared RoomType, weakness and tags.

Source: `MegaCrit.Sts2.Core.Models.Encounters/TestSubjectBoss.cs:1–26`; SHA-256 in JSON.

Fixed/finite dependencies: `TestSubject`.

### ENCOUNTER.THE_INSATIABLE_BOSS

Creates the fixed named monster with no randomized composition; retain declared RoomType, weakness and tags.

Source: `MegaCrit.Sts2.Core.Models.Encounters/TheInsatiableBoss.cs:1–28`; SHA-256 in JSON.

Fixed/finite dependencies: `TheInsatiable`.

### ENCOUNTER.THE_LOST_AND_FORGOTTEN_NORMAL

Fixed ordered TheLost then TheForgotten, separate instance state.

Source: `MegaCrit.Sts2.Core.Models.Encounters/TheLostAndForgottenNormal.cs:1–25`; SHA-256 in JSON.

Fixed/finite dependencies: `TheForgotten`, `TheLost`.

### ENCOUNTER.THIEVING_HOPPER_WEAK

Creates the fixed named monster with no randomized composition; retain declared RoomType, weakness and tags.

Source: `MegaCrit.Sts2.Core.Models.Encounters/ThievingHopperWeak.cs:1–22`; SHA-256 in JSON.

Fixed/finite dependencies: `ThievingHopper`.

### EVENT.BRAIN_LEECH

Act<2. Rip deals5 unpowered unblockable then offers one Colorless reward of3 with no rarity/pool modifications; ShareKnowledge generates5 character-pool cards and forces noncancelable select1 deck addition.

Source: `MegaCrit.Sts2.Core.Models.Events/BrainLeech.cs:1–73`; SHA-256 in JSON.

### EVENT.FIELD_OF_MAN_SIZED_HOLES

Requires every player eligible PerfectFit card. Resist selects/removes2 deck cards then adds Normality curse. EnterYourHole selects eligible card and enchants PerfectFit1, ends even no selected card.

Source: `MegaCrit.Sts2.Core.Models.Events/FieldOfManSizedHoles.cs:1–66`; SHA-256 in JSON.

Fixed/finite dependencies: `Normality`, `PerfectFit`.

### EVENT.NEOW

Unmodified setup chooses filtered curse option then two positives with paired exclusions and three random alternatives; all native candidates/ordering/RNG preserved. Modified setup sequentially runs each modifier Neow callback; no options if none.

Source: `MegaCrit.Sts2.Core.Models.Events/Neow.cs:1–314`; SHA-256 in JSON.

### EVENT.OROBAS

Select unlocked other character (fallback own), then random one of first pool augmented by 1/3 PrismaticGem versus SeaGlass(parameterized CharacterId); one second-pool relic; one filtered third-pool relic or locked placeholder. Preserve setup filters and calls.

Source: `MegaCrit.Sts2.Core.Models.Events/Orobas.cs:1–214`; SHA-256 in JSON.

### EVENT.RELIC_TRADER

Act>0, all players>=5 tradable relics. Cache stable-shuffled up to3 owned choices and pull exactly3 new front-bag relics. Each indexed branch removes owned then obtains corresponding new mutable relic. No owned choices offers Proceed.

Source: `MegaCrit.Sts2.Core.Models.Events/RelicTrader.cs:1–170`; SHA-256 in JSON.

### EVENT.ROOM_FULL_OF_CHEESE

Act<2. Gorge generates8 common character cards with uniform odds/no rarity modification, selects2 into deck. Search deals14 unpowered unblockable then obtains fixed ChosenCheese.

Source: `MegaCrit.Sts2.Core.Models.Events/RoomFullOfCheese.cs:1–53`; SHA-256 in JSON.

Fixed/finite dependencies: `ChosenCheese`.

### EVENT.SELF_HELP_BOOK

Eligibility exposes or locks Attack Sharp2/Skill Nimble2/Power Swift2; if no eligible card offers SkipBook. Each branch selects1 compatible deck card, enchants if non-null then ends.

Source: `MegaCrit.Sts2.Core.Models.Events/SelfHelpBook.cs:1–141`; SHA-256 in JSON.

Fixed/finite dependencies: `Nimble`, `Sharp`, `Swift`.

### EVENT.THE_ARCHITECT

Combat-layout event uses fixed TheArchitectEventEncounter/Architect; stores dialogue/line and score; continue/respond advance line, final Proceed invokes WinRun. Headless adapter must preserve natural victory semantics without requiring presentation nodes.

Source: `MegaCrit.Sts2.Core.Models.Events/TheArchitect.cs:1–527`; SHA-256 in JSON.

Fixed/finite dependencies: `Architect`, `TheArchitectEventEncounter`.

### EVENT.THE_LEGENDS_WERE_TRUE

Act0, nonempty decks, all HP>=10. Nab adds fixed SpoilsMap. Exit deals8 unpowered unblockable then chooses native unlocked character+shared potion via Rewards RNG and offers if non-null.

Source: `MegaCrit.Sts2.Core.Models.Events/TheLegendsWereTrue.cs:1–63`; SHA-256 in JSON.

Fixed/finite dependencies: `SpoilsMap`.

### EVENT.TINKER_TIME

Type stage samples2 of Attack/Skill/Power; rider stage samples2 of3 type-specific riders. Stores chosen type; creates parameterized MadScience with chosen type/rider. All nine rider effects belong to required card semantics, not only recorded Chaos.

Source: `MegaCrit.Sts2.Core.Models.Events/TinkerTime.cs:1–205`; SHA-256 in JSON.

Fixed/finite dependencies: `MadScience`.

### EVENT.VAKUU

Three independent native unstable pool shuffles, one option from each; choice effects are relic acquisition. Profile visits changes presentation variable, not permission to depend on profile for gameplay.

Source: `MegaCrit.Sts2.Core.Models.Events/Vakuu.cs:1–165`; SHA-256 in JSON.

### DecimillipedeSegment

Shared base: initial HP40–46 (A8:46–52), adjusted to even distinct from teammates, Reattach25. Starter index chooses Writhe5x2 (A9:6x2), Bulk6 (7)+Strength2, Constrict8 (9)+Weak1; cycle. Death waits, mandatory Reattach then no-repeat random alive move.

Source: `MegaCrit.Sts2.Core.Models.Monsters/DecimillipedeSegment.cs:1–282`; SHA-256 in JSON.

Fixed/finite dependencies: `ReattachPower`, `StrengthPower`, `WeakPower`.

### BowlbugNectar

Finite BowlbugsWeak composition alternative: attack3, buff self Strength15 (A9:16), then repeat attack3. HP35–38 (A8:36–39).

Source: `MegaCrit.Sts2.Core.Models.Monsters/BowlbugNectar.cs:1–92`; SHA-256 in JSON.

Fixed/finite dependencies: `StrengthPower`.

### Dazed

Status cost -1, no target, Ethereal and Unplayable, MaxUpgradeLevel0; requires generic ethereal end-turn exhaust and unplayable action rejection, no fixed model dependencies.

Source: `MegaCrit.Sts2.Core.Models.Cards/Dazed.cs:1–20`; SHA-256 in JSON.

### Wound

Status cost -1, no target, Unplayable, MaxUpgradeLevel0; no special effect and no fixed model dependencies.

Source: `MegaCrit.Sts2.Core.Models.Cards/Wound.cs:1–16`; SHA-256 in JSON.

### Normality

Unplayable curse, no upgrades; while in hand blocks all owner card plays after three started plays this turn, including autoplay. Reads turn-scoped CardPlaysStarted history.

Source: `MegaCrit.Sts2.Core.Models.Cards/Normality.cs:1–53`; SHA-256 in JSON.

### Nimble

Requires base enchantment eligibility plus GainsBlock; adds Amount to enchanted block.

Source: `MegaCrit.Sts2.Core.Models.Enchantments/Nimble.cs:1–20`; SHA-256 in JSON.

### Swift

On play while status Normal, disables enchantment then draws Amount; status must clone/restore and native reset/lifetime rules apply.

Source: `MegaCrit.Sts2.Core.Models.Enchantments/Swift.cs:1–23`; SHA-256 in JSON.

### TheArchitectEventEncounter

Event-only encounter creates fixed Architect; no random content pool.

Source: `MegaCrit.Sts2.Core.Models.Encounters/TheArchitectEventEncounter.cs:1–19`; SHA-256 in JSON.

Fixed/finite dependencies: `Architect`.

### Architect

9999 max/current initial HP; repeating Nothing move with hidden intent; event actor rather than boss victory shortcut.

Source: `MegaCrit.Sts2.Core.Models.Monsters/Architect.cs:1–39`; SHA-256 in JSON.

## Factual limits and remaining integration evidence

This is a source-derived inventory with exact file hashes and member/branch/RNG indexes, not a claim that all global dispatch order or command semantics are specified. Parent shared-command audit must resolve effective ordering, base-class eligibility/duration/reset rules, attack/HP rounding, selection plumbing, generation filters and persistence lifetimes. Each model requires regression evidence beyond its observed use; presence in ownership or an offer is not execution evidence. The public visibility/text audits remain separate. No existing ADR is contradicted.
