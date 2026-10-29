# Ordered combat powers

Continuation of [#19](https://github.com/TonyinBio/sts2-cpp/issues/19) on the
byte-copyable engine introduced by #54. The legacy work remains preserved on
`implement/ticket19`; its synthetic dispatch probes are not gameplay evidence.

The concrete card slice supports Inflame, Shockwave, Tremble and Uppercut,
including upgrades, Strength and Dexterity arithmetic, Weak duration, exhaust
movement and the removal of played power cards. Creature-owned power instances
preserve application order and monotonic identities. The access interface supports
explicit type, per-applier and separate stacking policies; supported content uses
type stacking. The current inline/overflow sequence is provisional pending layout
measurements. Sources and original appliers belong
to each live power; removed power diagnostics belong to the optional linear
exporter. No accumulated combat history is stored in engine state.

`initialize_combat_scenario` creates native setup and a declared custom deck.
`apply_power_scenario` mirrors a declared native PowerCmd operation without
pretending that it is a player action or advancing the player clock. Gameplay
still executes through the public combat/action interface. The supplement only
registers diagnostic model definitions and never adds cards to generation pools.

Checkpoint revision 9 includes removed-card status, live-power source/applier
relationships and the ordered exhaust callback state. Run revision 5 includes
Joss Paper's persistent counter. Earlier revisions are rejected. Run validation
checks the exact ordered relationship between inventory and combat listeners,
the Burning Blood flag, and the synchronized Joss counter. A rechecksummed
inventory-order corruption is rejected without changing the target or scratch.
Independent clone/restoration regressions exercise changed attack/block outcomes,
partial ordered choices, threshold draws and alternative selected orders.

Source-controlled native evidence under
`tests/parity/fixtures/reference/ordered-powers/` preserves the original bytes from
commit `2111750` and the Artifact extension. All original eight directories remain
byte-for-byte unchanged. Capture, independent native self-replay, and exact C++
replay are separate checks in `tools/oracle/check_ordered_powers.py`. The eight
power cases were freshly captured/self-replayed and exactly compared at 38
boundaries during this resumption. New directories record the real exhaust
interactions separately. The final 14-case capture independently self-replays and
compares all 54 boundaries exactly. Native comparison retains all historical fields.

Artifact captures the contributing instance before power application, prevents
the first debuff and consumes only that instance in the after-modifier callback.
The next debuff sees the updated inventory. Duration processing captures power
application order before Weak/Vulnerable callbacks retire their slots; their
successors still execute. Native scenarios distinguish application/modification,
zero/removal, signed values, applier retention and player/enemy expiration.

Charon's Ashes and Joss Paper use a state-owned exhaust source, captured ordered
relic list and cursor. Ashes deals unpowered damage. A lethal hit prevents new
exhaust/draw operations, but the already captured pass still invokes downstream
Joss Paper. Its threshold counter is run-owned and synchronized after every
accepted combat input; cleanup preserves it for the next encounter. Confirmation
finishes the selected effect and its callbacks before terminal graph retirement.
No acquisition hook is replayed when cloning/restoring. Native scenario setup
declares inventory, counter, enemy HP/block, and a custom deck independently of
the comparison snapshots. It never changes generation pools.

The source contracts are pinned `Hook.AfterCardExhausted` (237–246),
`CombatState.IterateHookListeners` (411–489) and `Contains` (550–607),
`CardCmd.Exhaust` (237–250), `CardPileCmd.DrawInternal` (1007 onward),
`CharonsAshes.AfterCardExhausted`, and `JossPaper.AfterCardExhausted`,
`DrawIfThresholdMet` and saved `CardsExhausted`. New native cases cover blocked,
lethal and nonlethal damage; threshold draws; selected exhaust order; lethal first
and later selected cards; and suppression of draws/new passes while ending.
The C++ public tests branch/restore inside the real source-card selection and
continue to these effects, including a different deselected/reselected order.

Native `Hook.AfterCardPlayed` (278–292) creates two fresh listener enumerations,
ordinary then late. Every currently supported model inherits no-op handlers for
both stages. Omitting those empty stages is a source-backed equivalence for this
slice, not a general dispatcher implementation. `AbstractModel`'s corresponding
execution-finished event has only the supported hand UI lifetime consumer
(`NPlayerHand.OnSelectModeSourceFinished`); the actual native selection probe
checks source completion. Exhaust has one guarded pass and does not certify late
passes. A future nontrivial handler must restore fresh per-stage captures and the
native type-specific eligibility: removed cards/relics differ from captured power
callbacks whose owners remain attached to combat. No current supported callback
detaches another captured power before its callback. Retention for that absent
consumer is conditional, not certified by the exhaust cases or synthetic probes.

Selection cases declare zero-step atomic comparison clocks and record actual
public boundary intervals plus input counts. Native atomic `select_cards`
corresponds to ordered public select inputs and confirmation; forced room return
uses the existing native/public settlement correspondence. Public nonzero clocks
remain covered independently by the choice tests. Exact English, unrelated
content, generalized hook certification and the complete Ironclad run remain
outside this slice.
