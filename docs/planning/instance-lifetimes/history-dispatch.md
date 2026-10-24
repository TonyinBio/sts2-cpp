# History and dispatch lifetimes

Static source evidence for the instance-lifetime audit. Sources below are relative
to `build/combat-state-audit/decompiled/`, except the explicitly named recording
adapter. `history-dispatch.json` retains source hashes, reference edges, scoped
consumer locations and regression requirements. No gameplay experiment or C++
implementation is claimed.

## Combat history owns references, not complete historical object snapshots

`MegaCrit.Sts2.Core.Combat.History/CombatHistory.cs:20–35,37–129` owns an ordered
list of entries. Recording call sites create entries after the event and before its
after hook as described by the history contract; source call sites remain authoritative
for exceptional ordering. `Add` tests `ICombatState.IsLiveCombat()`. Concrete
`MegaCrit.Sts2.Core.Combat/CombatState.cs:614–617` returns true from that method;
this is not a test of manager `IsInProgress`. Call sites can impose additional
current-combat guards. Do not replace all these conditions with one generic boolean.

`CombatHistoryEntry.cs:12–109` captures the actor reference, round, side and each
player's turn number at construction, plus a reference back to its history object.
`HappenedThisTurn` requires round, side and every captured player's current turn to
match; `HappenedLastPlayerTurn` compares the chosen player's turn to captured+1.
Current actor turn or round alone cannot reconstruct those predicates.

The entry definitions in `MegaCrit.Sts2.Core.Combat.History.Entries/` retain:

| Entry family | References and captured data that survive membership changes |
| --- | --- |
| CardPlayStarted / CardPlayFinished | The same `CardPlay` object; finished additionally captures `WasEthereal` at logging time |
| CardDrawn / Discarded / Exhausted / Generated | Exact card instance; drawn captures `FromHandDraw`; generated retains creator player |
| CardAfflicted | Card and affliction instances, even if the affliction is later cleared |
| DamageReceived | Receiver/actor, dealer, card source and `DamageResult` object |
| CreatureAttacked | Attacker/actor and ordered damage-result collection |
| BlockGained | Receiver/actor, amount, ValueProp and optional `CardPlay` |
| PowerReceived | Actual power and applier references plus amount at this application/modification |
| PotionUsed | Consumed potion and optional target references |
| OrbChanneled | Orb instance, even after it leaves the orb queue |
| EnergySpent / StarsModified / Summoned | Actor and captured amount |
| MonsterPerformedMove | Monster, move-state and target-enumerable references |

`MegaCrit.Sts2.Core.Entities.Cards/CardPlay.cs:11–74` separates `Player` from the
card's current Owner and stores exact target, resources, result pile, autoplay flag,
play index and play count. Several plays can share a card but have different play
identities. Started and finished entries must point to the same play, including
when a play pauses without yet producing a finished entry. `DamageResult.cs:12–106`
contains a receiver reference and captured damage/outcome fields; blocked damage
and some result flags are mutable. Neither a model-ID-only log nor a newly computed
result from current HP is equivalent.

History therefore keeps cards, creatures, removed powers/afflictions, consumed
potions and evoked orbs logically reachable. Removing an object from a zone or
inventory does not invalidate its history references. Conversely, a historical
reference does not make that object an active listener, legal target or inventory
member. A clone/checkpoint must preserve the graph's aliases and current referenced
object values; do not deep-copy a card separately for every log row or freeze all
current card fields into the past. Explicit snapshot fields such as `WasEthereal`
must remain snapshots and must not be recomputed from the current card.

## Scoped consumers prohibit early history truncation

The source index scans all model sources named by the victory semantic inventory,
including its fixed closure. Its directly read combat-history consumers are:

