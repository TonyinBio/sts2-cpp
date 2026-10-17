# Oracle state-access probe

The local check on October 17, 2026 confirmed that an extended `sts2-cli` can export
ordered combat piles and serialized RNG state from the installed game DLL. A replacement
submodule is not necessary for this access.

## Reproduce

Requires Python 3, a Linux .NET 9 SDK, the initialized `sts2-cli` submodule, and a
local game installation. Run from the repository root in WSL:

```sh
python3 tools/oracle/check_state_access.py \
  --game-dir '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2'
```

The script copies the adapter and required game DLLs to a temporary directory,
injects `StateAccessProbe.cs` and a `probe_state` command, builds, then starts two
independent reference processes. It does not modify the submodule or Steam files.
The initial .NET restore may require network access.

Reports and build/process logs go to ignored `build/oracle-check/`. Source and DLL
copies are removed when the script exits. No game DLLs are checked into this repo.
The probe copies original DLLs without applying setup.sh IL patches; the adapter's
existing Godot stubs and runtime patches are still active.

## Verified build

- Game: v0.111.0, commit `41cef1ea`, Windows x86_64 managed DLL under Linux .NET.
- Reference: `084d1aa3d8e118ca7ce8d8774ad16d6be9c92367`.
- Original `sts2.dll` SHA-256:
  `0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.
- Assembly MVID: `73b63ee0-6c0a-47bb-b0d1-b21f6d94222e`.

The generated report records the actual DLL hash, assembly MVID, reference revision,
release metadata when present, commands, and snapshots for subsequent checks.

## Evidence

Seed `ORACLECHECK01`, Ironclad, encounter `SHRINKER_BEETLE_WEAK`, snapshots at player
play phase in rounds 1–3:

- `CardPile.Cards` enumerates in the same order as private `_cards`. Hand, draw,
  discard, and exhaust collections are accessible. Exhaust is empty in this scenario.
- Ten starter card instances retain probe-assigned identities as they move between piles.
- The five cards remaining in the initial draw pile become the round-2 hand in order.
- The round-3 reshuffle changes the run Shuffle counter from 9 to 18.
- `run.Rng.ToSerializable()` exports every one of the 12 `RunRngType` entries.
- `player.PlayerRng.ToSerializable()` exports all 3 `PlayerRngType` entries.
- Each serialized RNG contains `counter` and four unsigned 64-bit words (`s0`–`s3`).
- `Rng.LoadFromSerializable()` restores each export into fresh RNG objects. Two
  differently seeded objects restored from the same state produce the same next
  16 unsigned-long outputs and matching resulting states.
- Consecutive exports with no action between them match, and both fresh reference
  processes produce identical sequences of exported snapshots.

Run streams: UpFront, Shuffle, UnknownMapPoint, CombatCardGeneration,
CombatPotionGeneration, CombatCardSelection, CombatEnergyCosts, CombatTargets,
MonsterAi, Niche, CombatOrbs, TreasureRoomRelics.

Player streams: Rewards, Shops, Transformations.

Use integer-safe parsing for the state words: JavaScript numbers cannot exactly
represent every 64-bit integer. Python integers and C++ uint64_t can.

## Limits and next implementation

This is a targeted accessibility check, not a complete combat snapshot format or a
proof that the headless adapter matches the running Godot game. In particular:

- Card exports contain only model ID, pile index, and a process-local probe identity;
  upgrades, current costs, temporary effects, and other card state are not exported.
- Creature, power, relic, enemy AI, pending-choice, and action-queue state still need
  dedicated exporters. The current probe does not certify action quiescence generally.
- The RNG test proves access and restoration within the reference DLL, not C++ RNG
  compatibility, randomness trace capture, or arbitrary whole-combat restoration.
- Only this single-player starter encounter is exercised. Empty exhaust access is not
  a test of exhaust mechanics. Other encounters, characters, and builds need checks.

Build the production snapshot exporter using the existing RNG serialization APIs and
explicit combat serializers. Preserve stable instance mappings within each recording,
validate all expected streams, and fail when required state is unavailable. Keep this
probe separate from the default GoogleTest suite because it requires installed game DLLs.
