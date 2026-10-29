# Complete-state requirements inventory, revision 2

This is the storage checklist for [the matching contract](matching-contract.md).
`Visible` means project only through [the visibility contract](decision-contract.md);
it does not permit serializing an entire record into a public observation.
`Hidden` still belongs in clones, checkpoints and canonical comparisons. `Mixed` means
that only an explicit projection is public. Native save coverage is a source of fields,
not proof that the run can resume at an arbitrary choice.


The accepted [inline-state decision](../../adr/0003-inline-state-with-correct-overflow.md)
governs storage and retention: complete common state must support memcpy cloning,
with separately owned heap overflow and no arena. Completed encounter archives and
export-only statistics are not required state. Known gameplay histories, generation
progress, player observations and pending work remain required. Initial capacities
are estimates; tune them after parity. This is a target, not implemented coverage.

## Owners and required data

| Owner | Future-affecting fields and relationships | Visibility | Evidence / native coverage |
| --- | --- | --- | --- |
| State root | Game/content/schema identity, config, validity, boundary counter, phase, next identity counters | Mixed | Architecture contract; simulator-owned |
| Run configuration | Character, ascension, ordered modifiers/parameters, seed string/numeric seed, fixed unlock set, game mode, explicit clock policy | Mixed | `SerializableRun`, `RunState`, `Player` |
| Run progression | Ordered acts, current act index, act/total floor, current/visited coordinates, room ID allocator, run location | Mixed | `RunState`, native acts/map/history |
| Map | Nodes, coordinates, ordered edges, point type, assigned room/encounter/event, reveal/visit state, generated future act data | Mixed | `ActMap`, act models, native map data; preserve hidden assignments |
| Generation history | Seen events, encounter lists/cursors, shared/player relic grab bags, card/potion/unknown-room odds, exclusions, per-act counters | Hidden unless displayed | `RunState`, act saved properties, native odds/grab bags |
| Room stack | Ordered active/parent rooms, IDs, map location, resolved type, entry/exit/completion stages, return destination, pre-finished room | Mixed | `RunState._currentRooms`, `NextRoomId`, `SerializableRun.PreFinishedRoom`; native save does not represent every live stack |
| Player | Current/max HP, gold, base resources/capacities, character-specific persistent state, deck, relic order, potion slots, persistent effects | Mixed | `SerializablePlayer`, runtime `Player`; combat audit for additions |
| All instances | Definition and stable instance IDs, owner, lifecycle, creation order, references to shared instances, persistent/combat identity domains | Mixed | `RunState._allCards` includes floating reward/preview cards; combat audit |
| Cards | Upgrade level, mutable costs/damage/block/keywords, enchantments, afflictions, counters, temporary changes, clone/deck-version links | Mixed | Native saved cards plus detailed card/cost audit |
| Relics/potions | Ordered acquisition/slots, saved properties, mutable counters, use/activation state, attached targets and local content state | Mixed | Native properties plus model-member audit |
| Combat root | Round/side/phase, active participant, encounter configuration/state, turn counters, victory/defeat stage, cleanup flags | Mixed | `CombatState`, `CombatManager`; native run save is insufficient |
| Combat player | Energy and other resources, turn limits/counters, orb slots/order, summons and character mechanics | Mixed | Runtime player combat state and combat audit |
| Entities | Identity/owner/team, alive/dead/removing state, HP/max HP/block, powers/order/stacks/appliers, summons, source relationships | Mixed | Creature/power sections of combat audit |
| Card zones | Ordered hand, draw, discard, exhaust, limbo/play/selection memberships; floating referenced cards | Mixed | Combat audit; identity is distinct from membership |
| Enemy AI | AI graph/state, move history, queued/revealed intent, captured targets, per-monster variables and encounter counters | Mixed | Monster AI and encounter model audit |
| Combat summaries and required references | Sufficient play/draw/hit counters, per-card last-finished turn, suspended operands and any richer state required by hooks | Mixed | History sections of combat audit; diagnostic ordered history lives in the optional JSON exporter, outside gameplay clones |
| Rewards | Ordered reward entries/offers, generated card/potion/relic instances, alternatives, claim/skip status, pending replacement and exit state | Mixed | `SerializableReward` family plus runtime reward objects |
| Merchant | Ordered inventories and item instances, generated prices, discounts, sold state, removal cost/count, restock and pending service transaction | Mixed | `MerchantRoom.Inventories`, `MerchantInventory`; runtime data exceeds room tag |
| Events/ancients | Mutable event instance, branch/page, eligible/current options, local variables, captured choices, payment/effect stages, nested room return | Mixed | Event models, `EventRoom`, native event saved properties; active locals need explicit continuations |
| Rest/services | Available actions, used/disabled state, selected objects, upgrade/transform/remove/enchant previews, cancellation/payment stage | Mixed | `RestSiteRoom`, rest-site options and card selectors |
| Treasure | Generated/offered relics, selection/claim state, replacement/skip/exit continuation | Mixed | `TreasureRoom`, relic acquisition hooks |
| Lifecycle | Victory/loss kind, finalization stage, gameplay/observation-required score inputs and history, explicit elapsed-time values/reload counters where relevant | Mixed | Native run/history and `RunManager`; no persistent profile |
| RNG | Every live stream/local generator, seed/state words/counter, aliases/ownership and generation cursors | Hidden | `RunRngSet`, `PlayerRngSet`, `Rng`, `MegaRandom` |
| Decision | Active prompt/owner, option identities/order, constraints, partial ordered selection, confirm/cancel semantics, resume destination | Mixed | Player-choice/card-selector paths; not recovered from a room tag |
| Execution | Program/handler IDs, instruction/stage, locals, captured operands/targets/sources, pending operations, nested stack | Hidden | Simulator continuation representation; real-game async locals require semantic mapping |
| Trigger dispatch | Active event payload, subscription owners/order, captured source list, cursor, nested dispatch and addition/removal behavior | Hidden | Runtime hooks and combat audit; rebuild only demonstrably derived indices |
| Storage metadata | Slot generations/retired slots, free-slot allocation order, live regions, instance-domain ownership and next creation sequence | Hidden | Simulator-owned; affects future identity allocation |

