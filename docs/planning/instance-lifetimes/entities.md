# Entity, power and encounter lifetimes

This is source-derived lifetime evidence for the pinned victory scope, not a new
runtime parity result. Source paths below are relative to the local ILSpy tree
`build/combat-state-audit/decompiled/`; `entities.json` preserves exact hashes and
citation ranges. Native object reachability is evidence for semantic relationships,
not a requirement to reproduce garbage collection or allocate one C++ object per
native object.

## Reference graph and lifecycle distinctions

| Relationship | Source evidence | Required preservation |
| --- | --- | --- |
| Combat → ordered allies/enemies; creature → combat, local ID, monster; monster → creature | `MegaCrit.Sts2.Core.Combat/CombatState.cs:233–311,530–549`; `MegaCrit.Sts2.Core.Models/MonsterModel.cs:194–217` | Creation/attachment and active membership are separate. `CreateCreature` assigns combat and `_nextCreatureId`, initializes HP/local RNG and invokes encounter spawn callback before `AddCreature` makes the entity an active target/listener. |
| Combat → escaped creatures | `CombatState.cs:268–304` | Escape removes active membership and detaches combat by default but records the same object in `_escapedCreatures`; do not replace escape with death or erase historical identity. |
| Creature → ordered powers; power → immutable owner binding, optional applier/target, local/internal data | `MegaCrit.Sts2.Core.Entities.Creatures/Creature.cs:648–680`; `MegaCrit.Sts2.Core.Models/PowerModel.cs:264–319,564–598` | A removed power leaves the owner's active list but retains its Owner reference. History, callbacks and pending payloads can still refer to it and to applier/target creatures. |
| Monster → move-state machine, state objects/log, pending NextMove; move continuation → captured target list | `MonsterModel.cs:390–394,416–455`; `MegaCrit.Sts2.Core.MonsterMoves.MonsterMoveStateMachine/MonsterMoveStateMachine.cs:9–88` | Current/initial state, performed-first-move flag, ordered log and generated move identities are distinct from displayed intent. Capture targets and owner combat for unfinished moves. |
| Turn state → combat, pending loss room, readiness signals, cancellation and captured combat ID | `MegaCrit.Sts2.Core.Combat/CombatTurnState.cs:22–132`; `MegaCrit.Sts2.Core.Combat/CombatManager.cs:336–445,1186–1209,1265–1288` | Ending and teardown differ. Old work must not adopt the next combat's state just because a new combat is live. Simulator checkpoints preserve equivalent semantic stages and ownership, not native Tasks/locks/tokens. |

`Creature.cs:207–209,286–324` separates alive, hittable and able-to-receive-powers.
Alive is positive HP. Hittable also requires hit permission. Power applicability
requires a combat and hit permission but does not itself require positive HP.
Membership, life, targetability, effect eligibility and semantic retention must not
be collapsed into one `alive` bit or a single active array.

Encounter spawn accounting is another distinct relationship:
`MegaCrit.Sts2.Core.Models/EncounterModel.cs:135–152,368–371,397–413` records unique
canonical MonsterModel definitions in SpawnedEnemies, not every spawned creature
instance. OnCreatureSpawned canonicalizes and deduplicates enemy models. Its default
gold proportion divides escaped-creature count by that definition-list count.
Preserve those native semantics even where the XML phrase "all the monsters" sounds
like an instance list. The canonical list is not evidence that it owns every retired
spawned entity, and neither count may be reconstructed from current live enemies.

## Death, removal and deferred work

`MegaCrit.Sts2.Core.Commands/CreatureCmd.cs:512–605` captures the combat ID at kill
entry, runs before-death callbacks, resolves death/removal predicates, runs after-death
callbacks, then removes active membership and power state according to those predicates.
Power cleanup invokes AfterRemoved on the returned removed instances. Death prevention
and removal prevention are separate paths. Secondary-enemy cleanup depends on retained
teammate identities and primary/secondary classification.

If a monster dies while performing its own move, manager removal still invokes
BeforeRemovedFromRoom, resets its machine and unsubscribes tracking, but combat-list
removal is deferred. `MonsterModel.cs:435–458` completes the captured move and history
entry, clears IsPerformingMove, and only then removes the dead creature if permitted.
An object therefore may be logically removed from manager activity while still in
the combat list and referenced by executing code. Reclaiming it at HP zero or at the
first removal call changes this behavior.