| Consumer | Native query and resulting lifetime requirement |
| --- | --- |
| `Models.Cards/Bolas.cs:39–48` | Before hand draw checks whether this exact card finished a play last player turn; retain previous-turn entries and card identity |
| `Models.Cards/GoldAxe.cs:21` | Counts all finished plays in this combat, without owner or turn filter; clearing on turn change changes damage |
| `Models.Cards/TearAsunder.cs:26` | Counts all positive-unblocked damage entries received by its owner creature; retain prior-turn hit records, not just lost-HP total |
| `Models.Cards/Normality.cs:33,40–55` | Counts this owner's started plays this turn and limits while the curse is in Hand; started and finished counts differ during a suspended play |
| `Models.Powers/JugglingPower.cs:30–33` | On application initializes its counter from owned Attack plays already started this turn, then keeps its own counter; restoring only the current counter is insufficient for a later application |
| `Models.Powers/RingingPower.cs:64–75` | Checks whether any owned play has started this turn; retaining only finished plays permits illegal extra plays |

The indexed scoped history writes include `LeadPaperweight` unchosen card-choice
descriptors and `SwipePower` stolen/returned loot counters. Other root effects use
shared commands that record run/combat history. The consumer scan is an audit aid,
not proof no reflection, delegated helper or later model can read additional data.
The full-target model set has additional consumers, including last-turn card and
damage queries, and is not certified by this scoped list.

`MegaCrit.Sts2.Core.Combat/CombatManager.cs:1299–1333` marks combat not in progress,
revives participants as needed, runs `AfterCombatEnd`, **then clears combat history**,
then calls room completion/player cleanup and `AfterCombatVictory`. End hooks can
therefore read history that victory hooks can no longer read. `:1186–1208` Reset
also clears history while dropping/cancelling its turn state. Do not clear when
the last enemy reaches zero HP, on each turn, or before `AfterCombatEnd` returns.
History clearing removes the history's own root edges, not every outstanding
continuation, dispatch, mutable-model or exporter reference to those objects.

## Dispatch candidates and callback lifetimes

`MegaCrit.Sts2.Core.Combat/CombatState.cs:411–495` constructs a candidate list when
enumeration starts: allied then enemy creatures, each creature's powers and monster
or active player's ordered inventory/orbs/pile cards and attachments, followed by
modifiers/badges/scaling. It then checks a **type-specific** `Contains` predicate
as each saved candidate is yielded. Additions after the list is built do not enter
that pass. `MegaCrit.Sts2.Core.Runs/RunState.cs:545–596` builds a different run-level
snapshot and only starts its child-combat enumeration when it reaches that phase.

Important correction to a generic "skip removed listeners" rule:

- `CombatState.cs:550–600` checks card/relic/potion removal flags and active owner,
  attachment/card eligibility, orb removal state, and monster combat association.
- For a power it checks only that its Owner still has a CombatState and, for a
  player owner, remains active for hooks. It does **not** check membership in the
  owner's present Powers collection.
- `MegaCrit.Sts2.Core.Models/PowerModel.cs:575–580` removes the power from that
  collection but preserves its Owner. Thus a removed power already in a captured
  dispatch list can still pass the next yield test and receive its callback. This
  source fact differs from recomputing the candidate list or checking active-power
  membership after each callback.

`MegaCrit.Sts2.Core.Hooks/Hook.cs:32–62` applies its ordinary combat-ending guard
once when enumeration begins, with a setup exemption. It does not abort remaining
listeners merely because an earlier listener starts combat ending. Some hooks
deliberately dispatch directly. `:278–294` enumerates normal and late
AfterCardPlayed passes separately; `:204–220` similarly separates early/ordinary
draw callbacks. Changes in the first pass can affect the next pass's fresh candidates.

Captured modifier identities and dispatch candidates are also distinct:
`Hook.cs:811–835,1897–1944` records contributing modifiers during amount calculation,
then the after-modification callback path freshly enumerates eligible listeners and
intersects with those modifier identities. Replaying a stored modifier list directly
without the fresh listener pass can call the wrong removed/replaced model.

At a pause inside a callback, complete state must retain the event payload, current
model/source, already captured ordered candidates, cursor and nested operation. A
model can be absent from active membership yet needed by the current callback or a
remaining saved candidate. Native iterators/async frames are evidence for these
semantic values, not a requirement to serialize .NET enumerators or Tasks.

## Run history is a different ownership graph

