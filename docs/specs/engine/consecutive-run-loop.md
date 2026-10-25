# Consecutive supported combat ownership

Issue [#17](https://github.com/TonyinBio/sts2-cpp/issues/17) extends the
[public bounded run loop](public-run-loop.md) with run-owned completed encounter
graphs and explicit permanent-card links. Combat, reward and map decisions continue
through the existing typed public API. This slice does not establish natural
second-encounter or victory parity.

## Ownership and identities

`RunState::encounters` owns each completed combat graph, resolved rewards, captured
permanent-deck descriptors and discarded-potion history when another combat starts.
References within a retained combat continue to address that graph. The next combat
receives a distinct card-handle generation; rebasing an old card action onto a fresh
boundary cannot make it address a new card with the same slot index.

Combat cards carry an explicit permanent-card instance link. Native export resolves
that link independently of permanent-deck order. Combat-copy identities and permanent
identities remain separate. Native `Player.PopulateCombatState` creates combat copies
and assigns their `DeckVersion`; see the source anchors in the
[evidence ledger](../../../data/evidence/consecutive-run-loop.json).

Copies of `RunState` own independent mutable graphs, including retained encounters.
Binary checkpoints preserve those graphs and links. The combat format is version 3;
the run format is version 2. Restore validates a temporary state, including retained
references, domain generations and archived boundary/clock ordering, before replacing
its destination. Incompatible versions and malformed data are rejected atomically.
This slice retains complete prior pools; it implements no slot retirement or recycling.

## Declared scenario entry and natural travel

`enter_combat_scenario` is a diagnostic scenario operation, separate from player
actions. After a completed combat and resolved rewards, it enters another supported
Shrinker room through the same run-owned lifecycle. It neither edits generated
encounter queues nor advances the map. It consumes no player action-clock increment.
Native room entry advances the normal-room visit counter; the C++ operation preserves
that behavior and rejects unsupported content or exhausted supported limits before
mutation.

The native operation is `RunManager.EnterRoom`: it exits the current room and enters
the replacement. This differs from `EnterRoomWithoutExitingCurrentRoom`, which pushes
a nested room and appends its history. Live comparison establishes that replacement
entry retains the current floor/history room, accumulates spawn IDs and player
statistics, preserves the previous completed turn count during an unfinished fight,
then replaces that count when the new fight ends. Internal completed-combat graphs
remain distinct from these native captured history values.

Natural travel no longer rejects a room solely because a prior combat graph exists.
Its native generated queue and effect-capability checks remain in force. The native
Overgrowth pool contains Shrinker once, and other encounters are still unsupported
by this slice; no duplicate Shrinker is inserted to create a natural route.

Potion of Binding can remain in inventory during the scenario combat, with
source-backed privileged metadata. This does not implement potion use. Other active
potion effects remain explicitly unsupported. Generation pools and unchosen offers
are unchanged.

## Verification

The declared scenario starts from seeded setup, naturally enters the first Shrinker,
claims gold and a potion, resolves rewards, then explicitly enters the second
Shrinker. Its operations and complete expected states are stored separately from
historical `actions-v1` player recordings under
`tests/parity/fixtures/reference/consecutive-shrinker-scenarios`.

`tools/oracle/check_consecutive_combats.py` captures the pinned native game,
independently replays the operations in a fresh native process, then drives the C++
scenario transport through public observations and actions. The existing exact
comparator checks every corresponding logical boundary. Expected states are never
used as transition inputs. Each C++ operation also resumes an independent clone and
binary restore and compares their resulting complete states; observation, action
enumeration and description queries are checked for mutation.

The registered `kernel_consecutive_scenarios` check replays the preserved fixture
without a local game installation. Unit regressions cover independent alternative
futures, stale handles, permanent-deck reordering, malformed retained state and pure
rejection of unsupported or premature scenario entry. The ledger records executed
results and fixture hashes separately from source findings and broader pending
interaction coverage. Existing historical fixture bytes remain unchanged.

This evidence covers consecutive supported combat ownership. General room returns,
partial selections, arbitrary inventory hooks, broader encounters, natural victory,
warm throughput and exact-English certification remain assigned to later tickets.
