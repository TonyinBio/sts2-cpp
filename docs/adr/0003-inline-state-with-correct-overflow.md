# Favor compact inline state with correct overflow

The implementation and contract-reconciliation specification is
[issue #54](https://github.com/TonyinBio/sts2-cpp/issues/54).

Core state should favor compact inline storage for common gameplay while supporting
unusually large decks and other collections through heap overflow and a slower,
ownership-correct clone path. This balances MCTS state-copy cost against complete
gameplay parity: exceptional states must not be truncated or rejected merely for
exceeding inline capacity, and common states must not carry inline capacity sized
for extreme cases such as a 200-card deck.

Use reasonable estimated inline capacities while establishing parity with the real
game. Defer distribution measurement and capacity tuning until parity is established;
the suggested 95% is directional, not a numerical acceptance threshold.

Common-state clones must use memcpy during parity development; this requirement is
not deferred until parity is established. The common-state representation must
therefore be explicitly designed for byte-copy cloning.
Remaining within a general small-vector's inline capacity does not establish that
the object can be cloned with memcpy: internal pointers and ownership semantics
still matter. Overflow storage must be copied independently so branches cannot
alias mutable state.

Use custom containers with inline elements, a size, and an overflow ID or sentinel;
do not store pointers into the inline elements. Keep heap ownership in a separate
owner outside the byte-copyable state, rather than using an arena. Inline element
types must be trivially copyable and must not contain references into the source
state. One explicit clone operation handles both paths: memcpy for a wholly inline
state, and a byte copy plus independent copying of overflow storage otherwise.
Once a collection enters overflow storage, shrinking below its inline capacity does
not move it back inline; it retains overflow storage until an explicit reset.

Determine which values belong in complete state before designing its clone layout.
Cloning must preserve the entire state; it must not omit selected historical fields
to reduce copying. Remove completed encounter records entirely rather than moving
them to a separate archive. Preserve any independently required gameplay values,
such as combat-generation identity, without retaining completed combat graphs.
This changes the historical export and checkpoint contracts; their replacement
behavior must be resolved before implementation. It supersedes the requirement in
the consecutive-run-loop specification to retain complete prior encounter graphs.
Historical export fields that require completed encounter records may be removed
unless their values are also needed for future gameplay. Historical export fidelity
does not justify retaining those records in state or in a separate archive.

The same rule applies to current-combat and current-room export-only statistics:
they may be removed when neither gameplay nor player observations require them.
Preserve the information required by known, audited mechanics even before those
mechanics are implemented. Equivalent counters, flags and per-instance metadata
may replace the original event representation; any richer information still needed
for gameplay must remain. Suspended effects own their required operands. Reclaim
completed plays and expired powers only when gameplay has no remaining references,
including captured dispatch candidates. New gameplay state must obey the same
inline, overflow and independent-clone contracts.

Current-combat diagnostic histories belong to the optional `json_io` exporter,
which attaches at creation and tracks a single linear execution. The comparison
schema and its histories remain unchanged; replacement engine counters are not
added to that schema. Ordinary simulations and clones do not carry exporter history.
Recording from an arbitrary pre-made state or recording cloned branches is unsupported.
This exporter is not an archive of completed encounters. Discard its current-combat
history when native lifecycle semantics clear it or the next combat starts.

Compact engine checkpoints include counters, identities and suspended operands,
not diagnostic exporter history. Older internal checkpoints may be rejected with
an explicit incompatible-version status. The separate JSON checkpoint migration
is not part of this change.