Escape (`CreatureCmd.cs:610–636`) removes powers without ordinary after-removal
callbacks, performs manager removal, then records/removes the creature through
CreatureEscaped. It is not a kill and should not accidentally fire death rewards or
restoration callbacks. The original instance may remain referenced after detachment.

## Required special cases

### Test Subject and Adaptable

`MegaCrit.Sts2.Core.Models.Powers/AdaptablePower.cs:22–71` keeps itself after owner
death, prevents creature removal, sets `isReviving`, suppresses hitting and prevents
combat completion. Its death hook calls the same TestSubject's TriggerDeadState.
`MegaCrit.Sts2.Core.Models.Monsters/TestSubject.cs:166–171,254–276,332–338` increments
run extra fields, installs a mandatory dead/respawn move, increases Respawns and heals
the same creature after changing max HP. On second respawn it removes Adaptable and
PainfulStabs and adds Nemesis. No new creature identity is allocated by these revivals.
Preserve dead-state AI, respawn count, power identity/flags and zero-target windows.

### Decimillipede and Reattach

`MegaCrit.Sts2.Core.Models.Powers/ReattachPower.cs:39–109,149–160` retains the same
dead owner and power, sets reviving/dead-move state while other segments remain alive,
suppresses hits and heals the owner on reattachment. Fatal handling asks whether all
other segments are dead. Teammate scans use the retained combat graph, not only a
filtered list of live entities. `DecimillipedeSegment.cs:29–105` owns mutable starter
move index and a dead-state reference. The final all-dead animation frees visual
nodes, not a replacement semantic entity; presentation cleanup is not a retirement
proof for pending history or references.

### Tough Egg, Ovicopter and spawn ordering

`MegaCrit.Sts2.Core.Models.Monsters/ToughEgg.cs:123–174` hatches by mutating its existing
monster/creature state and HP, removing Hatch and all non-Minion powers through their
normal remove calls. It does not replace the entity with a hatchling instance.
Preserve `IsHatched`, the runtime `_hatched` flag and AfterHatchedState where read;
`_hatched` selects the public Title as well as DeathSfx, so its observable naming stage
must not be discarded as audio-only state.
presentation position/skin data is not automatically semantic state.
`Ovicopter.cs:44–95` finds unused slots against current enemy membership, creates
up to three ToughEgg entities sequentially, then applies Minion with Ovicopter as
applier. Creature creation, encounter callback, insertion/sorting, AfterAddedToRoom,
initial move setup and after-add hooks occur in distinct stages
(`CreatureCmd.cs:38–81`; `CombatManager.cs:1099–1134`). Keep spawn cursor, resulting
IDs, slots, callbacks and partially completed additions across any suspension.

### Illusion

`MegaCrit.Sts2.Core.Models.Powers/IllusionPower.cs:29–135` retains its owner after
death, preserves buffs and temporary debuffs according to the native predicate,
sets a reviving flag and creates a mandatory REVIVE_MOVE. That move captures either
the explicitly configured follow-up ID or the last state-log ID before healing the
same owner to full HP. It may be a dynamically installed state rather than a member
of the original machine. Persist its identity/behavior and follow-up; reconstructing
only the canonical AI graph cannot restore this stop correctly.

### Infested and Phrog Parasite

`MegaCrit.Sts2.Core.Models.Powers/InfestedPower.cs:20–55` creates four new Wrigglers
sequentially during the owner's nonprevented death, sets StartStunned, and assigns
fixed encounter slots. Its completion-prevention hook keeps the combat from ending
before additions occur. `MegaCrit.Sts2.Core.Models.Encounters/PhrogParasiteElite.cs:15–41`
declares slot order and spawn-name mapping. The later reveal task only changes
presentation; the newly created semantic entities already exist. Preserve any
unfinished spawn operation rather than letting last-primary-enemy death end combat
between creation steps.

### Swipe and Possess

