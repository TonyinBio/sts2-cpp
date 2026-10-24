# Rooms, outstanding work and retirement

This source audit separates native object relationships, required simulator semantics
and optional allocation strategies. Native citations below are relative to the pinned
local decompilation. `rooms-continuations.json` preserves hashes and inspected ranges.
No new room behavior, checkpoint implementation or oracle experiment is supplied.

## Room stack and exit sequence

`MegaCrit.Sts2.Core.Runs/RunState.cs:171–185,458–499,686–691` owns an ordered room
stack, a top/current room and a bottom/base room. Visiting a new coordinate resets
NextRoomId; pushing/popping room objects is separate from entering/exiting them.
`MegaCrit.Sts2.Core.Rooms/AbstractRoom.cs:27–34,59–80` assigns an ID on Enter; the ID
is unique only within that map location and is not stable across native save/load.
Do not treat a bare room ID or a repeated exporter label as a global instance identity.
Simulator-owned IDs can have a different documented domain, but their future
allocation metadata must be cloned/restored consistently.

`MegaCrit.Sts2.Core.Runs/RunManager.cs:1162–1245` exits rooms top first: it resolves
outstanding rewards before popping, then calls the popped room's asynchronous Exit
and emits RoomExited. The popped object remains a local reference even though it
is no longer in the stack. Entry pushes the new room before BeforeRoomEntered and
Enter, with special restoration/pre-finished cases affecting external hooks. Merely
changing CurrentRoom is neither completed teardown nor completed entry.

`RunManager.cs:1278–1304,1418–1463` supports a nested room without exiting its parent.
Resuming a parent first awaits child exit and then passes that exact exited room to
the parent's Resume. The parent event and its local variables therefore remain
owned during child combat/rewards; the child may remain a required reference after
being popped. Preserve the return destination and should-resume flag. A single
room tag cannot encode this state. Native restoration of a pre-finished combat's
parent stack (`:923–931`) is a limited reconstruction path, not arbitrary paused-state
restore or proof the native save serializes every live room.

## Event and encounter references

`MegaCrit.Sts2.Core.Rooms/EventRoom.cs:56–112` keeps a canonical definition separately
from the mutable local event supplied by EventSynchronizer. Entry registers
StateChanged handlers and starts event work. Exit awaits tracked option tasks,
then resets combat synchronizer state, unsubscribes handlers and calls EnsureCleanup.
Resume invokes existing mutable events; it does not regenerate their chosen options.

`MegaCrit.Sts2.Core.Multiplayer.Game/EventSynchronizer.cs:98–126,277–290,339–381`
retains mutable events and tracked pending option tasks. It clears/replaces these
collections when beginning another event; BeforeExitingRoom itself resets the combat
synchronizer rather than clearing the event list. `MegaCrit.Sts2.Core.Models/EventModel.cs:510–545`
tracks a once-only cleanup flag independently of current options/finished state.
A finished event can still be referenced while enclosing work completes. Persist
equivalent cleanup stages, current page/options and gameplay variables rather than
rerunning initialization, RNG generation or payment after restore.

`MegaCrit.Sts2.Core.Multiplayer.Game/EventCombatSynchronizer.cs:63–88,127–181` can
create a mutable encounter and combat state before actual combat entry for a combat
layout event, then transfer that same state into a child CombatRoom with extra rewards
and parent-event metadata. Reset removes layout creatures and clears its references.
Its namespace does not make these single-player lifecycle consequences excludable.
Exclude network transport while preserving the local semantic state and ordering.

`MegaCrit.Sts2.Core.Rooms/CombatRoom.cs:122–158` resets CombatManager on exit and has
additional pre-finished cleanup. Combat win, reward publication, room exit and manager
reset are distinct stages. The entities/history reports describe the intermediate
references; do not reclaim an encounter pool at the first lethal hit or reward prompt.

## Rewards, merchant entries and services

`MegaCrit.Sts2.Core.Rewards/RewardsSet.cs:46–77,129–191` retains player, room, ordered
rewards, generation flags and synchronizer. Offer can await completion after generation
and modification hooks. Generation must not repeat when reentering a prompt or
restoring. `MegaCrit.Sts2.Core.Multiplayer.Game/RewardsSetSynchronizer.cs:57–98,167–199,329–430`
holds ordered pending sets with completion sources and per-player IDs/completion
records. Skipping operates on the top set; completion removes the specified set,
so not every completion is simply a blind stack pop. Room exit skips remaining sets
and resolves their completion work. Empty sets may already complete before the test
selector/adapter sees them. Preserve per-operation stage and nested return semantics.

