# Shared semantic requirements

Source citations below refer to the local ILSpy decompilation under
`build/combat-state-audit/decompiled/`; source hashes and build identity are retained
in `source-index.json`. These are implementation requirements inferred from inspected
native control flow, not implemented C++ behavior or exhaustive runtime verification.

## Operation order and listener dispatch

`MegaCrit.Sts2.Core.Combat/CombatState.cs:411–497` snapshots listener candidates in
creature order: allies then enemies, each creature's powers before its monster model
or active player's unmelted relics, occupied potion slots, orbs and pile-ordered cards
with affliction then enchantment. Modifiers/badges/scaling follow. It checks membership
as it yields; a removed candidate is not automatically still dispatched. Run listeners
in `MegaCrit.Sts2.Core.Runs/RunState.cs:545–596` have a different order: active players'
permanent deck cards and enchantments, then out-of-combat inventories and modifiers
when no child combat exists, otherwise the child combat listeners after run subscribers.
Do not deduplicate deck/combat-copy identities or globally sort listeners by model ID.

`MegaCrit.Sts2.Core.Hooks/Hook.cs:32–62` guards most combat dispatches when already
ending, with a setup exemption. The ending check occurs at dispatch entry, not after
each listener. Specific death/card-play hooks dispatch directly. `:278–292` performs
ordinary AfterCardPlayed listeners, then a separately enumerated late pass. These
differences require operation-specific execution frames and dispatch cursors; one
universal trigger loop cannot infer all ordering from hook names.

`Hook.cs:2520–2577` applies damage additive, multiplicative and cap passes in that
order, enumerating listeners for each pass. Preserve decimal/native rounding stages,
ValueProp distinctions, source and target identities, intermediate state and modifier
callbacks; an aggregate cached damage percentage is insufficient. Per-model records
identify the corresponding multiplier, cap and flat-change conditions.

## Card and potion execution

`MegaCrit.Sts2.Core.Models/CardModel.cs:1807–1843,1858–2035` separates resource spending
and captured X cost from the play wrapper. The wrapper moves the card into Play,
captures result location and lets hooks modify it, generates play count and runs each
play with before/history/effect/enchantment/affliction/after ordering and owner-death
guards. A per-play combat-ending guard can suppress later repeats. Result movement,
exhaust/removal, empty-hand detection and cost cleanup follow. Retain original owner,
result destination, resources, current play index and unfinished nested work; do not
recompute captured decisions from a later state. Autoplay and manual play differ.

`MegaCrit.Sts2.Core.Models/PotionModel.cs:236–353` removes a potion before its effect
and before BeforePotionUsed, then runs a branching effect and finally balances effect
depth. Surviving-owner history, after-use callbacks and empty-hand processing follow.
Potion target validation (`:255–295`) differs from card validation, especially Self
and AnyPlayer; shared targeting must preserve those differences. Sparse slot indices
and consumed source lifetime remain required. The separate lifetime audit owns the
complete ownership/retention design.

`MegaCrit.Sts2.Core.Combat/CombatManager.cs:385–445,1390–1440` tracks nested card/potion
effect depth, safe cleanup, pending loss, live primary enemies and hooks preventing
combat end. Enemy turns snapshot the enemy list and recheck membership before acting.
`MegaCrit.Sts2.Core.Commands/CreatureCmd.cs:530–603` separates before-death callbacks,
death prevention, removal prevention, post-death callbacks, power cleanup and player
deactivation. Zero HP alone is neither removal nor victory. Adaptable/TestSubject,
Reattach/Decimillipede, auto-play lethal effects and secondary enemies need explicit
regressions at those boundaries.

## Mutations and selection

`MegaCrit.Sts2.Core.Commands/PowerCmd.cs:105–166,219–255` distinguishes new application
from modification of an existing power. It preserves pre-change callbacks, given and
received modifiers, application, history, skip-next-duration handling and after hooks;
existing powers are removed when their amount rules require it. Amount sign/type,
zero-result application, applier absence, removed targets and combat-ending guards
are observable edge cases, not generic increment/decrement arithmetic.

`MegaCrit.Sts2.Core.Commands/CardPileCmd.cs:267–312,373–550,1006–1067,1073–1133`
separates generation, adding/moving, adding-to-deck interception, pile-change hooks,
draw history and shuffle. Draw applies native ceiling for positive decimal requests,
checks hand capacity and ending before each draw, shuffles when needed, then runs
draw hooks before recomputing remaining capacity. Random insertion and shuffle use
the native Shuffle stream; random selection has its own stream. Generated card
creation is not equivalent to moving a deck card or cloning a recorded descriptor.

`MegaCrit.Sts2.Core.CardSelection/CardSelectorPrefs.cs:21–86` preserves min/max,
confirmation, cancellation, ordering and prompt semantics. `MegaCrit.Sts2.Core.Commands/CardSelectCmd.cs:252–310,388–441,537–552,754–781,817–850`
has method-specific empty and auto-selection rules. The injected selector interface
can omit UI flags: its numeric selection bounds alone cannot certify native prompt
semantics. Keep the visibility/prompt audit as an independent acceptance prerequisite.

`MegaCrit.Sts2.Core.Commands/RelicCmd.cs:35–81` inserts the relic and removes nonstackable
instances from grab bags before AfterObtained; replacement preserves inventory position
and runs removal before acquisition. `MegaCrit.Sts2.Core.Entities.Merchant/MerchantEntry.cs:69–95`
checks affordability, delegates the actual operation, clears/restocks only on success
and then dispatches AfterItemPurchased. `MerchantPotionEntry.cs:81–103` procures before
charging. Full inventory means failed procurement without gold/stock loss, even though
the recording adapter accepts the attempt and advances its action clock. Avoid
universal payment-first or rollback assumptions across shops/events.

