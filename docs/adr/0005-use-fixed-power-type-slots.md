# Use fixed power-type slots instead of cached creature effects

Use fixed arrays indexed by power type for direct access to powers such as
Vulnerable and Shrink, removing their duplicated values from `CreatureState`.
Accept the larger state footprint and defer measuring its impact until the full
game is implemented. This deliberately qualifies ADR 0003's preference for compact
active collections; flat, byte-copyable state and independent cloning remain
required.

The storage design must preserve native power application order and distinct
instances of the same power type: `PowerCmd.FindExistingInstanceForStacking`
distinguishes ordinary, instanced and per-applier powers. Fixed type slots must not
merge those instances or make dispatch follow enum order. Reserve slots only for
implemented power types, expanding the fixed arrays as support grows. Track active
application order with compact slot IDs, without duplicating power records.

Multiple-instance support must not impose a large common-state overhead. The
currently implemented powers, Vulnerable and Shrink, are non-instanced, so do not
reserve or allocate additional-instance storage now. When an instanced power is
implemented, add compact storage for its additional instances, favoring inline
storage with independent overflow ownership under ADR 0003 rather than a large
fixed instance array per power type or per creature. Preserve separate identities
and captured references; do not merge instances to save space. Review the actual
footprint then, without requiring full-game performance measurements now.

## Follow-up: revisit application-order tracking

Keep explicit application-order tracking for now to preserve native trigger order.
The user prefers to remove it if a smaller representation or workaround can preserve
the same behavior, and explicitly requests that this be revisited when the full
game is implemented. At that point, evaluate which interactions require ordering
and whether a smaller representation can reproduce them; do not treat the current
tracking mechanism as a permanent architectural requirement. Deferring that review
does not permit changing gameplay order in the meantime.