`MegaCrit.Sts2.Core.Rewards/Reward.cs:127–145` awaits OnSelect before after-taken hooks
and the SuccessfullySelected flag; parent linked rewards can then resume separately.
An attempted reward acquisition is not necessarily success. `CardReward.cs:185–313`
retains generated offers, alternatives, chosen cards and selection-loop state across
awaits; it removes the actual acquired result from the offer list and records picked
and unpicked choices afterward. The permanent card may differ from the initial offer
when acquisition hooks substitute it. Retain both relationships where still required.

`MegaCrit.Sts2.Core.Rooms/MerchantRoom.cs:52–116` owns inventories and records remaining
offers as unpicked history on exit; exit does not itself clear Inventories. Stock
clearing/replacement is distinct from acquired model ownership. `RestSiteRoom.cs:23–49`
uses synchronizer-owned mutable options; `RestSiteSynchronizer.cs:177–253` awaits a
chosen option, applies success/removal and callbacks, and finalizes rest-site state.
A selected service's source, chosen card/preview, payment and post-selection stage
must survive its prompt even if the option is no longer offered afterward.
`TreasureRoom.cs:34–86` similarly separates room exit synchronizer cleanup from
reward generation/offer work. Do not reuse the payload of a prior room just because
the next room has the same RoomType.

## Native async locals versus complete checkpoints

`tools/oracle/adapters/RunRecordingSession.cs:38–98,187–236` tracks operations, sources,
reward frames and pending selections so the reference can stop at input. It waits
for nonblocked work and removes completed operation frames at a settled boundary.
Those adapter Tasks and TaskCompletionSources are not native serialized continuations.
The adapter's captured summary fields are not an exhaustive future-state import format.

The simulator needs explicit equivalents for every value read after each supported
pause: source/target handles, candidate order, partial selection, captured amount/cost,
already executed effects, collection snapshots/cursors, branch and return stage,
child ownership, combat/room scope, and once-only cleanup status. Do not serialize CLR
Tasks or invoke native creation clones as a substitute for copying current state.
Scratch, raw pointers, C++ stack locals and borrowed observations cannot be the only
owner of such data. Capturing a source list does not erase native per-callback
eligibility checks; the dispatch audit gives the type-specific rules.

`RunManager.cs:1335–1339,1569–1615` separates WinRun/OnEnded and its later operations
from full run cleanup, which resets action/selection/combat systems, disposes
synchronizers and drops State. A published terminal state still has contractually
required fields and history; do not emulate full cleanup merely upon setting a
victory flag. Persistent platform/profile transport remains out of scope.

## Retirement conditions and implementation implications

Retire a room/encounter's mutable storage only after all of the following hold:

1. Exit/end/cleanup callbacks for that scope have completed in native order, including
   child rewards, event returns and once-only finalization.
2. No active parent, prompt, operation, captured dispatch, history/export relationship
   or required runtime model field still needs its instances or mutable values.
3. No outstanding operation can legally resume into it; invalid/stale scope work is
   rejected without touching the next scope. Preserve the guards and cancellation
   consequences rather than relying on physical memory still being present.
4. Required complete-state/observation records and future identity allocation remain
   identical. Reclaiming addresses must not reuse an identity visible to an old handle.

These are semantic conditions, not a claim to prove exact CLR garbage-collection time.
The native game may retain objects longer through registries or tasks; a C++ arena,
reference counts, deferred lists or another checked strategy is an implementation
choice provided it preserves required logical graph behavior. Copying immutable
history payloads can replace a native reference only after proving consumers and
exports cannot distinguish the substitution. No such history-compaction proof is
provided here. Do not derive capacities, reclamation ages or history limits from one run.

## Current C++ gaps and architecture clarification

The architecture's same-identity deck/combat wording is corrected to distinct native
combat copies linked by DeckVersion, with identity preserved only for movement of
the same object. This follows the normative state inventory and existing ADR0001;
it is a factual correction, not an ADR reversal or an implementation choice to merge
objects. The prior semantic audit's generic listener-membership wording is also
clarified: removed powers can still qualify through their owner in a captured dispatch.

`include/sts2/state/run.hpp:17–35` currently has one optional combat and narrow rewards,
not a general room stack/pending-operation graph. `src/kernel/run_simulator.cpp:146–153`
rejects another combat while its current optional combat exists, and
`src/kernel/run_validation.cpp:22` restricts the act. Current `history.hpp` uses boolean
actors/owners and narrow CardHandles. These are supported-subset implementations,
not proof of full-run retention. Existing narrow clone/checkpoint tests do not cover
the relationships in this audit. Build sequencing should deepen each introduced
domain with ownership, codecs, validation and regressions together.

Required scenarios include nested event combat returning through rewards, exit with
unclaimed/nested/empty rewards, cancellation after partial service work, old room IDs
reused at new coordinates, repeated encounters of the same model, final victory before
cleanup, and resumption/clone/restore at each supported suspended stage. Combine with
the model-specific inventory/entity/history cases in the companion reports. The
acceptance decision still owns the actual evidence gates.
