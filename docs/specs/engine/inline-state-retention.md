# Complete-state field classification for issue #55

The classification precedes capacity selection. Its implementation working notes
were recorded before the state migration. This inventory applies to existing fields
and supported mechanics; it does not infer that an unimplemented history consumer
makes its history removable. The [combat runtime audit](../combat-runtime-audit.md#history-that-affects-gameplay)
identifies Normality, Feral, DeathMarch, BansheesCry and other history readers.
Their required gameplay information remains authoritative, using equivalent compact
representations where established rather than retaining diagnostic event logs.

## Retained state

| Existing fields | Reason |
| --- | --- |
| Combat `rules`, `boundary`, `phase` | Content identity, rejection/stale-action protection, continuation phase |
| Combat/run `seed`, `numeric_seed`, clocks and every RNG bank | Seed display, generation, deterministic future draws and action clocks |
| `player`, `enemy`, `energy`, `turn`, `move`, `burning_blood` | Gameplay, visible health/resources/intent and lifecycle hooks |
| Combat pool, zone order, `next_creation_order`, slot generations and free order | Stable identities, zone membership, floating referenced sources and future identity allocation |
| `execution` | Self-contained suspended source, play identity, instruction, paid energy and selected target; no completed-play table |
| `counters` | Combat-wide started/finished play totals and finished-ethereal count; per-player-turn starts, Attack starts, zero-energy Attack starts and non-hand draws |
| Creature `positive_unblocked_hits` | Per-receiver count, distinct from HP lost |
| Card `last_finished_player_turn` | Exact card identity's last finished player turn |
| Creature fixed `powers`, `power_order`, `power_count`; combat `next_power_instance` | Slots indexed by implemented `PowerId`, runtime amount, turn-start amount, duration flags, application order and non-reused identity; no separate Vulnerable/Shrink caches or expired instances |
| Run character, ascension, HP/max HP, gold, potion/orb slots | Gameplay and player observations |
| Persistent deck, instance/upgrade/floor metadata, relic order and potion slots | Inventory and stable permanent/combat relationships |
| Both relic bags, tier order, all act queues, exclusions represented by generated queues, per-act cursors, rarity/potion odds | Hidden generation progress; preserve the original algorithms and RNG positions |
| Run setup, active combat, direct-combat mode, phase, boundary, clocks | Current execution and legal public boundary behavior |
| Active rewards, offers, claim/skip/selection/resolution flags | Pending branches and continuation; do not regenerate offers on copy/restore |
| Act index, maps, current/visited coordinates | Floors, visible path and future movement/generation |
| Event RNG, Neow offers and selected option | Pending/visible choice and subsequent state |
| New explicit run `combat_generation` and pool `highest_generation` | Cross-combat stale-handle rejection without encounter archives, including recycled slot generations |

## Current-combat representation and diagnostic ownership

The summaries above cover the audited candidates for the supported single-player
turn model. Only Strike, Defend, Bash and Survivor are currently executable; their
types do not mutate, none is ethereal, and all current draws are hand draws.
Normality, Ringing, Juggling, Feral, DeathMarch, GoldAxe, TearAsunder, BansheesCry
and Bolas remain unsupported. Their future implementation must verify each query
against the source, including live-property semantics, before relying on these
summaries. This is not a complete-game sufficiency proof. Multiplayer/extra-turn
identities, type changes, ordered-result consumers and detached model references
may require additional state. A round number alone is not a general turn identity.

The optional synchronous `CombatTrace` callback is supplied through execution
scratch (and explicit initialization/scenario arguments), never stored in cloned
state. It does not control gameplay. With no exporter, diagnostic events are not
retained or allocated. `json_io/` paths host the exporter implementation, although
the C++ namespace remains `sts2::engine`. Exporter-owned vectors retain event order,
completed plays and expired power values/identities for the existing comparison
schema. Turn-start power snapshots and final removal updates reach the exporter
before reclamation. Supported powers have no captured dispatch/model references;
future powers with those references cannot use this reclamation rule unchanged.

The exporter attaches when recording starts and receives every transition of one
linear execution. It is not copied with engine branches; late recording of arbitrary
saved states and recording cloned branches are unsupported. Missing initialization
or mismatched combat boundary/generation causes an explicit export error. Complete
engine checkpoints restore gameplay independently of exporter history; combat
checkpoint revision 6 and run checkpoint revision 4 reject older revisions. Their
encoding remains binary. Comparison schemas, action recordings and fixtures are
unchanged. Exact history comparisons remain enabled.

[ADR 0005](../../adr/0005-use-fixed-power-type-slots.md) selects fixed power-type
slots over the former combat-wide sequence. Only implemented power types reserve
slots; Vulnerable and Shrink have no multiple instances, so the current layout
reserves no additional-instance storage. Application order uses compact type IDs.
The ADR records the requested later review of removing that order tracking while
preserving native behavior.

## Removed state and exact export paths

These removals concern privileged native diagnostic exports, not public observations
in `src/observation`. Live HP, gold, inventory, turn, exported active-combat history, prompts and
reward operations remain exact. No wildcard comparison ignores or fabricated null
values replace the removed fields.

| Removed state / export path | Individual rationale |
| --- | --- |
| `RunState.encounters`, `EncounterRecord` and `max_retained_encounters` | Completed combat graphs have no continuation or public-observation reader. Active continuations remain in active combat. There is no replacement archive. |
| `/map_point_history/*/*/rooms/*/monster_ids` | Completed-floor spawn summaries; active creatures and visible intent remain in active combat. |
| `/map_point_history/*/*/rooms/*/turns_taken` | Completed-room turn statistic; active `turn` remains for gameplay and observation. |
| `/map_point_history/*/*/rooms/*/model_id`, `room_type`, and `map_point_type` | Diagnostic floor descriptors; active room/encounter, generated queues and meaningful visited path remain. |
| `/map_point_history/*/*/player_stats/*/damage_taken`, `hp_healed` | Export aggregates (`State.damage_taken`, `healing`, `RunState.initial_heal`); actual HP/healing effects and gameplay hit summaries remain; combat diagnostic history is exporter-owned. |
| `/map_point_history/*/*/player_stats/*/gold_gained`, `gold_lost`, `gold_spent`, `gold_stolen` | Floor-accounting summaries, including `RunState.gold_gained`; actual gold, payment and active reward claim state remain. |
| `/map_point_history/*/*/player_stats/*/current_gold`, `current_hp`, `max_hp`, `max_hp_gained`, `max_hp_lost`, `stolen_loot`, `is_affected_by_fur_coat`, `player_id` | Diagnostic floor summary metadata/placeholders, not current player state or public observations. Current inventory, player properties and identity remain. |
| `/map_point_history/*/*/player_stats/*/ancient_choice`, `event_choices`, `relic_choices` | Completed choice summaries; current Neow option/selection and resulting relic inventory remain. No supported or audited consumer reads these diagnostic descriptors. |
| `/map_point_history/*/*/player_stats/*/card_choices`, `cards_gained`, `cards_removed`, `cards_transformed`, `cards_enchanted`, `upgraded_cards`, `downgraded_cards` | Completed offer/edit descriptors, distinct from active gameplay history. Permanent cards, upgrade/floor/instance metadata and active card reward offers remain. |
| `/map_point_history/*/*/player_stats/*/potion_choices`, `potion_discarded`, `potion_used` | Diagnostic acquisition/use descriptors, including `RunState.discarded_potions`; actual potion slots and active replacement/reward choices remain. |
| `/map_point_history/*/*/player_stats/*/bought_colorless`, `bought_potions`, `bought_relics`, `completed_quests`, `relics_removed`, `rest_site_choices` | Export-only empty summaries in supported scope; no represented gameplay/observation state is removed. Future services with required local histories need explicit authoritative records. |
| `/players/*/extra_fields/damage_dealt`, `debuffs_applied` | Export-only cumulative counters (`State.damage_dealt`, `debuffs_applied`); actual damage and live powers remain; ordered combat diagnostic history is exporter-owned. |
| `/players/*/discovered_enemies` | Profile discovery bookkeeping accumulated from completed encounters, excluded from gameplay state; active enemy identity remains. |
| `/players/*/discovered_potions` | Profile discovery bookkeeping, excluded from gameplay state; active potion inventory/offers remain. |
| `State.cards_played`, `cards_drawn`, `shuffles` | Unexported diagnostic/benchmark totals. Gameplay summaries and captured continuation energy remain; diagnostic play/draw history is exporter-owned; current RNG streams encode actual shuffle progress. |

The v2 exporter removes the entire enumerated `map_point_history` diagnostic
surface, rather than emit a fabricated partial history. It also removes exactly
the four named player fields. Schemas forbid their reintroduction into v2, including
null placeholders. All other exported fields are unchanged and compared exactly.
Historical snapshots still validate with v1, and native self-comparison retains the
original full surface when both sides are v1.

This work preserves encounter queues, cursors, tags/exclusions and generation RNG.
It does not certify an exact elite-cycle rule from an example, introduce new game
content, or reclassify previously retained audit evidence as new verification.
