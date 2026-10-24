# Pinned English formatting and dynamic descriptions

Source facts for **Establish authoritative pinned English sources and rendering
evidence**. This audit implements no renderer and captures no native runtime
outputs. The cases below are source-derived golden **specifications**, not passing
goldens. An extracted template, substituted template and player-visible rendered
description are distinct evidence surfaces.

## Provenance and reconstruction

Native sources are the existing ILSpy 9.1.0.7988 decompilation under
`build/combat-state-audit/decompiled/` (not regenerated here), tied to the pinned
game DLL SHA-256 `0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`
already independently checked in the preceding visibility audit.
`formatting-sources.json` hashes the inspected files/ranges. It includes local
dependency decompilations and installed metadata as local prerequisites, not assets
to publish. No game source or assembly is copied into the planning artifact.
Later citations abbreviate the `MegaCrit.Sts2.Core.` namespace prefix; the addon
directory is `MegaCrit.Sts2.addons.mega_text`. The manifest retains full paths.

Installed `sts2.deps.json` identifies SmartFormat **3.3.0**, assembly version
`3.0.0.0`, file version `3.3.0.704`. Installed binary hashes:

| Binary | SHA-256 |
| --- | --- |
| `SmartFormat.dll` | `6220cde1dafffde5c51a4e23db4bb16e8ec620ef52bac51c0ad5f48c4769f168` |
| `SmartFormat.ZString.dll` | `723e9ffdb9cec9565d9bbbdb27b4d19a9b0ba4ff006e1ab8b94022ae5c426450` |

The installed library's `PluralLocalizationFormatter`, `PluralRules`,
`ConditionalFormatter`, `ChooseFormatter` and `DefaultFormatter` were freshly
decompiled to `/tmp/sts2-english-formatting/`. Reconstruct each using:

```sh
DOTNET_ROLL_FORWARD=Major /tmp/sts2-audit-tools/ilspycmd \
  -t SmartFormat.Extensions.PluralLocalizationFormatter \
  '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2/data_sts2_windows_x86_64/SmartFormat.dll' \
  -o /tmp/sts2-english-formatting
```

Replace the type for the other four inspections (`SmartFormat.Utilities.PluralRules`
for the rules type). The roll-forward setting is needed because the local ILSpy
tool targets .NET 8 and installed runtime is .NET 9; this runs decompilation only.

## Loading, culture and variable binding

`MegaCrit.Sts2.Core.Localization/LocManager.cs` is authoritative for the loading
path: `LoadTablesFromPath` (409–481) enumerates **all `.json` files** in
`res://localization/<language>/` through `ListLocalizationFiles` (510–521), and
uses filename stems as table names. For non-English tables it loads English
fallbacks. English is `eng`, mapped to culture `en` (101–123, 213–223), not an
arbitrary machine locale. `completion.json` is language-completion metadata,
loaded separately (238–255), not a table of gameplay sentences.

Ordinary loading can merge nested Weblate overrides, then flat
`user://localization_override/<language>/<filename>` overrides, then mod tables.
Invalid override templates are skipped except `EXTENSION.*`, which bypass syntax
validation (535–554). `StartOverridingLanguageAsEnglish` (370–395) suppresses user
overrides on its fresh-load branch or reuses cached tables. **It is not proof of
unmodified base resources:** the unconditional mod merge remains in
`LoadTablesFromPath`, and the already-English fast path reuses `_tables`.
Certification must record actual loaded table bytes and reject unexpected overrides
or mods rather than infer purity from `Language == "eng"` or `OverridesActive`.

`SmartFormat` (258–281) obtains raw text and supplies current culture for a locally
present key, English culture for a fallback key. It catches SmartFormat
formatting/parsing errors outside `TestMode`, logs detailed variables, and returns
**raw template text**. In `TestMode` it rethrows. A returned string is consequently
not proof that substitution succeeded. Error logs contain variables and are
privileged diagnostics, not player feedback.

