# Combat runtime audit against the installed game

This audit identifies gameplay state that the native run save leaves out. It explains
what extra data parity tests need, what can be left out, and which questions still need
checks against the game.

See the [shared terms](README.md#terms-used-in-these-sheets) for definitions of parity, snapshot, boundary, and model ID.

## On this page

- [Scope: what to include](#scope-what-to-include)
- [Data the native save already contains](#data-the-native-save-already-contains)
- [Capture points and top-level additions](#capture-points-and-top-level-additions)
- [Combat, turns, and player state](#combat-turns-and-player-state)
- [Creatures and powers](#creatures-and-powers)
- [Cards and cost modifiers](#cards-and-cost-modifiers)
- [Enchantments, orbs, relics and potions](#enchantments-orbs-relics-and-potions)
- [Enemy AI](#enemy-ai)
- [History that affects gameplay](#history-that-affects-gameplay)
- [Pending choices and paused effects](#pending-choices-and-paused-effects)
- [Findings and open questions](#findings-and-open-questions)
- [How to repeat the audit](#how-to-repeat-the-audit)

Inspected on 2026-10-18. Target: v0.111.0, release commit `41cef1ea`; `sts2.dll` SHA-256
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`. The source
references below are to a local ILSpy 9.1.0.7988 decompilation of that assembly under
ignored `build/combat-state-audit/decompiled/`.

This audit starts from the game rather than the earlier parity proposal. The [native v20
schema](schemas/serializable-run-v20.schema.json) is unchanged. This document identifies
source-backed additions and exclusions; it is not an implemented exporter or a
declaration that full-game state completeness has been verified. The [model-member
inventory](combat-model-field-audit.md) records the individual model fields examined,
including fields already saved and fields excluded.

## Scope: what to include

Single-player gameplay, at a settled normal-input boundary, an unresolved required
player choice, or completed-run boundary. Compare mutable gameplay data held by both
engines. Preserve object IDs, links to the same object, and collection order wherever it affects gameplay. Do not export
presentation objects or require the simulator to reproduce the engine's class layout.

A value belongs in the addition only if it is absent from native serialization and is
read by gameplay, determines pending input, or preserves a necessary relationship
between gameplay objects. Fixed model definitions and values computed entirely from
captured state do not need another field. A field that happens to be private or mutable
does not automatically qualify.

This distinction changes earlier assumptions:

* `Player.PopulateCombatState` really does clone deck cards and set `DeckVersion`.
  The link is real; a separate root `instances` table is not required to represent it.
* `MonsterModel.Rng` exists, but the inspected concrete use is ToughEgg skin selection.
  `MonsterModel.RollMove` uses **`RunRng.MonsterAi`**, already covered by native run RNG.
  Do not add a cosmetic monster RNG merely because it exists.
* There is no native generic list of logical instruction frames corresponding to the
  previous proposal's `EffectState`. Do not invent one as if it were a game property.
* `NetFullCombatState` is the game's checksum export, not an exhaustive gameplay save.
  It is useful corroboration, but omits important runtime data and must not define
  the completeness limit for our parity tests.

## Data the native save already contains

`RunManager.ToSave` calls `Player.ToSerializable`, which serializes the **permanent
deck**, relics, potion slots, current/max HP, gold, base max energy, base orb-slot
count, run/player RNG, odds, pools and native history. It does not serialize
`PlayerCombatState` or the active `CombatState`.

`CardModel.ToSerializable` includes `Id`, `CurrentUpgradeLevel`, `FloorAddedToDeck`,
`SavedProperties.From(this)` and `Enchantment.ToSerializable`. Reuse these fields for
each combat card too; combat copies can have different upgrades and saved values.
`RelicModel.ToSerializable` similarly preserves its saved properties. Native
`SerializablePotion` contains model ID and slot index.

Examples already covered by native saved properties include GeneticAlgorithm's
`CurrentBlock`/`IncreasedBlock`, TheScythe's `CurrentDamage`/`IncreasedDamage`,
HappyFlower's `TurnsSeen`, PenNib's `AttacksPlayed`, LizardTail's `WasUsed`, and
RelicModel's `IsWax`/`IsMelted`. Do not add parallel counters for these.

Native room records already have `encounter_id`, `encounter_state`, room type,
parent-event relationships and extra rewards. `pre_finished_room` does not mean the
active combat room. An active-room addition should reuse `SerializableRoom`, not
redefine those fields. `BattlewornDummyEventEncounter.RanOutOfTime` is already written
by `SaveCustomState` as `encounter_state["RanOutOfTime"]`.

Sources: `RunManager.ToSave`, `Player.ToSerializable`, `CardModel.ToSerializable`,
`RelicModel.ToSerializable`, `PotionModel.ToSerializable`, `CombatRoom.ToSerializable`,
`EncounterModel.SaveCustomState` and `SavedProperties.From`.

## Capture points and top-level additions

These are the concepts that need representation at the root; field spelling and
placement can be finalized after the outstanding boundary checks below:

| Addition | Purpose |
| --- | --- |
| Boundary kind | `awaiting_input` or `run_complete`; supplied by the test driver after establishing the boundary |
| Run-ending reason | Null while ongoing; victory, death or abandonment when complete; observe the actual ending path |
| Active room | Native `SerializableRoom` for the current room, rather than overloading `pre_finished_room` |
| Combat runtime | The missing gameplay data in the following sections, when combat state is relevant |
| Pending choice | The unresolved selection request, when there is one; null during ordinary free card play |

Build hashes, recorder versions and action/boundary sequence numbers belong to the test
recording envelope. No profile selector, restore capability, English action catalog,
generic instance registry or additional RNG format is needed.

`RunManager.WinRun`/`OnEnded(bool isVictory)` and `IsAbandoned` provide the outcome
evidence. Abandonment also takes a death path, so HP alone cannot distinguish it.
`WinRun` calls `OnEnded(true)` and then `GuaranteeKillAllPlayers` too; a zero-HP
post-completion player is therefore not sufficient evidence of defeat either.
`EndCombatInternal` clears history and removes combat cards; `ProcessPendingLoss` does
not perform the same victory cleanup. At run completion, capture the state that actually
remains at the agreed boundary, not a synthetic retained pre-cleanup combat.

## Combat, turns, and player state

| Native source | Missing gameplay values |
| --- | --- |
| `CombatState` | `RoundNumber`, `CurrentSide`, ordered `Allies` and `Enemies`, `EscapedCreatures` |
| `CombatManager` / `CombatTurnState` | `IsInProgress`, `IsStarting`, `IsEnemyTurnStarted`, `EndingPlayerTurnPhaseOne`, `EndingPlayerTurnPhaseTwo`, `PendingLoss` presence, `PlayerActionsDisabled`, players taking extra turns, readiness at a suspended turn transition, active card/potion effect depth |
| `PlayerCombatState` | `TurnNumber`, `Phase`, `Energy`, `Stars`, the five ordered card piles, pet relationships, orb queue |
| `Player` | `CanUseOrRemovePotions`, hook-active state when not implied by the agreed boundary's alive/dead state |
| `OrbQueue` | `Capacity`, ordered `Orbs` |
| `CombatRoom` / `EncounterModel` | Active native room record, spawned-enemy definition set used by reward proportions, extra encounter gameplay fields in the member inventory |

Use the real player phases: `None`, `Start`, `AutoPrePlay`, `Play`, `AutoPostPlay`,
`End`. Do not replace them with invented phase labels. `TurnNumber` is not redundant
with `RoundNumber`: an extra player turn increments only the former.

Control flags that are constant at a proven boundary can be boundary invariants instead
of serialized fields. For example, a normal `Play` boundary should not be represented as
a half-completed end turn. They cannot all be assumed constant at selection boundaries:
input can suspend turn-start/end hooks and nested card effects. Actual gameplay control
values should be distinguished from locks, task signals, timeouts, network sequence
numbers and cancellation-token objects, which are excluded.

`CombatState.IterateHookListeners` derives listener order from ordered creatures,
powers, inventories, orbs and piles, then modifiers. Therefore a second generic
`trigger_order` array duplicates that information at a settled boundary. A suspended
hook iteration can retain a listener snapshot/cursor; that belongs to the specific
pending operation, not a fictitious global trigger registry.

## Creatures and powers

For enemies/pets, add native monster ID, HP/max HP, block, side, slot name, pet-owner
relationship and ordered powers. Use existing native player HP/max HP for the player
rather than adding a second independently authoritative copy. Creature references must
preserve links to the same object; actual `CombatId` is available, but transport ID
allocation need not become a gameplay equality requirement.

`Creature.HpDisplay` is an exception to its presentation-sounding name: Hellraiser
checks `HpDisplay.IsInfinite()` to cap automatic plays. The gameplay-relevant display
mode therefore must be represented. `IsAlive` is simply `CurrentHp > 0`; do not add a
redundant alive flag. `IsHittable`, primary/secondary-enemy status and other hook-
computed predicates should be derived from the captured inputs.

Power common state absent from native saves:

* Model ID and amount, in owner power-list order.
* `AmountOnTurnStart` and `SkipNextDurationTick`.
* `Applier` and `Target` references when present. Owner is implicit in the containing
  creature; a second owner field is unnecessary.
* Mutable gameplay `DynamicVars` base values where not reconstructible from the
  captured model state.
* Concrete model-specific fields, including nested `PowerModel._internalData`.

Examples requiring explicit data are Dampen's caster set and old upgrade levels per
card, Nightmare's selected card clone, Panache's `alreadyApplied` and `CardsLeft`,
Nemesis's intangible toggle, and Vigor's attack association and amount captured at
attack start. Merely storing power amount loses these distinctions. The complete
declared-member list is in the companion inventory.

Capture a referenced card/creature even if it is not currently in an active pile or
creature list. Nightmare stores a floating clone for next turn. History and stolen- card
powers can also retain objects outside current collections. Do not capture unreferenced
preview cards solely because `CombatState._allCards` contains them.

## Cards and cost modifiers

The five native piles are `Hand`, `Draw`, `Discard`, `Exhaust`, `Play`. Preserve the
actual sequence; `CardPile.AddInternal` documents bottom insertion and the draw command
consumes from the top. A shared card collection plus references from piles is necessary
to preserve links to the same card. The permanent deck remains in native `players[].deck`; deck
links can reference its entries directly.

Each gameplay-reachable combat card needs its native `SerializableCard` fields plus:

| Native source member | Additional meaning |
| --- | --- |
| `DeckVersion` | Link to its permanent-deck card, or null |
| `CloneOf`, `IsDupe` | Clone/duplicate relationships used by gameplay; do not substitute model equality |
| `BaseReplayCount` | Locally stored replay count |
| Local keyword state (`_keywords`) | Instance-added/removed keywords, distinct from hook-derived `Keywords` |
| `ExhaustOnNextPlay`, `_hasSingleTurnRetain`, `_hasSingleTurnSly` | Temporary lifecycle/keyword flags |
| `HasBeenRemovedFromState` | Relevant when a removed card remains reachable by gameplay; used to exclude stale hook listeners |
| `DynamicVars` | Mutable numeric base values, such as accumulated card damage and counters |
| `EnergyCost._base` | Stored base energy cost, including custom changes |
| `EnergyCost._localModifiers` | Ordered modifier records described below |
| `EnergyCost._capturedXValue` | Most recently captured energy X value, where applicable |
| `BaseStarCost`, `_temporaryStarCosts`, `LastStarsSpent` | Stored base star cost, ordered temporary star costs and captured star spend |
| `Affliction` | ID and amount; afflictions are combat-only and absent from `SerializableCard` |
| Enchantment runtime | Additional gameplay state beyond its native saved ID/amount/properties |
| Concrete subclass fields | Card-specific state listed in the member inventory |

`LocalCostModifier` has exactly four stored properties:

* `Amount`: int.
* `Type`: `Absolute` or `Relative`.
* `Expiration`: bit flags; `EndOfCombat = 0`, `EndOfTurn = 2`, `WhenPlayed = 4`.
* `IsReduceOnly`: bool.

Keep modifier application order and combinations of expiration flags. Do not invent
remaining-use counters or arbitrary durations: this implementation has neither in
`LocalCostModifier`. `TemporaryCardCost`, used for stars, has exactly `Cost`,
`ClearsWhenTurnEnds`, `ClearsWhenCardIsPlayed`.

Canonical energy/star cost, X-cost classification, card type, rarity, tags and target
rules normally come from the pinned definition; do not duplicate them as state. Resolved
current cost is computed from stored modifiers and captured powers/relics.
`CurrentTarget`/`CurrentPlayIndex` can be represented through the current `CardPlay`
while playing, rather than repeated as independent card fields.

`DynamicVar.BaseValue` is the gameplay number. `PreviewValue`, `EnchantedValue` and
`WasJustUpgraded` are display/preview state; exclude those as separate mutable gameplay
fields. Do not run preview generation to export state. Calculated-variable formulas are
immutable rules; their mutable input variables are what must be compared.

## Enchantments, orbs, relics and potions

* **Enchantments:** preserve gameplay `Status` in addition to native saved data.
  Swift and Sown only execute while normal; Vigorous stops adding damage when disabled.
  Glam adds `_usedThisCombat`; Momentum adds `_extraDamage`. Slither's test override
  is excluded. Do not duplicate native amount/properties.
* **Orbs:** preserve identity, ownership/queue order and capacity. DarkOrb stores
  decimal `_evokeVal`; GlassOrb stores decimal `_passiveVal`. Other passive/evoke
  values are definition/hook-derived and need no second source of truth.
* **Relics:** attach only additional model-specific gameplay data to the existing
  relic entries. ArtOfWar's attack flags, Pocketwatch's turn counts and UnsettlingLamp's
  triggering card/power relationships are examples. A generic `Status` export would
  mostly capture icon highlighting; inspect gameplay consumers rather than including
  all statuses automatically. `StackCount` has no gameplay readers in the inspected
  build. Wax/melted state and saved counters are already native.
* **Potions:** native inventory already supplies ID and slot. A potion removed from
  inventory during use may remain the source of a pending selection; preserve that
  source/target and relevant captured values in the pending operation. There are no
  additional non-test, non-presentation concrete potion fields in the scanned
  subclasses. Do not add speculative potion charge/modifier fields.

If a removed relic/potion remains in a suspended hook snapshot, preserve its removal
status and owner relation until that reference is no longer gameplay-relevant.
`RunState.Contains` explicitly checks those flags. Do not retain unreachable garbage.

## Enemy AI

Capture actual state-machine data, not intent descriptions:

* `MonsterModel.NextMove`, and `MonsterMoveStateMachine._currentState` and
  `_initialState` by state identity.
* `StateLog` in order and `_performedFirstMove`.
* `MoveState._performedAtLeastOnce`, `MustPerformOnceBeforeTransitioning`, and
  effective follow-up state, including dynamically created `STUNNED` moves.
* Changed transition/configuration values not fixed by the model definition.
* `SpawnedThisTurn`; it controls whether the monster takes a turn.
* `IsPerformingMove` when a move is suspended; death removal reads it.
* Concrete gameplay subclass fields from the member inventory: for example
  BowlbugRock's off-balance flag, KnowledgeDemon's counter, Fabricator's last spawned
  definition, TestSubject's respawn count and WaterfallGiant's captured attack damage.

Do not create an independent speculative "next turn" move. `NextMove` is the game's
scheduled move; it can coincide with the state-machine current state, in which case one
shared reference suffices. Intents and damage previews derived from the move and
captured gameplay values do not need separate copies in a state-only comparison. Capture
any actual mutable intent parameters not otherwise represented, rather than serializing
delegates or derived English descriptions.

## History that affects gameplay

Native map-point/run history does not contain `CombatManager.History`. This history is
read by gameplay, not merely used for diagnostics. For example Normality counts started
plays, Feral reads their captured energy value, DeathMarch distinguishes hand draws, and
BansheesCry uses the saved `WasEthereal` flag on finished plays.

The source contains gameplay consumers for these entry kinds:

`BlockGainedEntry`, `CardAfflictedEntry`, `CardDiscardedEntry`, `CardDrawnEntry`,
`CardExhaustedEntry`, `CardGeneratedEntry`, `CardPlayStartedEntry`,
`CardPlayFinishedEntry`, `CreatureAttackedEntry`, `DamageReceivedEntry`,
`EnergySpentEntry`, `OrbChanneledEntry`, `PowerReceivedEntry`, `StarsModifiedEntry`.

Preserve the values the queries use: actor/source/target/card/power/orb references,
amounts and value-property flags, hand-draw origin, creator, card-play identity,
autoplay status, captured resource values, and finished-play `WasEthereal`. Retain the
round, side and player turn numbers used by `HappenedThisTurn` and
`HappenedLastPlayerTurn`; history cannot be replaced by a single combat play count. Do
not duplicate historical human-readable descriptions.

No code that reads these entries for model behavior was found for `MonsterPerformedMoveEntry`, `PotionUsedEntry` or
`SummonedEntry`; their diagnostic history need not be included just because the engine
logs them. AI state history remains necessary independently. The bounded consumer search
does not justify dropping these entries if another gameplay reader is found during
exporter verification.

## Pending choices and paused effects

Use the actual selection inputs: ordered candidate identities and the gameplay members
of `CardSelectorPrefs`: `MinSelect`, `MaxSelect`, `RequireManualConfirmation`,
`Cancelable`. Preserve the choosing player and relevant source model/action
relationships. Rendered prompts, preview flags, glow predicates and UI sort delegates
are not gameplay state. There is no native generic `allow_duplicates` property to copy
from these preferences.

`PlayerChoiceContext` and concrete action/context classes expose the involved models and
action owner. Source context is not enough to represent every suspended effect:
`CardModel.OnPlayWrapper` also holds real `CardPlay`, replay-loop count and result
location values across the selection. `CardPlay` contains card, player, target, result
pile, `ResourceInfo`, autoplay flag, play index and total play count. `ResourceInfo` has
`EnergySpent`, `EnergyValue`, `StarsSpent`, `StarValue`; cost and amount spent differ
for autoplay. `CardLocation` also carries destination player and insertion position,
beyond result pile alone.

These are actual gameplay values to compare when live. Do not serialize the task
builder, CLR state number, awaiters, delegates, transport IDs or a C++ instruction
index. Suspended loops/hooks may hold already-captured collections, cursors and amounts
which require per-operation instrumentation. Comparing model fields alone does not prove
equality of those locals.

## Findings and open questions

The common object fields and companion declared-member inventory are grounded in the
pinned DLL. A live two-turn starter probe confirms distinct combat/deck objects, deck
links, separate monster RNG objects, native card serialization, local cost modifier
cleanup, persistent combat history and repeated-export equality. It also adds an
isolated Survivor to the test hand and captures its real discard-selection boundary:
energy is already paid, block is already granted, the card is in `Play`, and history
contains a started `CardPlay` without its finished entry. Resolving the choice completes
the play. The CLI's selector bypass leaves the action state as `Executing`, reinforcing
that raw scheduler labels cannot define logical input boundaries. These checks are about
accessibility/representation, not complete behavior coverage.

An **exact, minimal, all-mechanics boundary schema is not yet established** by that
probe or by the declaration inventory. Before claiming it, the remaining work is:

1. Instrument all selection-producing operation paths to enumerate live captured
   gameplay values, including nested autoplay and suspended hook iterations.
2. Verify which control/model fields can actually survive those input boundaries,
   and derive or omit invariant/redundant values rather than adding every candidate.
3. Check generated/mutated AI transitions and intent parameters against their static
   definitions so delegates and immutable graph structure are not dumped as state.
4. Capture representative complete-run endings and agree on post-cleanup timing.

These are explicit audit gaps, not generic `runtime_state` placeholders that claim to
solve them. The [replacement parity proposal](proposals/run-parity-spec.md) now uses
these findings for its initial concrete contract and explicitly retains these coverage
gaps. It does not claim complete coverage. The native v20 schema is deliberately
unchanged.

## How to repeat the audit

Decompile the pinned assembly locally with ILSpy; do not commit game sources:

```sh
DOTNET_ROLL_FORWARD=Major ilspycmd --disable-updatecheck -p \
  -o build/combat-state-audit/decompiled \
  '/path/to/game/data_sts2_windows_x86_64/sts2.dll'
python3 tools/oracle/check_combat_fields.py --game-dir '/path/to/game'
```

The live probe builds a disposable copy of the pinned CLI, leaves the submodule and
installed game untouched, and writes evidence/logs under `build/combat-state-audit/`.
Its cost-modifier example acts on an isolated mutable Bash, not a live combat card. Its
Survivor scenario explicitly adds one card to the temporary test combat. It is a bounded
audit tool, not a production exporter, importer or parity fixture.