`MegaCrit.Sts2.Core.Runs/RunState.cs:31,158–168,511–539` retains nested act/floor
history with ordered room lists and per-player stats. Multiple nested rooms can
belong to one map point; flattening them into one room tag loses data and scoring
order. `Runs.History/MapPointRoomHistoryEntry.cs:17–42` stores room/encounter/event
model IDs, resolved monster model IDs and turns, rather than live room or monster
instances. `Runs/PlayerMapPointHistoryEntry.cs:22–178` stores numeric room stats,
ordered choices, model IDs and SerializableCard descriptors.

`Runs.History/CardChoiceHistoryEntry.cs:19–23` and
`CardTransformationHistoryEntry.cs:19–23` call `ToSerializable` when the choice or
transformation is recorded. Those records do not retain a direct `CardModel`
pointer. Run history can preserve what was offered, removed or transformed without
keeping that original object alive solely because of these records. Do not replace
these captured descriptors with pointers to later-mutated cards. Native serializers
and `RunManager.cs:688–705` preserve the accumulated history; native shallow list
copying does not establish independent simulator clone semantics.

Earlier floor data still affects gameplay and final outputs:
`RunManager.cs:660–668` forbids a random shop based on the previous map-point room
list; `Runs/ScoreUtility.cs:29–52,59–128` uses all act/floor counts, room ordering,
elite/boss boundaries and accumulated gained gold for final score. `TotalFloor`
derives from the history list lengths. Current floor/HP summaries cannot replace
this data. Profile persistence and run-history screen widgets are outside scope,
but the in-run data and matching final score inputs are not.

## Export identity is an adapter convention

`tools/oracle/adapters/RecordingSession.cs:215–310,312–359` assigns object-reference
paths while traversing the graph. Card/creature labels and `/combat/powers/N`,
`/combat/card_plays/N`, `/combat/attacks/N` are synthetic comparison identities, not
native allocation slots or proof an object belongs to an active zone. It can add
retained inventory models and removed powers found only through histories/fields.
Started-without-finished play identity is reconstructed from actual reference
equality between history entries. Clone/export must preserve the corresponding
semantic aliases, not native addresses or .NET object-layout identity.

The exporter deliberately omits MonsterPerformedMoveEntry, SummonedEntry and
PotionUsedEntry, citing no gameplay consumers in the audited build (`:315–317`).
A direct type-reference search finds only their native declarations and logging
methods. This bounded exclusion is not proof that their native objects never exist,
nor permission to drop a consumed potion needed by a continuation or model field.
Preserve declared comparison limits and audit any new consumer before expanding
or compressing history. The exporter cannot define complete native lifetimes.

## Retirement rules and regression obligations

Storage may be retired only after the object is absent from active ownership **and**
all history, in-flight dispatch, payload, continuation, model-field and pending-choice
references have been resolved or replaced by an independently equivalent owned
representation. Clearing a log or changing a pile alone is insufficient. This is a
semantic reachability rule, not a mandate for reference counting or a specific arena.

Required regressions include:

- Distinct repeated CardPlay identities share one card; a suspended started play
  has no finished counterpart, and restoring it does not invent or repeat an entry.
- Play Bolas, pass into the next turn and verify that the same instance returns;
  duplicate model IDs must not make another Bolas satisfy the identity query.
- Apply Juggling after earlier attacks; evaluate Normality/Ringing while a play is
  started but not finished; Gold Axe/Tear Asunder include earlier-turn history.
- Remove a power after dispatch captured it but before its callback; compare native
  pending-callback behavior. Repeat with removed relic/potion/card to exercise their
  different removal predicates, and with a newly added listener before the late pass.
- Kill the last enemy during an already-started guarded dispatch, then pause/restore;
  remaining callbacks preserve the entry guard and captured list/cursor.
- Verify AfterCombatEnd can read the old log while AfterCombatVictory sees it cleared;
  ensure a subsequent combat has neither leaked entries nor invalid old aliases.
- Mutate a card/power after logging; preserve live reference semantics alongside
  WasEthereal/amount/turn snapshots. Clone one branch without changing another.
- Change rooms/acts and finalize score with nested room history, stolen loot returned
  and serialized removed/transformed cards; earlier descriptors remain unchanged.

No log compression or early-retirement optimization is approved. Proving an equivalent
summary would require a complete consumer/continuation/comparison-surface analysis
and separate evidence; arbitrary capacities from this victory run are unsupported.