`LocString.cs` (72–90, 112–165) separates raw and formatted text. Adding a
`DynamicVar` keeps the object; adding another `LocString` formats it immediately
into a string. `AddObj` rejects null, replaces spaces in names with hyphens, and
overwrites a prior value under the same normalized key. `LocTable.cs` resolves
fallbacks and throws on a missing key with no fallback. Missing, empty and an
unresolved template must remain distinguishable in acceptance evidence.

## SmartFormat configuration and semantics

`LocManager.LoadLocFormatters` (229–235) creates its own `SmartFormatter`, registers
sources `ListFormatter`, `DictionarySource`, `ValueTupleSource`, `ReflectionSource`,
`DefaultSource`, then formatters in this order:

`ListFormatter`, `PluralLocalizationFormatter`, `ConditionalFormatter`,
`ChooseFormatter`, `SubStringFormatter`, `IsMatchFormatter`,
`LocaleNumberFormatter`, `DefaultFormatter`, `AbsoluteValueFormatter`,
`EnergyIconsFormatter`, `StarIconsFormatter`, `HighlightDifferencesFormatter`,
`HighlightDifferencesInverseFormatter`, `PercentMoreFormatter`,
`PercentLessFormatter`, `ShowIfUpgradedFormatter`.

This list and its source objects matter: object property selectors, nested children,
empty branches, formatter options and pipe separators cannot be handled faithfully
by replacing `{name}` tokens or splitting strings at every `|`.

| Path | Observed behavior; source under decompilation root unless noted |
| --- | --- |
| Number / `n` | `Localization.Formatters/LocaleNumberFormatter.cs`, entire type: autodetects listed primitive numeric types; default `N0`, explicit raw format otherwise; formats with **LocManager current culture**, not the provider passed for a fallback key. DynamicVar is not a listed primitive. |
| `diff` / `inverseDiff` | Corresponding formatter types require `DynamicVar` and call `ToHighlightedString`. `Localization.DynamicVars/DynamicVar.cs`, 171–180: cast preview and enchanted values to integers, compare those integers, use invariant numeric text; just-upgraded forces favorable highlight. `TextEffects/StsTextUtilities.cs` wraps nonzero comparison in green/red tags. This is not the locale-number `N0` path. |
| `percentMore` / `percentLess` | Corresponding types use DynamicVar **BaseValue**, otherwise decimal conversion; compute `(value-1)*100` / `(1-value)*100`, then `Convert.ToInt32` and invariant text. This conversion must not be replaced with the truncating cast used by `diff`. |
| `abs` | `AbsoluteValueFormatter`: decimal/double/float absolute values use LocManager culture; integral branches use parameterless `ToString`; unsupported objects return false. It does not automatically unwrap DynamicVar. |
| `energyIcons` | `EnergyIconsFormatter`: EnergyVar uses rounded integer `PreviewValue` and optional color prefix; CalculatedVar calculates with null target; primitive decimal casts to int; string takes count from formatter options. Counts 1–3 emit repeated icon images; other counts emit a number/highlight plus one icon. Missing/colorless prefix resolves the local character, then colorless fallback. See `Helpers/EnergyIconHelper.cs` for owner/pool-specific prefix. |
| `starIcons` | `StarIconsFormatter`: DynamicVar preview / decimal cast to int, then repeats star image count times. Zero gives empty string; unsupported type throws; negative values do not silently become a valid icon count. |
| `show` | `ShowIfUpgradedFormatter`: first branch is upgraded, optional second is normal; upgrade preview wraps first branch in green. More than two branches or wrong state errors. |
| English plural | Fresh dependency `PluralRules` maps `en` to `DualOneOther`: 2 branches mean one/other; 3 mean zero/one/other; 4 mean negative/zero/one/other. `PluralLocalizationFormatter` obtains decimal through `IConvertible` (so DynamicVar can use its conversion value), or counts an object sequence; splits nested format objects and formats only chosen child. Booleans/strings do not count as numeric plural values. |
| Conditional / `cond` | Fresh dependency `ConditionalFormatter`: bool chooses first for true, second for false; strings choose first for nonempty, second for empty; numeric positional choices use floor with clamping and negative maps to last, or explicit relational condition syntax. Numeric conditions are not generic boolean truthiness. It supports date/time branches too; those must not enter deterministic gameplay formatting without an explicit approved clock source. |
| `choose` | Fresh dependency `ChooseFormatter`: match supplied choices, otherwise optional final default; wrong branch counts or missing default for unmatched value error. Boolean/null matching and culture/case settings have their own paths. |
| Default | Fresh dependency `DefaultFormatter`: recursively formats nested children; otherwise uses provider custom formatter, `IFormattable`, literal string, or `ToString`. A DynamicVar's base/conversion/string paths need not equal its preview path. |