`MegaCrit.Sts2.Core.Models.Powers/SwipePower.cs:22–79` stores the stolen combat-card
reference, player target and the card's DeckVersion link. Steal removes the permanent
deck instance; BeforeDeath re-adds that exact DeckVersion to run ownership and creates
a SpecialCardReward referencing it. New equal-by-model cards cannot replace either
instance without changing identity and reward semantics. Retain the removed deck
instance through stolen-card/power/reward links.

`PossessStrengthPower.cs:16–68` and `PossessSpeedPower.cs:16–68` retain dictionaries
keyed by exact Creature instances with signed stolen amounts. On nonprevented owner
death they iterate the dictionary and restore the negative totals to each original
target. Keep target identity and iteration order; never replace these maps with one
aggregate amount or only alive/current targets. Ordinary prevented death must not
restore them early.

## Ending, teardown and retirement conditions

`CombatManager.cs:336–359` tests both captured combat ID and IsInProgress for live
work. ID equality alone is insufficient because a completed combat keeps its turn
state until reset. `:385–410` balances nested card/potion effect depth and defers dead
player cleanup until the outermost effect finishes. `:1265–1288` records pending loss
with the originating room and processes it at a safe point.

Winning (`:1300–1362`) marks the old combat not in progress, runs revive-before-end,
AfterCombatEnd, clears combat history, invokes room completion/player cleanup, then
AfterCombatVictory. That order prohibits retiring encounter/entity state before the
remaining callbacks solely because history was cleared. Reset (`:1186–1209`) resets
active creatures/powers, invokes manager removal, removes combat memberships, drops
the turn state, clears history/depth and cancels old turn work. Native cancellation
is not proof that every arbitrary simulator continuation may be discarded sooner.

Logical instance storage is eligible for retirement only after every remaining
gameplay/declared comparison reference is discharged: owner lists, escaped entities,
attached-but-not-active creations, powers, history, rewards, pending actions/selection,
captured dispatch payloads, move targets, and room/encounter callbacks. Which concrete
C++ mechanism proves that condition remains an allocation/layout choice. Keeping
an inactive instance until a verified arena boundary is valid; deleting it early
because it is not in an active list is not. No arbitrary capacity or history
compression is justified by this run.

Native prototype cloning is different from simulator checkpoint cloning:
`PowerModel.cs:554–598` creates canonical-derived mutable instances with fresh internal
data and no owner. It is not a full runtime clone of a live power graph. Simulator
clone/restore must instead preserve internal data, exact references/aliasing,
membership/lifecycle distinctions, ID allocation and captured combat-domain identity.
Every referenced object in export/checkpoint must resolve even if removed from its
active collection. The current recorder handles detached powers and graph-discovered
creatures (`tools/oracle/adapters/RecordingSession.cs:277–306,410–432`); that mechanism
is evidence of reference closure, not automatic proof all native async locals have
been captured.

## Required regressions and limits

1. Restore at Test Subject's first and second dead stops; resume identical inputs to
   identical revival HP, AI/powers and final death; include no-hittable-enemy stops.
2. Kill one and then all Decimillipede segments with multihit/retaliation; preserve
   dead IDs and Reattach flags, fatal condition and survivor scans through revival.
3. Kill a monster during its own move; verify deferred list removal and captured
   targets/history; no new combat may inherit an old continuation's cleanup.
4. Hatch a Tough Egg with Minion plus other powers; retain same creature ID and
   remove only the native-selected powers in order. Spawn Ovicopter eggs/Infested
   Wrigglers with fixed slots and compare ID allocator/RNG/callback progression.
5. Restore Illusion's dynamically installed revive state and follow-up ID, including
   retained buff/temporary-debuff behavior and its later ordinary callbacks.
6. Swipe and recover a card with upgrades/attachments; verify exact combat/deck/reward
   identity links. Possess multiple targets with repeated signed changes, prevented
   versus actual owner death, and independent clone branches.
7. Complete/loss/reset one combat and begin another; stale captured work must not
   affect new entities or counters. Distinguish pending-loss and won-but-not-reset
   checkpoints from active combat and prove rejected stale inputs are immutable.

No fresh oracle experiment, simulator implementation or exhaustive full-content
lifetime proof was performed. This covers the named semantic families and inspected
victory dependencies. Additional supported model handlers must add their own captured
references and retirement constraints; full fields/visibility and history-consumer
audits remain separate. No existing ADR is contradicted.
