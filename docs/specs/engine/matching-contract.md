# Full-game matching contract, revision 1

This is the non-recording portion of implementation step 1 in the
[architecture](full-game-simulator-architecture.md#7-verification-and-implementation-order).
It defines the target, public interface and required state before implementing the new
kernel. It does not expand the current scaffold's supported gameplay. Real-game
recording, replay fixtures, checkpoint-import fixtures and parity certification are
outside this change.

## Target identity and configuration

| Identity | Required value |
| --- | --- |
| Game | Slay the Spire 2 v0.111.0 |
| Release commit | `41cef1ea` |
| `sts2.dll` SHA-256 | `0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9` |
| Reference wrapper | `sts2-cli` commit `084d1aa3d8e118ca7ce8d8774ad16d6be9c92367` |
| Native save schema | 20; not the future simulator checkpoint schema |
| Language | English |
| Players / mods | One local player; no mods |
| Unlock configuration | All single-player content and difficulty levels unlocked; fixed for the run |
| Characters | Ironclad, Silent, Regent, Necrobinder, Defect |
| Ascension | Integers 0 through 10, inclusive, with cumulative game modifiers |
| Seeds | Original seed string and the game's derived numeric seed; preserve both |
| Modes | Standard and seeded single-player runs; explicit supported modifier lists |

The version/hash comes from the [installed-build audit](../combat-runtime-audit.md).
The wrapper revision is the checked-out submodule revision. Neither identity means
that all behavior has been verified. Never silently accept a different DLL or content
bundle under this ruleset identity. New builds require a separate manifest and audit.

Ascension names in numeric order are `None`, `SwarmingElites`, `WearyTraveler`,
`Poverty`, `TightBelt`, `AscendersBane`, `Inflation`, `Scarcity`, `ToughEnemies`,
`DeadlyEnemies`, `DoubleBoss` (`AscensionLevel.cs`). Do not import ascension rules
from the first game. Modifier order and parameters are configuration, not a set of
names. Every reachable single-player modifier is in the eventual target; configurations
remain unsupported until all their mechanics are implemented. Daily service discovery,
leaderboards, multiplayer, profiles, achievements, cosmetics and platform menus are
excluded. A daily's gameplay modifiers can be supplied explicitly; the simulator does
not contact the daily service. Timing-dependent score/history values require an explicit
clock input policy rather than reading wall time inside transitions.

Initialization validates all identity and configuration fields before publishing state.
Unknown IDs, unsupported configurations, non-English locale and multiplayer requests
return an explicit unsupported result; there is no generic content fallback.

## Content inventory and coverage

[content-inventory-v0.111.0.tsv](content-inventory-v0.111.0.tsv) enumerates 1,602 concrete
model declarations in 16 gameplay categories from the audited local decompilation.
It records source class/path, scope, implementation, reference verification and
interaction verification separately. These are source identities, not guessed model
IDs or reward pools. Actual IDs must follow `ModelDb.GetId`, `ModelId` and
`StringHelper.Slugify`; reward eligibility must follow the actual pool and generation
filters.

| Category | Declarations | Category | Declarations |
| --- | ---: | --- | ---: |
| Cards | 596 | Powers | 265 |
| Relics | 300 | Potions | 65 |
| Monsters | 120 | Encounters | 90 |
| Events (including ancients) | 68 | Characters | 8 |
| Acts | 5 | Modifiers | 17 |
| Card pools | 14 | Potion pools | 10 |
| Relic pools | 9 | Orbs | 5 |
| Enchantments | 23 | Afflictions | 7 |

The inventory excludes mock subnamespaces and abstract classes. It identifies 37 cards
with an explicit `MultiplayerOnly` constraint and 14 `Deprecated*` declarations as
excluded. The remaining 1,551 are **candidates**, not a claim that they are all
single-player reachable. `RandomCharacter` and `Deprived`, for example, are not extra
selectable characters: the five-character list comes from `ModelDb.AllCharacters`.
Deprecated pool members, tokens, event-only content and indirect generation require
reachability analysis; absence from normal rewards does not establish exclusion.
Shared powers or entities used by excluded cards can still be required elsewhere.

Rows without coverage evidence remain `not_implemented`, `not_verified`,
`not_verified`. The inventory generator merges [scoped coverage annotations](../../../data/builds/v0.111.0/coverage.json)
for the new kernel. `partial` means only the documented setup/mechanics context is
implemented. Reference and interaction statuses identify the tested subset; they
never imply complete-run support. The old attack/block scaffold does not establish an implementation of a pinned
model. Before registering a candidate as supported, resolve its exact model ID,
single-player reachability, generation conditions and dependencies, then implement its
semantics. Reference and interaction status advance independently with evidence.
An excluded declaration must never be admitted through a generation pool by default.

Reproduce the declaration inventory without running the game or creating recordings:

```sh
python3 tools/contracts/inventory_content.py \
  --source build/combat-state-audit/decompiled --check
```

The ignored source tree is the ILSpy 9.1.0.7988 output described in the existing audit.
The script scans declarations and explicit card constraints; it is not a C# evaluator
or a reachability checker. The checked-in TSV can be read without that source tree.

## Player-visible interface

The normative revision-1 interface is [decision-contract.md](decision-contract.md).
It specifies observations, action payloads, visibility and stop points independently of
JSON transport or the current adapter's limitations. A future schema or C++ record must
preserve this contract. English text and structured values are separate matching
surfaces. Shared text comes from the pinned build; the submodule's localization files
are useful inputs but need content/build validation before being treated as authoritative.

## Revised storage and export scope

[ADR 0003](../../adr/0003-inline-state-with-correct-overflow.md) requires whole-state
common-case memcpy cloning, custom inline containers and independent optional heap
overflow. Remove completed encounter records entirely. Keep histories needed by known
gameplay mechanics, generation progress, hidden state and player observations. Current
and historical export-only fields may be removed only through an explicitly enumerated
comparison-surface revision with per-field rationale; all remaining fields stay exact.
Original reference bytes and prior results remain unchanged. Runtime memcpy is not a
checkpoint wire format. Revised schemas and checkpoint compatibility require explicit
versioning and fresh verification; this decision is not evidence of implemented parity.

## Complete-state inventory

[state-inventory.md](state-inventory.md) assigns visible and hidden state to owners,
identifies saved versus additional runtime state, and points to the existing detailed
combat member audit. A native save, public observation or engine checksum alone is not
complete state. Outstanding choices and their suspended continuations are required.

## Completion and evidence limits

The non-recording deliverables are the pinned scope, enumerated declaration inventory,
public decision contract and complete-state requirements inventory. They are ready to
use for storage/interface work in step 2. They do not claim source reachability or
behavioral completeness for every candidate. Remaining evidence work includes per-model
reachability, non-combat member-by-member audit, exact visibility exceptions, timing
policy, RNG call-site ordering and suspended local values for each handler. Track these
as explicit gaps rather than filling them with defaults.

The recorder and all recording/replay/import fixtures from the original step 1 are
intentionally deferred. Their absence means full step-1 parity acceptance remains
unverified; it does not prevent implementing storage against these requirements.