Built-in list, substring and regex extensions are confirmed registered; their full
implementation semantics were not audited here. Exact acceptance for templates
using those paths needs pinned-library captures or a further source audit. This
report does not claim that five inspected dependency types cover every template.

Unlabelled pipes also depend on formatter dispatch: the registered plural formatter
precedes conditional for numeric objects. A real bool, a numeric `BoolVar`, a
DynamicVar with distinct base/preview values, and a primitive decimal must retain
their input types in evidence; flattening them to strings or integers can choose
different branches. `DynamicVar.GetBaseValueForIConvertible` returns BaseValue,
while CalculatedVar overrides it with its null-target calculation. Neither is an
alias for the `diff` preview value.

## Native dynamic card-description pipeline

The native display path is `Nodes.Cards/NCard.cs:860–906`, `UpdateVisuals`:
choose display pile and preview target (`_previewTarget ?? CurrentTarget`), reset
preview variables, calculate card/enchantment previews unless forced unpowered,
then choose normal or upgrade description, finally wrap visible text in center
markup. Not-seen/locked cards show replacement resource strings. Calling a model's
description getter alone is not this pipeline, and its scratch preview writes are
not an implementation of the pure C++ query contract.

`Models/CardModel.cs:1372–1443`, `GetDescriptionForPile`, adds DynamicVars,
model-specific extra arguments and contextual inputs: `IfUpgraded`, `OnTable`
(hand/play display), `InCombat`, `IsTargeting`, `TargetType`, `GainsBlock`,
`IsOstyAlive`, energy prefix and star icon. It formats the base description, then
appends purple enchantment text and purple affliction text, inserts applicable
before-description keyword strings at **index zero for each loop iteration**,
appends enchanted replay text, appends after-description keyword text, and joins
nonempty lines with newline. Therefore multiple applicable leading keywords appear
in reverse traversal order, despite the array's ordering comment. Capture that
native behavior rather than rewriting it from the comment.

`Entities.Cards/CardKeywordOrder.cs` defines before traversal as Ethereal, Sly,
Retain, Innate, Unplayable; after traversal as Exhaust, Eternal.
`CardKeywordExtensions.GetCardText` uses gold title plus raw `card_keywords/PERIOD`.
Sly/Retain use current-turn predicates rather than mere keyword membership.

`CardModel.UpdateDynamicVarPreview` (1452–1480) runs global hooks only for a combat
card in hand/play or a combat upgrade preview. DamageVar/BlockVar apply enchantment
math and optional gameplay preview hooks; `EnchantedValue` gives the comparison
baseline, while `PreviewValue` is displayed. `CalculatedVar.Calculate` uses its
runtime multiplier only in active combat with a combat-bound card. Thus a preview
depends on pile, mode, target, upgrade and enchantment context, not just definition
ID and a numeric dictionary. `CardPreviewMode.MultiCreatureTargeting` documents
the common-effect preview distinction for all/random enemy targets.

Relic and potion `DynamicDescription` likewise bind variables and model-specific
energy icons (`RelicModel.cs:91–110`, `PotionModel.cs:48–65`). Native power hover
text first checks visibility, then selects smart/remote/default description and
binds its amount, ownership, targets and DynamicVars (`PowerModel.cs:350–402`).
Raw canonical description text is insufficient for these model families too.

## Markup semantics and safety

