# Observations, actions and input boundaries, revision 1

This contract belongs to [the pinned matching target](matching-contract.md). It defines
logical records for the new simulator, not screen layouts, existing JSON replay formats
or an implemented API. Names below are normative semantic fields; wire encoding is
separate. All sequences retain the order shown by gameplay. Missing, hidden, zero and
empty are distinct states.

## Observation record

| Record | Required fields and meaning |
| --- | --- |
| Identity | Contract revision, game/rules/content identity, boundary ID, phase, validity |
| Run | Character, seed display where exposed, ascension, modifiers, act/floor, current room and public current-run history |
| Player | Current/max HP, gold, visible combat resources and capacities; ordered deck, relics and potion slots with stable instance IDs |
| Map | Visible topology, node types/icons, visited path, current location, available routes and revealed boss information |
| Room | Tagged public room payload: combat, event, shop, rest, treasure, rewards or transition |
| Combat | Turn/side, player and visible entities in display order, HP/block/resources, powers, intents, hand, pile counts and inspectable pile contents |
| Instance | Stable identity, definition ID, owner, visible modifications/counters, upgrade/enchantment/affliction data and applicable previews |
| Prompt | Prompt ID/kind/owner, exact English instruction, ordered options, selection constraints, current partial selection, confirm/cancel/skip availability |
| Actions | Enabled inputs and displayed disabled options, typed restriction codes, exact visible English reasons, targets and costs/previews when shown |
| Feedback | Ordered visible outcomes and exact English feedback since the previous accepted input, when requested |
| Terminal | Win/loss/completion kind, visible score breakdown and current-run result/history |

Every visible definition has its pinned English name, descriptions and substitutions.
Do not expose internal formulas as player-facing reasons. Preserve number formatting,
pluralization, markup meaning and dynamic values; graphical animation timing is outside
scope. Previews are evaluated without mutation or RNG. Optional feedback events capture
values at emission time so subsequent mutation cannot rewrite what happened.

Definition IDs name content; instance IDs distinguish copies and preserve relationships.
Visible IDs cannot encode hidden pool slot order or future assignments. A projection may
omit inaccessible objects but must keep the identity of an object that disappears and
later reappears. Numeric values use explicit units and signedness in the eventual schema.

## Visibility rules

| Data | Public projection | Privileged state only |
| --- | --- | --- |
| Cards | Hand order; permanent deck and inspectable pile information as the game reveals it | Actual draw sequence and hidden generated choices |
| Draw-pile inspection | Revealed contents in the game's inspection order | Never infer draw order from backing storage order |
| Enemies | Visible entities, powers and revealed intent/preview | AI state, move history not displayed, future rolls |
| Map | Visible edges, icons, visited/revealed information | Unrevealed unknown-room resolutions, encounter/event assignments and future maps |
| Rewards/shop | Generated offers once revealed; displayed prices and sold/claimed state | Future offers, weighted pools, roll results and hidden stock |
| Event | Current text, visible options, displayed requirements and previews | Hidden branches, variables, future RNG outcomes |
| Randomness | Seed where normally shown | Stream internals, counters, derived generation seeds and trace |
| Execution | Current required choice and public feedback | Effect frames, source captures, trigger cursors and pending mutations |

A mechanic may explicitly reveal normally hidden information. Store that reveal state
and project only the scope/duration it grants. Visibility must be specified per mechanic;
unknown visibility fails support rather than exposing all state. A disabled option is
included only if the player can see it in that context. Unoffered options are not
fabricated as disabled. Debug state is a separate privileged API.

## Action payloads

Every action carries `expected_boundary_id`, its kind and exactly that kind's payload.
Use stable instance/option/node IDs, not display positions as persistent identities.
Selection arrays are borrowed for validation only; accepted continuations own what they
need. The action space describes choices and constraints lazily.