## Native pools, streams and conditional support

`MegaCrit.Sts2.Core.Factories/CardFactory.cs:25–37,119–162` filters distinct combat
generation by player count, CanBeGeneratedInCombat and rarity; it excludes Basic,
Ancient and Event cards, distincts, randomly selects and creates mutable combat cards.
`CardPoolModel.cs:101–114` applies unlock and single-player constraints. Do not replace
the eligible pool with implemented models or recorded candidates. The distinct draw
algorithm and source ordering, including RNG draws, are part of the requirement.

`CardFactory.cs:170–211` uses a separate transformation policy: pool choice depends
on the original type/rarity, excludes the original identity and may apply combat
generation eligibility. Reward and merchant creation (`:40–109,214–320`) have their
own rarity, duplicate exclusion, upgrade and hook policies and RNG sources. A single
"random card" implementation cannot be substituted for all these operations.

`MegaCrit.Sts2.Core.Factories/PotionFactory.cs:45–101` combines unlocked character and
shared pools; combat generation additionally excludes noncombat-generatable potions.
Each generated potion rolls rarity using NextFloat, selects within that rarity and
removes it before another draw. Alchemical Coffer explicitly uses the CombatPotionGeneration
stream despite acquisition being out of combat. Preserve those stream choices.

`MegaCrit.Sts2.Core.Models/MonsterModel.cs:408–418` rolls moves through RunRng.MonsterAi.
`MegaCrit.Sts2.Core.MonsterMoves.MonsterMoveStateMachine/RandomBranchState.cs:115–165`
uses dynamic branch weights, ordered cumulative selection, repeat limits, state log
and cooldown exclusions. Preserve state-machine history and registration order, not
only the last displayed intent or a recorded move cycle.

Full definitions, eligibility and generation remain mandatory even when a broad-pool
output's effect is outside the milestone. That is an explicit partial capability,
not a successful implementation of the output. Report unsupported execution with
the affected model/operation; never silently substitute, reroll, remove it from offers
or claim parity for the resulting branch. Acceptance must declare what operations
on such an output are supported and test rejection without false successful state.

## Cross-system regression families

Additional fixed capabilities exposed by the audit:

- `MegaCrit.Sts2.Core.Map/SpoilsActMap.cs:87–101,118–225,263–296,338–521`
  generates its own seven-column hourglass topology using a seed-derived `spoils_map`
  RNG. It narrows into a centered treasure, respects adjacency/crossover and maximum
  parent/child constraints, assigns constrained first/final rows, queues special room
  types in native order, performs up to three shuffled assignment passes, fills the
  rest with monsters, and invokes shared pruning/repair. Preserve complete topology,
  enumeration and RNG results across seeds; replaying only visited nodes is insufficient.
  Spoils Map's quest attachment, saved act index, remembered treasure coordinate,
  completion payment and removal lifecycle are additional requirements in `cards.md`.
- `MegaCrit.Sts2.Core.Models/EnchantmentModel.cs:130–189,273–295,324–364` binds an
  attachment to one card, rejects incompatible existing enchantments, stores amount
  and independent dynamic values, and reapplies modification logic after restoration
  or upgrade/downgrade recalculation. Mutable cloning must preserve already-applied
  values rather than apply the enchantment a second time. `AfflictionModel.cs:92–127,179–206`
  has separate amount/stackability/eligibility rules. Do not multiply all attachment
  variables by Amount universally; use each model's actual amount reads and formulas.
- `MegaCrit.Sts2.Core.Entities.Cards/CardEnergyCost.cs:94–161,331–351` distinguishes
  local and global modifiers, captures X resources and cleans up modifiers at their
  own turn/play lifetime. Repeat/autoplay with a captured cost is not another manual
  payment; free-this-turn effects must not become permanent cost changes.
- Archaic Tooth's authored transformation outputs include Suppress, Protector,
  Meteor Shower and Quadcast alongside the already observed Break. These are fixed
  conditional outputs, unlike an open-ended random pool. Its starter input names
  are identity predicates, not calls to those starter cards' effects. Protector
  requires pet/dealer identity and live pet max-HP access; Quadcast requires ordered
  orb queue/evoke dispatch. `MegaCrit.Sts2.Core.Commands/OrbCmd.cs:94–151` checks
  ending/empty queue, optionally dequeues before evoke, then calls after-evoke hooks
  and removal cleanup. This does not by itself instantiate every pet or orb model.
  Concrete separately unsupported pet/orb effects must remain explicitly unsupported.

- Reorder/acquire/remove listeners, then compare native listener effects, including
  a listener that kills the last enemy midway through dispatch and the late pass.
- Repeated/autoplay card with enchantment and affliction; owner or target dies before
  later effects; verify play/history/result-location/cost cleanup and captured values.
- Suspend inside a consumed-potion or nested card selection, restore/clone, branch,
  and resume through the remaining callbacks without repeating payment or RNG draws.
- Draw into a full hand, reshuffle, trigger nested generation/draw and exhaust; compare
  exact piles, history, random stream state and ending behavior.
- New/stacked/zero/prevented powers, duration tick skips and end-combat modifications;
  compare amounts and callback histories, not just final HP.
- Failed shop potion procurement, removal cancellation, successful acquisition and
  inventory replacement; compare stock, gold, instance identity, hooks and clock.
- Preserve unimplemented broad-pool candidates and seed/RNG vectors; choose supported
  and unsupported outcomes deliberately and verify the declared capability boundary.

These scenarios complement per-root cases. They do not substitute for the later parity
acceptance decision, exhaustive interaction coverage, public visibility evidence or
complete-state lifetime verification.
