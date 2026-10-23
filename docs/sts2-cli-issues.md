# sts2-cli issue tracker

Track upstream `sts2-cli` problems here for local follow-up. Entries in this file
have not been posted to GitHub.

## CLI-001: Sea Glass Ancient option omits the selected character

- Status: Open
- Reported: 2026-10-23
- Area: Ancient event option names and descriptions
- Affected interface: `sts2-cli/python/play.py`, also used by `tools/oracle/play.py`

At the act-2 Orobas event, the Sea Glass option does not identify which character's
cards it will offer. The displayed description contains an unresolved template:

```text
Sea Glass — See 15 cards from {Character.StringValue:cond:{}|another character}. Choose any number of them to add to your Deck.
```

Expected: show the character selected for this offered relic and resolve the
description without template syntax. Use the character-specific relic title when
available (Gear Glass, Demon Glass, Lich Glass, Noble Glass, or Venom Glass).
The character is needed to make an informed reward choice.

To reproduce, enter an Orobas event offering Sea Glass and inspect its option
before choosing it. The report came from an Ironclad run with seed `1234`,
ascension 0, at the beginning of act 2.

Source inspection points to the upstream C# `EventChoiceState` formatter reading
raw localization text and extracting numeric variables, including variables from
a fresh relic model, rather than formatting the actual offered relic's string
variables. The Python `desc` and `resolve_template` helpers do not correctly
resolve this nested string conditional. Our recorder reuses this upstream event
formatter through `reference_runtime.py`; it is not specific to recording.

Relevant sources:

- [`EventChoiceState`](../sts2-cli/src/Sts2Headless/RunSimulator.cs)
- [`desc`, `resolve_template`, and `show_event`](../sts2-cli/python/play.py)
- [Sea Glass localization](../sts2-cli/localization_eng/relics.json)
- [Recorder event formatter reuse](../tools/oracle/reference_runtime.py)

Acceptance: before selecting Sea Glass, both interfaces display the actual
offered character and a fully resolved description. Inspecting the option must
not reroll its character or consume gameplay RNG. Add regression coverage for
the offered relic's string variables and nested conditional text.

## CLI-002: Cannot inspect draw, discard, or exhaust piles during combat

- Status: Open
- Reported: 2026-10-23
- Area: Combat inspection commands
- Affected interface: `sts2-cli/python/play.py`, also used by `tools/oracle/play.py`

During combat, the interface shows draw and discard pile counts but provides no
command to view the cards in the draw, discard, or exhaust piles. The `deck`
command shows the run's deck, which does not identify each card's current combat
pile or include all temporary combat cards.

To reproduce, enter combat and type `help`. There are no pile inspection
commands; typing `deck` displays the full deck rather than the combat piles.

Expected: allow inspecting the contents of each combat pile at a combat prompt,
including card names and relevant card details. Display draw pile contents
without revealing the hidden draw order, consistent with the game's normal
player-visible information.

Relevant sources:

- [`get_input`, combat help, and `show_combat`](../sts2-cli/python/play.py)
- [Combat display export](../sts2-cli/src/Sts2Headless/RunSimulator.cs)

Acceptance: provide discoverable draw, discard, and exhaust inspection commands
in both interfaces. Handle empty piles, upgraded cards, and temporary combat
cards. Inspection must not advance combat, consume RNG, or add gameplay actions
to saves or recordings. Add regression coverage for pile contents and inspection
without state changes.

## CLI-003: Cannot discard potions from the interactive CLI

- Status: Open
- Reported: 2026-10-23
- Area: Potion action coverage
- Affected interface: `sts2-cli/python/play.py`, also used by `tools/oracle/play.py`

The interactive CLI has no command to discard a held potion. The `potions`
command only lists inventory, and `p0` during combat uses potion 0. The backend
already supports `discard_potion` with a zero-based `potion_index`, but the
interactive input handler does not expose that action.

To reproduce, hold a potion and inspect `help` or enter `potions`. There is no
discard option.

Expected: expose a discoverable command to discard a held potion, freeing its
inventory slot without using its effect. Refresh the displayed inventory and
indices after discarding.

Relevant sources:

- [`get_input` and combat potion handling](../sts2-cli/python/play.py)
- [`DoDiscardPotion` and action dispatch](../sts2-cli/src/Sts2Headless/RunSimulator.cs)

Acceptance: both interactive interfaces can discard a potion through the existing
backend action. Help documents the command; invalid indices are handled without
changing state. Add regression coverage for discarding, inventory refresh, and
subsequent potion selection after indices shift.