`addons.mega_text/MegaRichTextLabel.cs:184–200, 328–370, 388–396` uses Godot rich
text and installs named effects. Green/red convey preview improvement/reduction;
gold marks keyword text; purple wraps extra card text. Icon image markup represents
energy/stars, including count and character color meaning. `RichTextGold` preserves
already-green glyphs; nested semantic effects must not be flattened without checking
their interaction. Animation effects can be omitted under the contract's animation
exclusion, while their textual content remains.

`MegaLabelHelper.ParseBbcode` (59–162) recognizes `[lb]`/`[rb]` as literal brackets
and validates nested end-tag order for text-size calculation. This is a sizing
helper, **not a complete replacement for Godot's rendered markup semantics**.
Deleting all bracketed substrings loses icon resources, meaningful emphasis and
literal brackets, and cannot certify public text equivalence.

Public formatting must receive only the intended visible object/context. Registered
reflection sources and model-specific extra arguments are not authorization to
expose model internals. Formatting-error variable dumps, fallback resource keys,
hidden targets and unoffered previews remain privileged. Native
`LocString.GetRandomWithPrefix` (186–194) defaults to `Rng.Chaotic`: feedback chosen
at emission must be retained, not randomly reformatted by a pure observation call.

## Source-derived golden specifications — capture pending

| Case | Inputs and required independent capture |
| --- | --- |
| load-identity | Capture language `eng`, effective culture `en`, table/resource hashes, loaded mods/user overrides and fallback status. Contrast an overridden table and a clean base load; English flag alone cannot certify source identity. |
| primitive-vs-diff | Primitive 0/1/1000, fractional and negative numeric substitutions versus DynamicVar with same values under diff/default/n. Include rounding midpoint, grouping, preview-versus-base and integer cast differences. |
| english-plural | 2/3/4-form templates at -1/0/1/2/1.5, plus nested empty branches. Test actual resource keys where present; synthetic library probes certify only the formatter primitive. |
| conditional-choose | True/false, empty/nonempty string, numeric zero/one/negative/fractional, unknown choice with/without default and nested branches. Preserve resource-level punctuation/newlines. |
| upgrade-and-diff | Base/enchanted/preview values equal, higher, lower, and fractional values crossing an integer; normal/upgraded/upgrade-preview `show` branches. Include inverseDiff and just-upgraded override. |
| percent | Positive/negative multipliers, dynamic BaseValue different from PreviewValue, fractional percentages near midpoint. Capture .NET conversion behavior rather than assume truncation. |
| energy-star | 0/1/2/3/4 icons; EnergyVar, CalculatedVar, primitive decimal and explicit string-prefix branches; five character energy colors and ownerless pool behavior. Preserve zero-energy numeric+icon versus zero stars empty output. |
| card-context | Same visible card in deck, hand, draw, play and upgrade preview; Body Slam's conditional combat calculation; selected target and multi-target mode; no-target inspection. Include extra args and keyword/enchant/affliction line ordering. |
| description-order | Multiple leading keywords, both extra-text modifiers, replay and trailing keywords: capture final newline and order, including reverse index-zero insertion. |
| markup | Literal bracket escapes, nested green/gold, purple extra text, repeated image icons and resource newlines; compare semantic output plus formatted rich text. Do not substitute regex stripping for expected output. |
| failure-distinction | Absent table/key, present empty value, missing variable, malformed template, TestMode versus ordinary fallback. Failure/raw template is not a certified English observation. Keep detailed error variables private. |
| pure-repeat | Repeated text/previews do not alter authoritative state, RNG, prompt or IDs; maintain emission-time feedback values. Native preview scratch mutation and randomized feedback selection are setup facts, not a C++ purity waiver. |

Each public golden needs a visible native path, exact effective resources,
structured visible preconditions and captured formatted/semantic output. The local
adapter's `RunSimulator.cs:3525–3531` sets **InvariantCulture**, unlike native `en`,
and has permissive missing-key/initialization fallbacks later in that method.
Its source or output alone cannot establish native English certification. No such
captures were produced here, and none of the proposed cases should be marked passed.
