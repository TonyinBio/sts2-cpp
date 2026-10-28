# Native selection policy evidence for #18

Captured 2026-10-28 using pinned game `0.111.0+41cef1ea`, .NET 9.0.4 and actual
Godot 4.5.1 Mono. Six scenarios each passed assertions and identical results in
two independently initialized processes. The earlier four-case capture under
`native-hand-callbacks-18` remains byte-for-byte preserved.

New evidence includes native Armaments `OnPlayWrapper` →
`CardSelectCmd.FromHandForUpgrade` → actual upgrade-mode callbacks → confirmation
→ only the selected combat copy upgraded. The source and four Defend candidates
come from declared native permanent decks, normal combat setup/shuffle/draw and
native combat copies. Purity likewise uses its real deck source and confirms
ordered exhaustion `[1, 3, 0, source]`.

The pile scenario runs actual `CardSelectCmd.FromCombatPile` with Headbutt's own
prompt/preferences and the packed combat-pile screen. Its actual click callback
submits card 1 immediately; native `CardPileCmd.Add` puts that returned object on
the draw-pile top. Pile visuals are disabled for this command-consumer check.
Headbutt's complete model damage/ordering is verified separately by the exact
atomic model corpus. Full native presentation startup is outside this seam.

The native observation reader brackets its reads with fingerprints of the
enumerated RNG, HP/Block, energy/round and ordered-pile/upgrade surface. The report
records that surface explicitly; it is not a whole-game instrumentation claim.

Two C++ scenarios independently start from the corresponding source/four-Defend
configuration. Eleven partial native Purity/Armaments states match public prompt
candidate models/upgrades, ordered selection, restrictions and confirmation
availability, followed by two completed confirmations. Equivalent Defend objects
are mapped by candidate order, without importing a native checkpoint. Every
accepted C++ input advances its boundary and configured two-second clock once.
Native UI callbacks do not advance the recorder transport clock themselves; the
report records the logical-input correspondence separately from the atomic
corpus's zero-step clock profile. This verifies the specified prompt projection,
not equality of all runtime state, payment routing or every screen field.

`verification.json` retains native callback results, public correspondence,
assembly/resource/runtime/source hashes and exact teardown diagnostics.
`capture.tar.gz` preserves all twelve native logs, build log, generated probe
project/configuration, runner and public scenario-transport source.
`sha256.json` verifies artifacts and identifies the C++ binary/base revision.

All contexts are explicitly freed and two process frames awaited before a cleanup
marker. Armaments still leaves native resource-cache allocations at process exit.
Those exact allocator/renderer teardown messages are retained in both logs and
the report, and accepted only after successful cleanup. Earlier errors, unknown
error forms, failed assertions or incomplete cleanup fail the runner. This does
not certify a leak-free full application lifecycle.

Reproduce with `tools/oracle/native_choice_probe/README.md`. Nested exhaust-hook
continuations are covered by the companion #19 interaction slice, not this
isolated UI/command evidence.