This table specifies semantic families, not the game's memory layout. Preserve all
relevant model-specific fields by incorporating the existing
[combat runtime audit](../combat-runtime-audit.md) and
[model-member inventory](../combat-model-field-audit.md). The latter enumerates actual
storage names and classifications in cards, powers, relics, monsters, potions, orbs,
enchantments, afflictions, encounters, modifiers and characters. Its `additional_gameplay`
and relevant native-property rows refine these families; excluded presentation fields
do not become requirements merely because they are writable.

Native field spellings/types are defined by the existing
[native v20 schema](../../../data/schemas/serializable-run-v20.schema.json). In particular preserve
acts, modifiers, current act index, events seen, pre-finished room, odds, shared relic bag,
players, RNG, visited coordinates, ascension, gameplay-relevant extra fields and mode.
Retain map-point history only where future gameplay or player observations require it;
do not retain completed combat graphs or export-only room statistics.
Do not import platform IDs, map drawings or save timestamps into gameplay simply because
the native file stores them. Start/run/win times and reload counts require classification
against scoring/history consumers; use explicit inputs if observable behavior needs them.

## RNG inventory

The pinned enum inventories are:

| Owner | Stream identifiers |
| --- | --- |
| Run | `UpFront`, `Shuffle`, `UnknownMapPoint`, `CombatCardGeneration`, `CombatPotionGeneration`, `CombatCardSelection`, `CombatEnergyCosts`, `CombatTargets`, `MonsterAi`, `Niche`, `CombatOrbs`, `TreasureRoomRelics` |
| Player | `Rewards`, `Shops`, `Transformations` |

`TreasureRoomRelics` is declared even though its documented use is multiplayer tie
breaking. Retain compatible state where present; do not invent single-player draws.
`RunRngSet.CombatOrbGeneration` accesses enum stream `CombatOrbs`, not a new stream.
`MonsterModel.Rng` is not the source for normal monster moves: those use `MonsterAi`.
The existing audit identifies ToughEgg's monster-local RNG use as cosmetic.

The list of named banks is not the entire randomness contract. `Rng(Player, ModelId,
mixin)` creates content-local generators from run seed, player slot, model entry hash
and mixin. Preserve any such live generator needed after a pause. Inventory local
map/event generators and their seed/call ordering before implementing each system.
`Rng.Chaotic` is documented for non-gameplay randomness; a gameplay consumer would be
an explicit gap requiring analysis, not permission to introduce nondeterminism.
Store the generator's complete `MegaRandom` state and wrapper counter, not just a seed
or the number of API calls. Implement bounded draws, shuffle, deterministic string hash,
seed conversion and draws skipped/consumed on branches exactly in step 2.

## Continuation and ownership requirements

An accepted input may pause inside payment, a trigger, selection, replacement, card
play, room entry or room exit. Every value read after resumption belongs to complete
state: selected and remaining candidates, source/target snapshots, amounts already paid,
iteration positions, outstanding child operation, branch and cancellation destination.
Scratch and C++ stack locals cannot be the only copy. Do not serialize real-game Tasks
or pretend that the game already owns the proposed instruction-frame representation.

A permanent deck card and its combat clone may be separate objects linked by
`DeckVersion`; movement must not merge their identities. Floating offered cards and
history-referenced destroyed objects need valid relationships. Ordered collections
cannot use swap removal. Sets may use canonical ordering only if their iteration order
cannot affect gameplay. Native dictionary/hash-set iteration needs consumer analysis.

Checkpoints and clones include hidden state, pending prompts, nested room stack,
continuations, RNG and identity-allocation metadata. Restore validates references,
zone membership, handles, ownership, prompt eligibility, frame destinations and
compatibility before publishing a valid instance. Capacity, addresses and padding are
not canonical state; free-slot order and generations are. Reconstructed caches must
preserve behavior without draws or changed trigger order.

## State retention and extensibility

Before choosing capacities, classify fields by future gameplay, player observations,
known audited future mechanics, or export-only use. Keep histories required by known
mechanics even when their consumers are not implemented. Adding a newly required
history must compose with clone, reset, validation, overflow and checkpoint operations.
Do not retain fields solely for diagnostic historical exports. Remove completed
encounter records entirely, without an external archive; keep explicit combat-generation
metadata to preserve stale-handle safety independently of archive length. Encounter
queues/cursors, RNG, exclusions, gameplay history and meaningful visited paths remain.
The exact elite-cycle rule still needs source/oracle verification.

## Exclusions and remaining audits

Exclude graphics, textures, animation progress, audio, widget references, formatting
buffers, static definition text, network transport, multiplayer state and persistent
profile progression. Preserve a gameplay consequence of any otherwise excluded system.
A cached preview can be excluded only if it can be recomputed from authoritative state
without randomness or side effects.

The source-backed combat audits are detailed; the full-run families above still need
member-by-member source audits for map generation, merchant restocking, rewards,
event locals, services, lifecycle and every local RNG consumer. Exact trigger capture
and mutation ordering also remains mechanic-specific. These are named evidence gaps,
not omitted state or assertions that native saving captures them. The next implementation
must reject a configuration whose required fields/continuations are not represented;
it must not claim arbitrary checkpoint parity from this checklist alone.
