# Slay the Spire 2 simulator

Language for matching single-player gameplay against a pinned Slay the Spire 2 build.

## Language

**Parity**:
Agreement between the game and simulator for the same inputs on a declared comparison surface, such as observations or complete state. A passing comparison establishes only its tested scope.

**Oracle**:
The installed game used as the reference source through the `sts2-cli` adapter.
_Avoid_: Reference when it could mean a stored fixture rather than the running game.

**Ruleset**:
The gameplay definitions and semantics associated with a specific game build and content identity.

**Run**:
One playthrough from its initial configuration through its in-run decisions to an ending.

**Complete state**:
All visible and hidden information needed to determine future gameplay and observations, including outstanding choices and suspended effects.
_Avoid_: Observation or native save as a synonym for complete state.

**Observation**:
The information the player can see at a particular point in play, including revealed choices and feedback.
_Avoid_: Complete state, debug state.

**Boundary**:
A logical capture point where gameplay awaits input, has completed, or has stopped with an operational failure. Each contract defines its publication and capture rules.

**Prompt**:
A request for player input with offered options, selection constraints and any available confirmation, cancellation or skipping.

**Action**:
One submitted player input at the current boundary.

**Continuation**:
The unfinished part of an effect that must resume after a required choice, together with the information needed to resume it.

**Snapshot**:
A record of game state at a particular point in play; its format determines which state it contains.
_Avoid_: Complete checkpoint unless completeness has been established.

**Checkpoint**:
A restorable simulation state that preserves future behavior, including hidden state and suspended execution.
_Avoid_: Native save or observation as a synonym.

**Native save**:
The game's own saved-run representation, which does not by itself establish complete runtime state.

**Model ID**:
The identity of a game definition, such as a card type, shared by its instances.
_Avoid_: Instance ID.

**Instance ID**:
The identity of one particular game object, distinct from its definition and current location. Two copies of a card have different instance identities.
_Avoid_: Model ID, display position.

**Profile**:
A named recording format or supported set of mechanics, such as `actions-v1`; its meaning is scoped by the relevant contract.
_Avoid_: Player profile when referring to a recording profile.

**Native**:
Originating in the game's own data, behavior or format.
