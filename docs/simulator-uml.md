# C++ simulator UML

These diagrams describe the current public C++ headers, rather than the planned
full-game design. Members are selected for readability. Most types live in
`sts2::engine`; directories indicate module boundaries, not separate namespaces.

## State ownership

```mermaid
classDiagram
    direction TB
    class RunState {
        +RunData data
        +OverflowStorage overflow
        +OverflowStorage combat_overflow
        +view() RunView
        +reset()
    }
    class RunData {
        <<struct>>
        +RunPhase phase
        +BoundaryId boundary
        +int hp
        +int gold
        +InlineSequence deck
        +InlineSequence relics
        +InlineSequence potions
    }
    class CombatState {
        +CombatData data
        +OverflowStorage overflow
        +view() CombatView
        +reset()
    }
    class CombatData {
        <<struct>>
        +CombatPhase phase
        +BoundaryId boundary
        +int energy
        +int turn
        +CardPool cards
        +InlineSequence[] zones
        +InlineSequence execution
        +CombatCounters counters
    }
    class OverflowStorage
    class PersistentCard
    class MapState
    class CombatRewards
    class RngBank
    class CreatureState
    class CombatCard
    class EffectFrame
    class RunView {
        <<borrowed view>>
    }
    class CombatView {
        <<borrowed view>>
    }

    RunState "1" *-- "1" RunData : data
    RunState "1" *-- "2" OverflowStorage : run and combat owners
    RunData "1" *-- "0..1" CombatData : current combat
    RunData "1" *-- "0..3" MapState : maps
    RunData "1" *-- "0..1" CombatRewards : rewards
    RunData "1" *-- "0..*" PersistentCard : deck
    RunData "1" *-- "1" RngBank : rng
    CombatState "1" *-- "1" CombatData : standalone combat
    CombatState "1" *-- "1" OverflowStorage
    CombatData "1" *-- "2" CreatureState : player and enemy
    CombatData "1" *-- "1" RngBank : rng
    CombatData "1" *-- "0..*" CombatCard : card pool slots
    CombatData "1" *-- "0..*" EffectFrame : suspended execution
    RunView --> RunData : borrows
    RunView --> OverflowStorage : borrows both owners
    RunView ..> CombatView : exposes current combat
    CombatView --> CombatData : borrows
    CombatView --> OverflowStorage : borrows
```

`RunData` and `CombatData` are trivially copyable. `clone_into` copies their bytes
and independently clones any overflow storage. The owning `RunState` and
`CombatState` wrappers must not themselves be cloned with a raw `memcpy`.
An embedded run combat is `CombatData`, not a nested owning `CombatState`.
Mutable and const view aliases share the `BasicRunView` / `BasicCombatView`
templates. Card zones contain handles into the pool, rather than card copies.

## APIs and integration

Boxes marked `module` group free functions; they are not C++ classes.
Dashed arrows indicate use, solid diamonds indicate ownership.

```mermaid
classDiagram
    direction TB
    class Session {
        +initialize(setup)
        +apply(action) StepResult
        +snapshot() json
    }
    class RunState
    class Scratch {
        +uint32_t step_limit
        +uint32_t steps
        +CombatTrace trace
    }
    class NativeRunExporter
    class PlayerInput {
        <<variant>>
        RunInput or CombatInput
    }
    class RunAPI {
        <<module>>
        initialize_run(state, config)
        get_actions(state)
        validate_action(state, input)
        apply_action(state, input, scratch)
        advance_to_decision(state, scratch)
    }
    class CombatAPI {
        <<module>>
        initialize_combat(view, config, scratch)
        validate_combat(view, input)
        apply_combat(view, input, scratch)
        advance_combat(view, scratch)
    }
    class CombatView
    class Ruleset
    class SetupRules
    class ObservationAPI {
        <<module>>
        observe(state) RunObservation
        observe(view) CombatObservationView
        observe_card(view, handle) CardView
    }
    class RunObservation
    class CombatObservationView
    class PersistenceAPI {
        <<module>>
        checkpoint(state) Checkpoint
        restore(state, bytes, scratch) RestoreStatus
    }
    class Checkpoint {
        +vector~byte~ bytes
    }

    Session *-- RunState
    Session *-- Scratch
    Session *-- NativeRunExporter
    Session ..> RunAPI : submits inputs
    RunAPI ..> PlayerInput
    RunAPI ..> RunState : mutates
    RunAPI ..> Scratch
    RunAPI ..> SetupRules : setup definitions
    RunAPI ..> CombatAPI : combat transitions
    CombatAPI ..> CombatView : mutates through view
    CombatAPI ..> Scratch
    CombatAPI ..> Ruleset : card definitions
    Scratch ..> NativeRunExporter : optional trace callback
    NativeRunExporter ..> RunState : reads for export
    ObservationAPI ..> RunState : projects visible state
    ObservationAPI ..> CombatView : projects visible state
    ObservationAPI ..> RunObservation : returns
    RunObservation *-- "0..1" CombatObservationView
    PersistenceAPI ..> RunState : saves or restores
    PersistenceAPI ..> CombatView : saves or restores
    PersistenceAPI ..> Checkpoint : produces or consumes bytes
```

`apply_action` advances automatic transitions to a decision or terminal result.
Inputs carry an expected boundary identity. Observations exclude hidden RNG,
draw order, and suspended execution. Some observation fields borrow state storage
and are invalidated by mutation. `Scratch` is discardable; authoritative
continuations live in state. Diagnostic exporter history belongs to the optional
integration layer and is not carried into cloned engine state.

The persistence API currently exposes byte checkpoints. JSON checkpoints are a
recorded direction in [ADR 0004](adr/0004-use-json-for-restorable-checkpoints.md),
not represented here as an implemented API.

## Source map

| Area | Headers |
| --- | --- |
| Owning state and views | `include/sts2/state/run.hpp`, `include/sts2/state/combat.hpp` |
| Inline and overflow storage | `include/sts2/core/inline_storage.hpp`, `include/sts2/core/storage.hpp` |
| Run actions and combat transitions | `include/sts2/engine/action.hpp`, `include/sts2/engine/run.hpp`, `include/sts2/engine/combat.hpp` |
| Definitions | `include/sts2/content/cards.hpp`, `include/sts2/content/catalog.hpp` |
| Player observations | `include/sts2/observation/run.hpp`, `include/sts2/observation/observe.hpp` |
| Checkpoints | `include/sts2/persistence/checkpoint.hpp` |
| JSON integration | `include/sts2/json_io/session.hpp`, `include/sts2/json_io/native_run.hpp` |
