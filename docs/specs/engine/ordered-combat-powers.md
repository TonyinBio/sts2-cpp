# Ordered combat powers

Continuation of [#19](https://github.com/TonyinBio/sts2-cpp/issues/19) on the
byte-copyable engine introduced by #54. The legacy work remains preserved on
`implement/ticket19`; its synthetic dispatch probes are not gameplay evidence.

The concrete card slice supports Inflame, Shockwave, Tremble and Uppercut,
including upgrades, Strength and Dexterity arithmetic, Weak duration, exhaust
movement and the removal of played power cards. Fixed power-type slots preserve
application order and monotonic identities. Sources and original appliers belong
to each live power; removed power diagnostics belong to the optional linear
exporter. No accumulated combat history is stored in engine state.

`initialize_combat_scenario` creates native setup and a declared custom deck.
`apply_power_scenario` mirrors a declared native PowerCmd operation without
pretending that it is a player action or advancing the player clock. Gameplay
still executes through the public combat/action interface. The supplement only
registers diagnostic model definitions and never adds cards to generation pools.

Checkpoint revision 7 includes removed-card status and live-power source/applier
relationships. Earlier revisions are rejected. Independent clone/restoration
regressions exercise changed attack and block outcomes and alternative futures.

Source-controlled native evidence under
`tests/parity/fixtures/reference/ordered-powers/` preserves the original bytes from
commit `2111750`. Capture, independent native self-replay, and exact C++ replay are
separate checks in `tools/oracle/check_ordered_powers.py`; saved evidence does not
claim a fresh native execution. Native comparison retains all historical fields.

Remaining #19 acceptance includes supported modifier prevention, mutation-safe
captured ordinary/late passes and combat completion during an exhaust listener
pass. Synthetic listener-removal controls cannot close those obligations. This
slice alone does not close #19 or certify the complete Ironclad run.
