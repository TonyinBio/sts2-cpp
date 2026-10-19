# Model-owned combat field audit

This inventory lists fields found in the inspected game classes and explains whether
each needs extra capture, is already saved, or can be left out. Use it with the runtime
audit when writing an exporter.

See the [shared terms](README.md#terms-used-in-these-sheets) for definitions of parity, snapshot, boundary, and model ID.

## On this page

- [How to read the field tables](#how-to-read-the-field-tables)
- [Cards](#cards)
- [Powers](#powers)
- [Relics](#relics)
- [Monsters](#monsters)
- [Potions](#potions)
- [Orbs](#orbs)
- [Enchantments](#enchantments)
- [Afflictions](#afflictions)
- [Encounters](#encounters)
- [Modifiers](#modifiers)
- [Characters](#characters)

Target: installed v0.111.0 DLL, SHA-256
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.

This is a source-member inventory supporting [the runtime
audit](combat-runtime-audit.md), not a new schema or a claim that every member is live
at every input boundary. The names below are actual C# storage members, including nested
power `Data` members. Only objects still used or referenced by gameplay are relevant.
Collection types and nullability must follow the source; reference-valued keys/values
require references that preserve shared object identity.

The scan covers non-static, non-constant declared fields and writable auto-properties in
the concrete Cards, Powers, Relics, Monsters, Potions, Orbs, Enchantments, Afflictions,
Encounters, Modifiers and Characters namespaces; mocks subnamespaces are excluded.
Base-class storage and suspended method locals are covered separately in the runtime
audit. This declaration scan is not a proof of reachability or minimality of every
member.

## How to read the field tables

A **model** is a game definition, such as a card, relic, or monster. A field is a stored
value on one instance of that model. `Type` uses the game's C# spelling; `?` allows
null, `List` preserves order, and `HashSet` stores distinct entries.

Classification is based on the code that reads each field:

* `additional_gameplay`: gameplay state absent from native saved properties; capture when relevant at the boundary.
* `native_property`: already represented by the named native `[SavedProperty]`.
* `native_room_property`: represented by encounter custom state in a native room record.
* `exclude_presentation`: consumers affect graphics, audio, text or display caches.
* `exclude_test_override`: tests/mocks, outside normal gameplay.
* `exclude_multiplayer`: creating content is multiplayer-only.
* `exclude_fixed_definition`: initialized value with no gameplay writes after setup in this build.
* `initial_configuration`: setup parameter; the resulting HP/initial AI state is the compared runtime state.
* `ai_state_alias`: capture the referenced AI state once, rather than duplicating the same state object.

The source links target the ignored local decompilation described in the runtime audit.
Additional members from base classes, such as enchantment `Status`, do not appear again
in each subclass. Native-property matching excludes hover-tip side effects from saved
property setters; only the actual saved gameplay value is reused.

| Classification | Declared members |
| --- | ---: |
| `additional_gameplay` | 125 |
| `ai_state_alias` | 8 |
| `exclude_fixed_definition` | 2 |
| `exclude_multiplayer` | 4 |
| `exclude_presentation` | 76 |
| `exclude_test_override` | 10 |
| `initial_configuration` | 10 |
| `native_property` | 57 |
| `native_room_property` | 1 |

These counts describe the declaration inventory, not a count of fields in the final
JSON.

## Cards

| Source field | Type | How to handle it |
| --- | --- | --- |
| [BouncingFlask._vfxTint](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/BouncingFlask.cs#L22) | `Color` | exclude_presentation |
| [Claw._extraDamageFromClawPlays](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Claw.cs#L19) | `decimal` | additional_gameplay |
| [Discovery._mockSelectedCard](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Discovery.cs#L13) | `CardModel?` | exclude_test_override |
| [Dowsing._roomsEntered](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Dowsing.cs#L19) | `int` | native_property: RoomsEntered |
| [GeneticAlgorithm._currentBlock](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/GeneticAlgorithm.cs#L16) | `int` | native_property: CurrentBlock |
| [GeneticAlgorithm._increasedBlock](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/GeneticAlgorithm.cs#L18) | `int` | native_property: IncreasedBlock |
| [Guilty._combatsSeen](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Guilty.cs#L17) | `int` | native_property: CombatsSeen |
| [KinglyPunch._extraDamage](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/KinglyPunch.cs#L16) | `decimal` | additional_gameplay |
| [MadScience._tinkerTimeType](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/MadScience.cs#L63) | `CardType` | native_property: TinkerTimeType |
| [MadScience._tinkerTimeRider](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/MadScience.cs#L65) | `TinkerTime.RiderEffect` | native_property: TinkerTimeRider |
| [MadScience._mockedChaosCard](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/MadScience.cs#L67) | `CardModel?` | exclude_test_override |
| [Maul._extraDamageFromMaulPlays](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Maul.cs#L19) | `decimal` | additional_gameplay |
| [Rampage._extraDamageFromPlays](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Rampage.cs#L16) | `decimal` | additional_gameplay |
| [Regret._cardsInHand](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Regret.cs#L15) | `int` | additional_gameplay |
| [SovereignBlade._currentDamage](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/SovereignBlade.cs#L27) | `decimal` | additional_gameplay |
| [SovereignBlade._currentRepeats](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/SovereignBlade.cs#L29) | `decimal` | additional_gameplay |
| [SovereignBlade._createdThroughForge](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/SovereignBlade.cs#L31) | `bool` | additional_gameplay |
| [Splash._mockGeneratedCard](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Splash.cs#L13) | `CardModel?` | exclude_test_override |
| [SpoilsMap._spoilsActIndex](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/SpoilsMap.cs#L15) | `int` | native_property: SpoilsActIndex |
| [SpoilsMap.SpoilsCoord](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/SpoilsMap.cs#L37) | `MapCoord?` | additional_gameplay |
| [TheBall._extraDamageFromPlays](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/TheBall.cs#L18) | `decimal` | additional_gameplay |
| [TheScythe._currentDamage](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/TheScythe.cs#L19) | `int` | native_property: CurrentDamage |
| [TheScythe._increasedDamage](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/TheScythe.cs#L21) | `int` | native_property: IncreasedDamage |
| [Thrash._extraDamage](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Thrash.cs#L18) | `decimal` | additional_gameplay |
| [UpMySleeve._timesPlayedThisCombat](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/UpMySleeve.cs#L13) | `int` | additional_gameplay |
| [Wither._fakeUpgradeLevel](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Cards/Wither.cs#L15) | `int` | exclude_presentation |

## Powers

| Source field | Type | How to handle it |
| --- | --- | --- |
| [AdaptablePower.isReviving](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/AdaptablePower.cs#L13) | `bool` | additional_gameplay |
| [AfterimagePower.amountsForPlayedCards](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/AfterimagePower.cs#L21) | `Dictionary<CardModel, int>` | additional_gameplay |
| [AutomationPower.cardsLeft](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/AutomationPower.cs#L14) | `int` | additional_gameplay |
| [BeaconOfHopePower._hasAlreadyBeenGivenBlock](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/BeaconOfHopePower.cs#L13) | `bool` | exclude_multiplayer |
| [CalamityPower.amountsForPlayedCards](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/CalamityPower.cs#L16) | `Dictionary<CardModel, int>` | additional_gameplay |
| [ChainsOfBindingPower.boundCardPlayed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/ChainsOfBindingPower.cs#L22) | `bool` | additional_gameplay |
| [ConfusedPower._testEnergyCostOverride](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/ConfusedPower.cs#L11) | `int` | exclude_test_override |
| [CurlUpPower.playedCard](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/CurlUpPower.cs#L18) | `CardModel?` | additional_gameplay |
| [DampenPower.casters](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/DampenPower.cs#L15) | `HashSet<Creature>` | additional_gameplay |
| [DampenPower.downgradedCardsToOldUpgradeLevels](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/DampenPower.cs#L17) | `Dictionary<CardModel, int>` | additional_gameplay |
| [DarkEmbracePower.etherealCount](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/DarkEmbracePower.cs#L23) | `int` | additional_gameplay |
| [DemonFormPower._vfx](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/DemonFormPower.cs#L17) | `NDemonFormVfx?` | exclude_presentation |
| [EchoFormPower._vfx](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/EchoFormPower.cs#L17) | `NEchoFormVfx?` | exclude_presentation |
| [FeralPower.zeroCostAttacksPlayed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/FeralPower.cs#L17) | `int` | additional_gameplay |
| [GigantificationPower.commandToModify](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/GigantificationPower.cs#L16) | `AttackCommand?` | additional_gameplay |
| [GravityPower.amountsForPlayedCards](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/GravityPower.cs#L23) | `Dictionary<CardModel, int>` | additional_gameplay |
| [HardenedShellPower.damageReceivedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/HardenedShellPower.cs#L16) | `decimal` | additional_gameplay |
| [HellraiserPower.autoPlayingCards](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/HellraiserPower.cs#L19) | `HashSet<CardModel>` | exclude_presentation |
| [HellraiserPower.infiniteAutoPlaysThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/HellraiserPower.cs#L21) | `int` | additional_gameplay |
| [HellraiserPower.showedCapReachedMessage](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/HellraiserPower.cs#L23) | `bool` | exclude_presentation |
| [IllusionPower.isReviving](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/IllusionPower.cs#L19) | `bool` | additional_gameplay |
| [IllusionPower._followUpStateId](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/IllusionPower.cs#L26) | `string?` | additional_gameplay |
| [ImitationLearningPower.cardsAndClones](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/ImitationLearningPower.cs#L19) | `List<(CardModel Card, CardModel Clone)>` | exclude_multiplayer |
| [ImitationLearningPower._playerTarget](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/ImitationLearningPower.cs#L24) | `Player?` | exclude_multiplayer |
| [InterceptPower.coveredCreatures](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/InterceptPower.cs#L20) | `List<Creature>` | exclude_multiplayer |
| [JugglingPower.attacksPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/JugglingPower.cs#L18) | `int` | additional_gameplay |
| [MonologuePower.amountsForPlayedCards](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/MonologuePower.cs#L24) | `Dictionary<CardModel, int>` | additional_gameplay |
| [NemesisPower._shouldApplyIntangible](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/NemesisPower.cs#L14) | `bool` | additional_gameplay |
| [NightmarePower.selectedCard](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/NightmarePower.cs#L23) | `CardModel?` | additional_gameplay |
| [OblivionPower.amountsForPlayedCards](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/OblivionPower.cs#L20) | `Dictionary<CardModel, int>` | additional_gameplay |
| [OrbitPower.energySpent](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/OrbitPower.cs#L14) | `int` | additional_gameplay |
| [OrbitPower.triggerCount](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/OrbitPower.cs#L16) | `int` | additional_gameplay |
| [PaleBlueDotPower.alreadyActivatedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/PaleBlueDotPower.cs#L20) | `bool` | additional_gameplay |
| [PanachePower.alreadyApplied](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/PanachePower.cs#L22) | `bool` | additional_gameplay |
| [PossessSpeedPower.stolenDexterity](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/PossessSpeedPower.cs#L15) | `Dictionary<Creature, decimal>` | additional_gameplay |
| [PossessStrengthPower.stolenStrength](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/PossessStrengthPower.cs#L15) | `Dictionary<Creature, decimal>` | additional_gameplay |
| [ReaperFormPower._vfx](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/ReaperFormPower.cs#L16) | `NReaperFormVfx?` | exclude_presentation |
| [ReattachPower.isReviving](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/ReattachPower.cs#L21) | `bool` | additional_gameplay |
| [RitualPower._wasJustAppliedByEnemy](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/RitualPower.cs#L15) | `bool` | additional_gameplay |
| [RupturePower.playedCards](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/RupturePower.cs#L17) | `Dictionary<CardModel, int>` | additional_gameplay |
| [SandpitPower._initialAmount](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/SandpitPower.cs#L29) | `int` | exclude_presentation |
| [SandpitPower._initialTargetPosition](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/SandpitPower.cs#L31) | `float` | exclude_presentation |
| [SerpentFormPower.amountsForPlayedCards](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/SerpentFormPower.cs#L24) | `Dictionary<CardModel, int>` | additional_gameplay |
| [SerpentFormPower._vfx](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/SerpentFormPower.cs#L27) | `NSerpentFormVfx?` | exclude_presentation |
| [SkittishPower.hasGainedBlockThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/SkittishPower.cs#L23) | `bool` | additional_gameplay |
| [SlothPower._cardsPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/SlothPower.cs#L14) | `int` | additional_gameplay |
| [SoulboundPower._isAddingSoul](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/SoulboundPower.cs#L14) | `bool` | additional_gameplay |
| [StormPower.amountsForPlayedCards](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/StormPower.cs#L21) | `Dictionary<CardModel, int>` | additional_gameplay |
| [StranglePower.amountsForPlayedCards](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/StranglePower.cs#L22) | `Dictionary<CardModel, int>` | additional_gameplay |
| [SubroutinePower.amountsForPlayedCards](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/SubroutinePower.cs#L19) | `Dictionary<CardModel, int>` | additional_gameplay |
| [SurroundedPower._facing](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/SurroundedPower.cs#L27) | `Direction` | additional_gameplay |
| [SwipePower._stolenCard](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/SwipePower.cs#L17) | `CardModel?` | additional_gameplay |
| [TenderPower._cardsPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/TenderPower.cs#L16) | `int` | additional_gameplay |
| [VigorPower.commandToModify](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/VigorPower.cs#L16) | `AttackCommand?` | additional_gameplay |
| [VigorPower.amountWhenAttackStarted](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/VigorPower.cs#L18) | `int` | additional_gameplay |
| [VoidFormPower.cardsPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/VoidFormPower.cs#L18) | `int` | additional_gameplay |
| [VoidFormPower._vfx](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Powers/VoidFormPower.cs#L21) | `NVoidFormVfx?` | exclude_presentation |

## Relics

| Source field | Type | How to handle it |
| --- | --- | --- |
| [ArchaicTooth._serializableStarterCard](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/ArchaicTooth.cs#L20) | `SerializableCard?` | native_property: StarterCard |
| [ArchaicTooth._serializableAncientCard](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/ArchaicTooth.cs#L22) | `SerializableCard?` | native_property: AncientCard |
| [ArchaicTooth._extraHoverTips](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/ArchaicTooth.cs#L24) | `List<IHoverTip>` | exclude_presentation |
| [ArtOfWar._anyAttacksPlayedLastTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/ArtOfWar.cs#L19) | `bool` | additional_gameplay |
| [ArtOfWar._anyAttacksPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/ArtOfWar.cs#L21) | `bool` | additional_gameplay |
| [BeatingRemnant._damageReceivedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/BeatingRemnant.cs#L18) | `decimal` | additional_gameplay |
| [BeltBuckle._dexterityApplied](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/BeltBuckle.cs#L18) | `bool` | additional_gameplay |
| [BingBong._cardsToSkip](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/BingBong.cs#L11) | `HashSet<CardModel>?` | additional_gameplay |
| [BoneTea._combatsLeft](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/BoneTea.cs#L18) | `int` | native_property: CombatsLeft |
| [BookOfFiveRings._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/BookOfFiveRings.cs#L15) | `bool` | exclude_presentation |
| [BookOfFiveRings._cardsAdded](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/BookOfFiveRings.cs#L17) | `int` | native_property: CardsAdded |
| [BrilliantScarf._cardsPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/BrilliantScarf.cs#L16) | `int` | additional_gameplay |
| [BurningSticks._wasUsedThisCombat](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/BurningSticks.cs#L14) | `bool` | additional_gameplay |
| [Byrdpip._skin](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Byrdpip.cs#L18) | `string` | native_property: Skin |
| [CentennialPuzzle._usedThisCombat](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/CentennialPuzzle.cs#L16) | `bool` | additional_gameplay |
| [DemonTongue._triggeredThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/DemonTongue.cs#L15) | `bool` | additional_gameplay |
| [DustyTome._ancientCard](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/DustyTome.cs#L20) | `ModelId?` | native_property: AncientCard |
| [DustyTome._extraHoverTips](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/DustyTome.cs#L22) | `IEnumerable<IHoverTip>` | exclude_presentation |
| [EmberTea._combatsLeft](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/EmberTea.cs#L19) | `int` | native_property: CombatsLeft |
| [FakeHappyFlower._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/FakeHappyFlower.cs#L20) | `bool` | exclude_presentation |
| [FakeHappyFlower._turnsSeen](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/FakeHappyFlower.cs#L22) | `int` | native_property: TurnsSeen |
| [FakeOrichalcum._shouldTrigger](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/FakeOrichalcum.cs#L17) | `bool` | additional_gameplay |
| [FakeSneckoEye._testEnergyCostOverride](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/FakeSneckoEye.cs#L15) | `int` | exclude_test_override |
| [FakeVenerableTeaSet._gainEnergyInNextCombat](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/FakeVenerableTeaSet.cs#L15) | `bool` | native_property: GainEnergyInNextCombat |
| [FishingRod._combatsSeen](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/FishingRod.cs#L17) | `int` | native_property: CombatsSeen |
| [FurCoat._furCoatActIndex](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/FurCoat.cs#L24) | `int` | native_property: FurCoatActIndex |
| [FurCoat.FurCoatCoordCols](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/FurCoat.cs#L54) | `int[]` | native_property: FurCoatCoordCols |
| [FurCoat.FurCoatCoordRows](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/FurCoat.cs#L57) | `int[]` | native_property: FurCoatCoordRows |
| [FurCoat.FurCoatCoordsSet](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/FurCoat.cs#L60) | `bool` | native_property: FurCoatCoordsSet |
| [GalacticDust._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/GalacticDust.cs#L17) | `bool` | exclude_presentation |
| [GalacticDust._starsSpent](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/GalacticDust.cs#L19) | `int` | native_property: StarsSpent |
| [Girya._timesLifted](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Girya.cs#L18) | `int` | native_property: TimesLifted |
| [GoldenCompass._goldenPathAct](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/GoldenCompass.cs#L13) | `int` | native_property: GoldenPathAct |
| [HappyFlower._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/HappyFlower.cs#L22) | `bool` | exclude_presentation |
| [HappyFlower._turnsSeen](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/HappyFlower.cs#L24) | `int` | native_property: TurnsSeen |
| [IronClub._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/IronClub.cs#L16) | `bool` | exclude_presentation |
| [IronClub._cardsPlayed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/IronClub.cs#L18) | `int` | native_property: CardsPlayed |
| [JossPaper._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/JossPaper.cs#L22) | `bool` | exclude_presentation |
| [JossPaper._cardsExhausted](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/JossPaper.cs#L24) | `int` | native_property: CardsExhausted |
| [JossPaper._etherealCount](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/JossPaper.cs#L26) | `int` | additional_gameplay |
| [Kunai._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Kunai.cs#L20) | `bool` | exclude_presentation |
| [Kunai._attacksPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Kunai.cs#L22) | `int` | additional_gameplay |
| [Kusarigama._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Kusarigama.cs#L19) | `bool` | exclude_presentation |
| [Kusarigama._attacksPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Kusarigama.cs#L21) | `int` | additional_gameplay |
| [LastingCandy._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/LastingCandy.cs#L22) | `bool` | exclude_presentation |
| [LastingCandy._combatRewardsSeen](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/LastingCandy.cs#L24) | `int` | native_property: CombatRewardsSeen |
| [LavaLamp._tookDamageThisCombat](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/LavaLamp.cs#L18) | `bool` | native_property: TookDamageThisCombat |
| [LavaRock._hasTriggered](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/LavaRock.cs#L15) | `bool` | native_property: HasTriggered |
| [LetterOpener._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/LetterOpener.cs#L19) | `bool` | exclude_presentation |
| [LetterOpener._skillsPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/LetterOpener.cs#L21) | `int` | additional_gameplay |
| [LizardTail._wasUsed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/LizardTail.cs#L14) | `bool` | native_property: WasUsed |
| [MawBank._hasItemBeenBought](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/MawBank.cs#L15) | `bool` | native_property: HasItemBeenBought |
| [Metronome._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Metronome.cs#L20) | `bool` | exclude_presentation |
| [Metronome._orbsChanneled](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Metronome.cs#L22) | `int` | additional_gameplay |
| [MiniRegent._usedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/MiniRegent.cs#L19) | `bool` | additional_gameplay |
| [MusicBox._wasUsedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/MusicBox.cs#L17) | `bool` | additional_gameplay |
| [MusicBox._cardBeingPlayed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/MusicBox.cs#L19) | `CardModel?` | additional_gameplay |
| [Nunchaku._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Nunchaku.cs#L17) | `bool` | exclude_presentation |
| [Nunchaku._attacksPlayed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Nunchaku.cs#L19) | `int` | native_property: AttacksPlayed |
| [Orichalcum._shouldTrigger](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Orichalcum.cs#L17) | `bool` | additional_gameplay |
| [OrnamentalFan._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/OrnamentalFan.cs#L20) | `bool` | exclude_presentation |
| [OrnamentalFan._attacksPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/OrnamentalFan.cs#L22) | `int` | additional_gameplay |
| [PaelsEye._usedThisCombat](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PaelsEye.cs#L19) | `bool` | additional_gameplay |
| [PaelsEye._wasOwnerPartOfLastPlayerTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PaelsEye.cs#L21) | `bool` | additional_gameplay |
| [PaelsLegion._skin](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PaelsLegion.cs#L24) | `string` | native_property: Skin |
| [PaelsLegion._cooldown](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PaelsLegion.cs#L26) | `int` | additional_gameplay |
| [PaelsLegion._triggeredBlockLastTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PaelsLegion.cs#L28) | `bool` | additional_gameplay |
| [PaelsLegion._affectedCardPlay](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PaelsLegion.cs#L30) | `CardPlay?` | additional_gameplay |
| [PaelsTears._hadLeftoverEnergy](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PaelsTears.cs#L17) | `bool` | additional_gameplay |
| [PaelsTooth._serializableCards](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PaelsTooth.cs#L23) | `List<SerializableCard>` | native_property: SerializableCards |
| [PaelsWing._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PaelsWing.cs#L22) | `bool` | exclude_presentation |
| [PaelsWing._rewardsSacrificed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PaelsWing.cs#L24) | `int` | native_property: RewardsSacrificed |
| [PenNib._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PenNib.cs#L17) | `bool` | exclude_presentation |
| [PenNib._attacksPlayed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PenNib.cs#L19) | `int` | native_property: AttacksPlayed |
| [PenNib._attackToDouble](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PenNib.cs#L21) | `CardModel?` | additional_gameplay |
| [Pendulum._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Pendulum.cs#L19) | `bool` | exclude_presentation |
| [Pendulum._turnsSeen](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Pendulum.cs#L21) | `int` | native_property: TurnsSeen |
| [Permafrost._activatedThisCombat](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Permafrost.cs#L16) | `bool` | additional_gameplay |
| [Pocketwatch._cardsPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Pocketwatch.cs#L19) | `int` | additional_gameplay |
| [Pocketwatch._cardsPlayedLastTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Pocketwatch.cs#L21) | `int` | additional_gameplay |
| [PollinousCore._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PollinousCore.cs#L19) | `bool` | exclude_presentation |
| [PollinousCore._turnsSeen](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PollinousCore.cs#L21) | `int` | native_property: TurnsSeen |
| [PumpkinCandle._kindleCount](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/PumpkinCandle.cs#L20) | `int` | native_property: KindleCount |
| [RainbowRing._attacksPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/RainbowRing.cs#L19) | `int` | additional_gameplay |
| [RainbowRing._skillsPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/RainbowRing.cs#L21) | `int` | additional_gameplay |
| [RainbowRing._powersPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/RainbowRing.cs#L23) | `int` | additional_gameplay |
| [RainbowRing._activationCountThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/RainbowRing.cs#L25) | `int` | additional_gameplay |
| [RedSkull._strengthApplied](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/RedSkull.cs#L19) | `bool` | additional_gameplay |
| [Regalite._usedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Regalite.cs#L18) | `bool` | additional_gameplay |
| [RuinedHelmet._usedThisCombat](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/RuinedHelmet.cs#L13) | `bool` | additional_gameplay |
| [SeaGlass._characterId](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/SeaGlass.cs#L23) | `ModelId?` | native_property: CharacterId |
| [Shuriken._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Shuriken.cs#L20) | `bool` | exclude_presentation |
| [Shuriken._attacksPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Shuriken.cs#L22) | `int` | additional_gameplay |
| [SilkenTress._isUsed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/SilkenTress.cs#L16) | `bool` | native_property: IsUsed |
| [SilverCrucible._timesUsed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/SilverCrucible.cs#L16) | `int` | native_property: TimesUsed |
| [SilverCrucible._treasureRoomsEntered](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/SilverCrucible.cs#L18) | `int` | native_property: TreasureRoomsEntered |
| [SneckoEye._testEnergyCostOverride](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/SneckoEye.cs#L17) | `int` | exclude_test_override |
| [StoneCalendar._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/StoneCalendar.cs#L20) | `bool` | exclude_presentation |
| [SwordOfStone._elitesDefeated](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/SwordOfStone.cs#L15) | `int` | native_property: ElitesDefeated |
| [TeaOfDiscourtesy._combatsLeft](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/TeaOfDiscourtesy.cs#L19) | `int` | native_property: CombatsLeft |
| [ThrowingAxe._usedThisCombat](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/ThrowingAxe.cs#L10) | `bool` | additional_gameplay |
| [TouchOfOrobas._starterRelic](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/TouchOfOrobas.cs#L21) | `ModelId?` | native_property: StarterRelic |
| [TouchOfOrobas._upgradedRelic](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/TouchOfOrobas.cs#L23) | `ModelId?` | native_property: UpgradedRelic |
| [TouchOfOrobas._extraHoverTips](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/TouchOfOrobas.cs#L25) | `List<IHoverTip>` | exclude_presentation |
| [ToyBox._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/ToyBox.cs#L22) | `bool` | exclude_presentation |
| [ToyBox._combatsSeen](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/ToyBox.cs#L24) | `int` | native_property: CombatsSeen |
| [TuningFork._isActivating](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/TuningFork.cs#L17) | `bool` | exclude_presentation |
| [TuningFork._skillsPlayed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/TuningFork.cs#L19) | `int` | native_property: SkillsPlayed |
| [UnsettlingLamp._triggeringCard](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/UnsettlingLamp.cs#L16) | `CardModel?` | additional_gameplay |
| [UnsettlingLamp._doubledPowers](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/UnsettlingLamp.cs#L18) | `List<PowerModel>?` | additional_gameplay |
| [UnsettlingLamp._isFinishedTriggering](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/UnsettlingLamp.cs#L20) | `bool` | additional_gameplay |
| [Vambrace._triggeringCard](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Vambrace.cs#L15) | `CardModel?` | additional_gameplay |
| [Vambrace._blockGainedThisCombat](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/Vambrace.cs#L17) | `bool` | additional_gameplay |
| [VelvetChoker._cardsPlayedThisTurn](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/VelvetChoker.cs#L18) | `int` | additional_gameplay |
| [VenerableTeaSet._gainEnergyInNextCombat](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/VenerableTeaSet.cs#L15) | `bool` | native_property: GainEnergyInNextCombat |
| [WingedBoots._timesUsed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/WingedBoots.cs#L18) | `int` | native_property: TimesUsed |
| [WongosMysteryTicket._combatsFinished](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/WongosMysteryTicket.cs#L20) | `int` | native_property: CombatsFinished |
| [WongosMysteryTicket._gaveRelic](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Relics/WongosMysteryTicket.cs#L22) | `bool` | native_property: GaveRelic |

## Monsters

| Source field | Type | How to handle it |
| --- | --- | --- |
| [Aeonglass._additionalStrength](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Aeonglass.cs#L30) | `int` | additional_gameplay |
| [Aeonglass._witherUpgradeCount](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Aeonglass.cs#L32) | `int` | additional_gameplay |
| [Axebot._stockOverrideAmount](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Axebot.cs#L36) | `int?` | additional_gameplay |
| [Axebot._shouldPlaySpawnAnimation](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Axebot.cs#L38) | `bool` | exclude_presentation |
| [BowlbugRock._isOffBalance](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/BowlbugRock.cs#L26) | `bool` | additional_gameplay |
| [CalcifiedCultist._attackSfxStrength](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/CalcifiedCultist.cs#L26) | `float` | exclude_presentation |
| [CeremonialBeast._isStunnedByPlowRemoval](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/CeremonialBeast.cs#L47) | `bool` | exclude_presentation |
| [CeremonialBeast._isInSecondPhase](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/CeremonialBeast.cs#L49) | `bool` | exclude_presentation |
| [CeremonialBeast._inMidCharge](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/CeremonialBeast.cs#L51) | `bool` | exclude_presentation |
| [CeremonialBeast._beastCryState](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/CeremonialBeast.cs#L53) | `MoveState` | ai_state_alias |
| [Chomper._screamFirst](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Chomper.cs#L26) | `bool` | initial_configuration |
| [CorpseSlug._isRavenous](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/CorpseSlug.cs#L35) | `bool` | exclude_presentation |
| [CorpseSlug._starterMoveIdx](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/CorpseSlug.cs#L37) | `int` | initial_configuration |
| [CrossbowRubyRaider._isCrossbowReloaded](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/CrossbowRubyRaider.cs#L22) | `bool` | exclude_presentation |
| [Crusher._background](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Crusher.cs#L37) | `NKaiserCrabBossBackground?` | exclude_presentation |
| [CubexConstruct._isBurrowed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/CubexConstruct.cs#L38) | `bool` | exclude_presentation |
| [CubexConstruct._isCharging](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/CubexConstruct.cs#L40) | `bool` | exclude_presentation |
| [DampCultist._attackSfxStrength](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/DampCultist.cs#L24) | `float` | exclude_presentation |
| [DecimillipedeSegment._starterMoveIdx](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/DecimillipedeSegment.cs#L29) | `int` | initial_configuration |
| [DecimillipedeSegment._deadState](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/DecimillipedeSegment.cs#L33) | `MoveState` | ai_state_alias |
| [DevotedSculptor._ritualGain](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/DevotedSculptor.cs#L21) | `int` | exclude_fixed_definition |
| [Fabricator._lastSpawned](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Fabricator.cs#L37) | `MonsterModel?` | additional_gameplay |
| [FatGremlin._isAwake](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/FatGremlin.cs#L27) | `bool` | exclude_presentation |
| [FrogKnight._hasBeetleCharged](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/FrogKnight.cs#L31) | `bool` | additional_gameplay |
| [FuzzyWurmCrawler._isPuffed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/FuzzyWurmCrawler.cs#L27) | `bool` | exclude_presentation |
| [GasBomb._hasExploded](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/GasBomb.cs#L24) | `bool` | exclude_presentation |
| [GremlinMerc._hasSpoken](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/GremlinMerc.cs#L24) | `bool` | exclude_presentation |
| [Inklet._middleInklet](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Inklet.cs#L26) | `bool` | initial_configuration |
| [KinFollower._startsWithDance](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/KinFollower.cs#L42) | `bool` | initial_configuration |
| [KinPriest._speechUsed](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/KinPriest.cs#L47) | `bool` | exclude_presentation |
| [KnowledgeDemon._curseOfKnowledgeCounter](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/KnowledgeDemon.cs#L66) | `int` | additional_gameplay |
| [KnowledgeDemon._isBurnt](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/KnowledgeDemon.cs#L72) | `bool` | exclude_presentation |
| [LagavulinMatriarch._isAwake](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/LagavulinMatriarch.cs#L43) | `bool` | exclude_presentation |
| [LagavulinMatriarch._isShellAwake](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/LagavulinMatriarch.cs#L45) | `bool` | exclude_presentation |
| [LagavulinMatriarch._sleepingVfx](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/LagavulinMatriarch.cs#L47) | `NSleepingVfx?` | exclude_presentation |
| [LivingFog._bloatAmount](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/LivingFog.cs#L22) | `int` | exclude_fixed_definition |
| [LouseProgenitor._curled](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/LouseProgenitor.cs#L32) | `bool` | exclude_presentation |
| [MechaKnight._isWoundUp](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/MechaKnight.cs#L38) | `bool` | exclude_presentation |
| [Nibbit._isFront](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Nibbit.cs#L22) | `bool` | additional_gameplay |
| [Nibbit._isAlone](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Nibbit.cs#L24) | `bool` | additional_gameplay |
| [OwlMagistrate._isFlying](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/OwlMagistrate.cs#L33) | `bool` | exclude_presentation |
| [PhantasmalGardener._enlargeTriggers](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/PhantasmalGardener.cs#L22) | `int` | exclude_presentation |
| [PhantasmalGardener.CurrentScale](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/PhantasmalGardener.cs#L67) | `float` | exclude_presentation |
| [PunchConstruct._startsWithFastPunch](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/PunchConstruct.cs#L23) | `bool` | initial_configuration |
| [PunchConstruct._startingHpReduction](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/PunchConstruct.cs#L25) | `int` | initial_configuration |
| [Queen._hasAmalgamDied](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Queen.cs#L38) | `bool` | additional_gameplay |
| [Queen._amalgam](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Queen.cs#L40) | `Creature?` | additional_gameplay |
| [Queen._burnBrightForMeState](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Queen.cs#L42) | `MoveState` | ai_state_alias |
| [Queen._enragedState](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Queen.cs#L44) | `MoveState` | ai_state_alias |
| [Rocket._background](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Rocket.cs#L31) | `NKaiserCrabBossBackground?` | exclude_presentation |
| [ScrollOfBiting._starterMoveIdx](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/ScrollOfBiting.cs#L27) | `int` | initial_configuration |
| [SlumberingBeetle._isAwake](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/SlumberingBeetle.cs#L34) | `bool` | exclude_presentation |
| [SlumberingBeetle._sleepingVfx](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/SlumberingBeetle.cs#L36) | `NSleepingVfx?` | exclude_presentation |
| [SneakyGremlin._isAwake](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/SneakyGremlin.cs#L19) | `bool` | exclude_presentation |
| [SoulFysh._isInvisible](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/SoulFysh.cs#L46) | `bool` | exclude_presentation |
| [SpinyToad._isSpiny](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/SpinyToad.cs#L27) | `bool` | exclude_presentation |
| [TerrorEel._terrorState](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/TerrorEel.cs#L31) | `MoveState` | ai_state_alias |
| [TestSubject._deadState](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/TestSubject.cs#L61) | `MoveState` | ai_state_alias |
| [TestSubject._respawns](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/TestSubject.cs#L63) | `int` | additional_gameplay |
| [TestSubject._extraMultiClawCount](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/TestSubject.cs#L65) | `int` | additional_gameplay |
| [TheInsatiable._hasLiquified](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/TheInsatiable.cs#L52) | `bool` | exclude_presentation |
| [TheObscura._hasSummoned](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/TheObscura.cs#L28) | `bool` | exclude_presentation |
| [ThievingHopper._isHovering](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/ThievingHopper.cs#L73) | `bool` | exclude_presentation |
| [Toadpole._isFront](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Toadpole.cs#L24) | `bool` | additional_gameplay |
| [ToughEgg._hatched](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/ToughEgg.cs#L26) | `bool` | exclude_presentation |
| [ToughEgg._afterHatchedState](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/ToughEgg.cs#L34) | `MonsterState?` | ai_state_alias |
| [ToughEgg._isHatched](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/ToughEgg.cs#L36) | `bool` | additional_gameplay |
| [ToughEgg._hatchPos](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/ToughEgg.cs#L38) | `Vector2?` | exclude_presentation |
| [Tunneler._isStunned](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Tunneler.cs#L38) | `bool` | exclude_presentation |
| [TwoTailedRat._starterMoveIndex](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/TwoTailedRat.cs#L38) | `int` | initial_configuration |
| [TwoTailedRat._turnsUntilSummonable](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/TwoTailedRat.cs#L40) | `int` | additional_gameplay |
| [TwoTailedRat._callForBackupCount](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/TwoTailedRat.cs#L42) | `int` | additional_gameplay |
| [Vantom._scaleTween](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Vantom.cs#L60) | `Tween?` | exclude_presentation |
| [WaterfallGiant._currentPressureGunDamage](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/WaterfallGiant.cs#L39) | `int` | additional_gameplay |
| [WaterfallGiant._steamEruptionDamage](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/WaterfallGiant.cs#L41) | `int` | additional_gameplay |
| [WaterfallGiant._aboutToBlowState](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/WaterfallGiant.cs#L43) | `MoveState` | ai_state_alias |
| [WaterfallGiant._isAboutToBlow](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/WaterfallGiant.cs#L45) | `bool` | exclude_presentation |
| [WaterfallGiant._pressureBuildupIdx](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/WaterfallGiant.cs#L47) | `int` | exclude_presentation |
| [Wriggler._startStunned](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Monsters/Wriggler.cs#L28) | `bool` | initial_configuration |

## Potions

| Source field | Type | How to handle it |
| --- | --- | --- |
| [Ashwater._tint](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/Ashwater.cs#L17) | `Color` | exclude_presentation |
| [SneckoOil._testEnergyCostOverride](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Potions/SneckoOil.cs#L19) | `int` | exclude_test_override |

## Orbs

| Source field | Type | How to handle it |
| --- | --- | --- |
| [DarkOrb._evokeVal](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Orbs/DarkOrb.cs#L17) | `decimal` | additional_gameplay |
| [GlassOrb._passiveVal](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Orbs/GlassOrb.cs#L15) | `decimal` | additional_gameplay |

## Enchantments

| Source field | Type | How to handle it |
| --- | --- | --- |
| [Glam._usedThisCombat](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Enchantments/Glam.cs#L15) | `bool` | additional_gameplay |
| [Momentum._extraDamage](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Enchantments/Momentum.cs#L10) | `int` | additional_gameplay |
| [Slither._testEnergyCostOverride](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Enchantments/Slither.cs#L12) | `int` | exclude_test_override |

## Afflictions

No declared fields matched this scan. Base-class fields are covered in the runtime
audit.

## Encounters

| Source field | Type | How to handle it |
| --- | --- | --- |
| [BattlewornDummyEventEncounter._ranOutOfTime](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Encounters/BattlewornDummyEventEncounter.cs#L9) | `bool` | native_room_property |
| [GremlinMercNormal._goldWasStolen](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Encounters/GremlinMercNormal.cs#L18) | `bool` | additional_gameplay |

## Modifiers

| Source field | Type | How to handle it |
| --- | --- | --- |
| [CharacterCards._characterModel](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Modifiers/CharacterCards.cs#L13) | `ModelId?` | native_property: CharacterModel |
| [Hoarder._cardsToSkip](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Modifiers/Hoarder.cs#L11) | `HashSet<CardModel>` | additional_gameplay |

## Characters

| Source field | Type | How to handle it |
| --- | --- | --- |
| [DeprecatedCharacter._mockCardPool](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Characters/DeprecatedCharacter.cs#L18) | `MockCardPool?` | exclude_test_override |
| [Deprived._mockCardPool](../../build/combat-state-audit/decompiled/MegaCrit.Sts2.Core.Models.Characters/Deprived.cs#L14) | `MockCardPool?` | exclude_test_override |
