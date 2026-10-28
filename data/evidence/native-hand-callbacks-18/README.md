# Native hand callback evidence for #18

Captured 2026-10-28 with `tools/oracle/check_native_choice_callbacks.py` against
pinned game `0.111.0+41cef1ea`, .NET 9.0.8 and actual Godot 4.5.1 Mono. Four cases,
each independently initialized and repeated in a fresh process, passed native
assertions and identical callback/result comparison. No C++ result is claimed
by this capture.

`verification.json` records native callbacks, returned card identities, Purity's
exhaust continuation, assembly MVID, game DLL/resource pack/runtime hashes,
executable probe sources and generated project hashes. `capture.tar.gz` retains
all eight native logs, build log and exact generated project source/configuration
files. `sha256.json` verifies the report and archive. Native shutdown warnings
about leaked scene objects/RIDs are retained; no runtime errors occurred.

The `ordered`, `zero`, and `single` cases establish real initialized hand callback
semantics with declared synthetic preferences. `purity-consumer` additionally
executes actual native `OnPlayWrapper` → `Purity.OnPlay` →
`CardSelectCmd.FromHand` → packed hand scene callbacks → ordered native exhaustion
and source completion. There is no test selector, native-method replacement or
imported checkpoint. Source selection fields are observed, never seeded.

See `tools/oracle/native_choice_probe/README.md` for reproduction and limits.
This is a declared native scene consumer scenario: supplied NGame context,
native test-run/encounter setup, zero-resource Purity autoplay wrapper, native
blocking choice context and noninteractive waits. It does not certify manual
action queue/payment, clocks, C++ projection, other cards, pile screens, nested
hooks, or complete #18 acceptance. Separate exact atomic comparison fixtures
remain necessary.