| Kind | Payload and constraints |
| --- | --- |
| Configure/start | Character, original seed, ascension, ordered modifiers and parameters; one setup prompt at a time |
| Travel | Offered map-node identity; adjacency and travel exceptions validated |
| Play card | Card instance, target when required, additional values only when the same real prompt requests them |
| Use/discard potion | Potion-slot instance and required target; discard only where available |
| End turn | No extra payload; allowed only at the normal combat input boundary |
| Select option | Offered event, ancient, rest, treasure, service or other option identity |
| Claim/skip reward | Reward identity and offered alternative; card/replacement subprompts remain separate |
| Purchase | Inventory entry identity, including removal service; do not combine later selections |
| Select objects | Prompt ID and ordered list of object/option IDs; respect min/max, distinctness, eligibility and ordering |
| Select target/value | Prompt ID and eligible target or bounded numeric value when explicitly requested |
| Confirm | Prompt ID; available only after valid partial selection when confirmation is exposed |
| Cancel | Prompt ID; only when offered; effect and payment rollback follow that mechanic |
| Continue | Offered continuation identity, including room exit/act/result acknowledgment |

Selections expose minimum/maximum count, whether order matters, whether repetition is
allowed, eligible objects, partial selection and whether the prompt submits immediately
or needs confirmation. A zero-item selection, skip, cancellation and confirmation are
separate semantics. Cost refund rules are not inferred from the existence of cancel.

Restriction categories include stale boundary, wrong phase/prompt, malformed payload,
unknown/stale object, ineligible selection/target, selection count/order/duplication,
insufficient resources, unavailable option, unsupported content and invalid simulation.
Only restrictions visible to the player receive public English reasons. Internal
validation diagnostics must not disclose hidden state through disabled action records.

## Boundary publication

`initialize`, accepted `apply` and `advance` run automatic effects until required input,
terminal completion or operational failure. A new boundary is published once each time
a new logical prompt or normal-input opportunity becomes active. Its counter increases
by one within that simulation. A normal combat opportunity after an accepted card or
potion action is a new boundary even if the available actions happen to be identical.
The counter and prompt identity are complete state, preserved by clone and restore.

| Phase / stop point | Offered input | Automatic work before the next stop |
| --- | --- | --- |
| Setup | Configuration or a starting choice | Initialize run and resolve starting effects |
| Map | Legal route | Travel hooks, room resolution, room entry |
| Combat ready | Play, potion, end turn and other explicitly available combat inputs | Payment and effects in game order; enemy/turn processing when appropriate |
| Nested choice | Required selection, target, confirm/cancel where offered | Resume the owning effect, including further nested choices |
| Rewards | Claim, alternative, skip or continue as offered | Resolve claim hooks, replacement, follow-up or room exit |
| Shop | Purchase, potion action where offered, exit | Purchase hooks, service prompts, inventory changes |
| Event/ancient | Visible branch or continue | Branch effects, child room or follow-up text/prompt |
| Rest/service | Offered action, object selection or cancel | Service and hooks, then return/exit as the game does |
| Treasure | Offered treasure action or continue | Acquisition/replacement and hooks |
| Act transition | Explicit continuation only if required | Generate next act, enter starting context |
| Terminal | No gameplay input; result acknowledgment only if exposed | No further gameplay transition |

Room kinds from `RoomType` are `Monster`, `Elite`, `Boss`, `Treasure`, `Shop`,
`Event`, `RestSite`, `Map`; `Unassigned` is not a playable room. A map-point type is
separate from its resolved room type. An event can push a combat child room and later
resume; room kind alone cannot identify a continuation.

`advance` at pending input returns the same boundary without consuming it. Repeated
`observe`, `actions`, formatting and debug calls neither publish boundaries nor consume
RNG. Partial-selection inputs consume only the currently exposed prompt step; they
cannot skip a follow-up prompt. Two identical-looking nested prompts remain distinct.
Input boundaries are logical waits for player decisions, not animation or network waits.

Validation checks the complete input before any authoritative mutation or RNG draw.
Rejected input preserves the whole state, including identity allocation and boundary
counter. Unsupported behavior is explicit. Execution exhaustion or internal failure
invalidates the instance; it is not a game loss or an observable valid partial result.
Successful restore/reinitialization can make it valid again. Public views borrow storage
and expire after state mutation or reuse of their projection workspace.
