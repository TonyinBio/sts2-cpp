# English resources and adapter rendering

Source audit for [Establish authoritative pinned English sources and rendering evidence](https://github.com/TonyinBio/sts2-cpp/issues/12).
This separates resource-byte verification from native rendering certification. No live
oracle or graphical game session was run for this adapter audit. No exact native
rendered-output fixture was captured here.

## Resource provenance

The inspected submodule revision is
`084d1aa3d8e118ca7ce8d8774ad16d6be9c92367`. Its `localization_eng` contains 46 JSON
tables and 7,028 entries. The installed PCK extraction prepared by the parent audit
under `build/english-source-audit/extracted/localization_eng` has exactly the same
46 filenames; all 46 files compare byte-for-byte equal, with no key/value difference.
See `adapter-resource-comparison.json` for per-table hashes and counts. The parent
provenance manifest identifies the installed PCK, extraction and game identity.
This establishes that these submodule English resource bytes agree with that PCK;
it does not establish that the adapter renders them as the native game does.

`sts2-cli/setup.sh` lines 102–107 refreshes English/Chinese from the first discovered
PCK when available, otherwise keeps existing resources. The extractor
`scripts/extract_localization.py::extract` validates supported unencrypted Godot PCK
v2/v3 headers, matching localization entries, MD5 of each selected entry and JSON
parseability before writing. It writes files present in the PCK without deleting
stale local tables. Therefore a clean directory plus exact path-set comparison is
needed when using it to establish authoritative resources; successful extraction
alone does not certify the absence of extra local keys/tables.

`tools/oracle/reference_runtime.py::reference_build` lines 284–313 checks the game
DLL hash and submodule revision/cleanliness, copies the submodule into a disposable
tree and injects adapters. Its source digest includes copied C#/project files,
wrapper inputs, schemas and DLLs, but has no explicit localization-resource input.
The pinned clean submodule constrains tracked tables today; future evidence should
record actual resource hashes instead of treating an adapter/DLL hash as a text hash.

## Three different text paths

| Path | Source anchor | Actual behavior and certification limit |
| --- | --- | --- |
| Display dictionary lookup | `RunSimulator.cs::LocLookup` lines 93–196 | Loads `localization_eng`/`zhs` JSON from a path relative to assembly output; missing directory and parse exceptions silently ignored. `Bilingual` returns selected raw value or key, then strips a subset of BBCode tags. It does not call native `LocString.GetFormattedText` or SmartFormat. A valid resource template is therefore often emitted with unresolved braces |
| Native model localization in headless host | `RunSimulator.cs::InitLocManager` lines 3460–3690 and `LocPatches` lines 3718–3810 | Creates an uninitialized native LocManager, installs dictionary-backed LocTables, tries to set English/invariant culture, initializes formatter by reflection with fallback paths, then patches several native table/existence methods. It is a modified native localization environment, not an independent native rendering oracle |
| Terminal-friendly Python rendering | `sts2-cli/python/play.py::desc`, `resolve_template`, `card_desc` lines 323–484 | Removes markup, rewrites SmartFormat into bracket substitutions and makes context assumptions; useful convenience rendering, unsuitable as exact native English expected output |

The critical headless patch is stronger than its adjacent comment suggests:
`InitLocManager` installs `LocPatches.GetRawTextPrefix` on `LocTable.GetRawText`, and
that prefix always sets the result to the key and returns false. It is not a
missing-key-only fallback. `HasEntry`, `IsLocalKey` and `LocString.Exists` are
patched true; prefix enumeration returns an empty list. These source facts prevent
using this patched pipeline to independently certify missing-key behavior, native
formatter output, key existence or prefix-dependent text. Do not infer formatter
success merely from log comments such as “real localization pipeline works.”

`tools/oracle/adapters/RecordingDisplay.cs::OracleDisplay` changes the lookup's
language on request. `DisplayCard` copies the raw dictionary description and base
dynamic vars; `CombatPlayState` copies dictionary descriptions and separate resolved
preview stats. `GetUpgradedInfo` creates/upgrades a fresh model for stats but still
returns the same dictionary description template. This does not resolve upgraded
conditional wording or preserve instance-specific modifier text. `PlayerSummary`
similarly separates relic/potion description templates from BaseValue variables.

Generated `RecordingEventState` comes from
`reference_runtime.py::inject` lines 198–208 and the formatting suffix of upstream
`EventChoiceState` lines 2523–2655. It looks up title/description templates by key,
falls back to relic/card titles or identifier-derived text, copies event dynamic
vars, sometimes merges fresh relic vars, and special-cases `RandomCard` deck-index
resolution. It does not render the native event page/option LocStrings with their
complete bound objects. Literal template lookup and fallback text must be labeled
uncertified, including when the resulting string looks plausible.

## Known Python approximation gaps

`desc` chooses the non-upgraded branch of `IfUpgraded` regardless of card upgrade,
chooses the combat branch of `InCombat` regardless of context and removes its leading
newline, handles generic conditional alternatives by selecting the last branch,
and only rewrites three nesting levels. Native `choose`, dynamic scopes, formatter
extensions and anonymous `{}` placeholders are not fully implemented. `resolve_template`
case-folds variables, converts plural values to integer and selects singular only
for integer one, converts values with Python `str`, and treats `energyPrefix` as
empty. It supplies neither native culture-specific formatting nor formatter semantics.
Energy/star icons become `E`/star terminal glyph conventions rather than preserved
native rich-text semantic tokens. `show_combat` passes absolute power Amount when
nonzero and omits substitution for zero; `show_event` suppresses the event page body.

An offline evaluation of the three unchanged helper functions (loaded by AST,
English `t` branch selected) demonstrates the limitations without running the game:

| Resource / inputs | Adapter helper result | Why it cannot be a native golden |
| --- | --- | --- |
| `cards:ARMAMENTS.description`, Block=5 | `Gain 5 Block.\nUpgrade a card in your Hand.` | The function has no upgrade-state input; upgraded Armaments uses the same “a card” choice despite the template containing an ALL-cards branch |
| `cards:BODY_SLAM.description`, CalculatedDamage=0 | `Deal damage equal to your Block.(Deals 0 damage)` | Combat text is selected even outside combat and the original contextual newline is removed |
| `card_selection:TO_DISCARD`, Amount=2 | `Choose {Amount:plural:a card\|{} cards} to Discard.` | Anonymous `{}` placeholder keeps the outer plural unresolved; populated variables alone are insufficient |

These are observed local helper outputs, not observations of native English.
They are diagnostic examples, not approval to preserve approximation in the final
simulator. First-victory wording may remain explicitly uncertified under the existing
acceptance decision; full completion still requires exact pinned English.

## Required native rendering cases

The following are specifications for future independent golden captures, not
executed/passing cases. Use the unpatched native rendering entry point, pinned
resources/formatter dependencies, current object state and explicit rendering
context; preserve both rich-text semantic tokens and plain text as appropriate.

| Case family | Concrete resource anchors / contexts | Evidence required |
| --- | --- | --- |
| Fixed labels and fallback | `cards:STRIKE_IRONCLAD.title`; monster segment-name fallback; intentionally missing table/key | Native output or explicit unsupported/error result; never silently accept identifier fallback as translated English |
| Numeric previews | `cards:STRIKE_IRONCLAD.description`, `DEFEND_IRONCLAD.description`, `BODY_SLAM.description`; base/current preview, target-specific mods, zero/negative values | Correct object/target preview scope, units, rounding and rich-text diff meaning; compare text independently of raw stats |
| Upgrade condition | `cards:ARMAMENTS.description`, `BEGONE.description`, upgraded/unupgraded and upgrade preview | Native conditional branch and upgrade-preview substitutions, including stateful enchantment/affliction changes |
| Combat context | `cards:BODY_SLAM.description`, `BARRAGE.description`; deck inspect/shop versus combat/targeting | Presence/absence of context text, line breaks, calculated values and plural hit count |
| Complex conditional/choose | `cards:SHIV.description`, `MAD_SCIENCE.description` and model-specific flags | Native enum/boolean selection, nested scope, absent optional rider, anonymous placeholders and deeper nesting |
| Selection and plural | `card_selection:TO_DISCARD`, `TO_REMOVE`, `TO_TRANSFORM`; Amount 0/1/2, zero/partial/full selection | Bound prompt variables, exact article/count/plural, confirm/cancel/skip labels and availability |
| Energy/star/percent | `cards:ADAPTIVE_STRIKE.description`, `BIG_BANG.description`, `powers:COLOSSUS_POWER.smartDescription` | Semantic resource icons, formatter arguments, percent conversion, singular/plural and line breaks; no replacement with internal formulas |
| Intent | `intents:ATTACK.description`, multi-hit and non-attack intents | Resolved damage and repeat, single-player conditional branch, native visible title/body order; no AI-internal identifiers |
| Powers/relics/potions/modifiers | `powers:COLOSSUS_POWER.smartDescription`, potion dynamic vars, relic counters and modifier-bound text | Actual model description property may select a smartDescription/context key; all current variables, nonzero and zero, signed meaning |
| Events/ancients/rest/shop/rewards | Native current page and offered option LocStrings; locked/disabled reason; exhausted/sold offers; replacement prompt | Current bound object strings and exact visible reasons, no fallback guesswork or unused dynamic vars; original option/page ordering |
| Feedback and results | `combat_messages`, `game_over_screen`, `run_history`, score lines | Values frozen at emission, ordered feedback, punctuation/plural/culture and native ending kind; terminal victory boolean is insufficient |
| Markup meaning | Gold/blue/purple keyword formatting, icons, links/tooltips, nested tags and literal brackets | Preserve meaning in typed text spans or audited equivalent; animation style such as sine can be excluded only under graphical-timing policy |

Observation/formatting must remain pure. Repeated render/preview work must preserve
authoritative state, pending prompt/boundary identities, clock, RNG and allocation
identity state. Clearing previews after inspection is not proof of restoration of
their prior values; fresh model construction/upgrades need purity evidence too.

## Evidence still missing

Resource provenance is now independently checkable against the extracted installed
PCK. Native rendered expectations for the matrix above remain absent from this
adapter inspection. Required tools must call the actual native description/prompt
rendering paths with real bindings, without the unconditional key/existence patches,
and capture returned text/spans alongside context and source identities. Keep those
fixtures separate from `oracle_display`/Python approximation fixtures. Record exact
formatter/DLL/resource identities and distinguish missing data, hidden values and
uncertified wording explicitly. No public JSON transport is required for that work.
