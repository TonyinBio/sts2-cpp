# Separate complete state from player observations

The simulator preserves complete visible and hidden state for deterministic replay,
cloning and restoration, while its public observations expose only information the
game reveals to the player. Using one representation for both would either omit state
needed to resume gameplay or reveal hidden information such as draw order and future
room assignments; privileged integration exports therefore remain separate from the
player-facing interface.

This records an existing design decision, not a new implementation or a claim of
complete observation parity. The [design sections 4 and 6](../specs/engine/full-game-simulator-design.md#4-complete-state-accuracy-to-the-real-game)
provide the rationale; the [decision contract](../specs/engine/decision-contract.md#visibility-rules)
and [state inventory](../specs/engine/state-inventory.md) retain the detailed requirements.
The [implementation status](../specs/engine/implementation-status.md) records current coverage.
