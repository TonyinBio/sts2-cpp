# Installed English resource provenance

The authoritative base-resource candidate for this audit is the installed
`SlayTheSpire2.pck`, with the exact DLL/PCK pair and release metadata recorded in
[resources.json](resources.json). The declared installation reports v0.111.0,
release commit `41cef1ea`; the DLL matches the existing matching contract.

| Input | SHA-256 |
| --- | --- |
| `sts2.dll` | `0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9` |
| `SlayTheSpire2.pck` | `c60f672ee7804e6aefa1e19a582fa1c80b126a7b0eef4d084d3abf110df2eab7` |
| `SmartFormat.dll` | `6220cde1dafffde5c51a4e23db4bb16e8ec620ef52bac51c0ad5f48c4769f168` |
| `SmartFormat.ZString.dll` | `723e9ffdb9cec9565d9bbbdb27b4d19a9b0ba4ff006e1ab8b94022ae5c426450` |

The manifest also hashes `sts2.deps.json`, `release_info.json` and the extractor.
This is an explicit association of co-installed artifacts, not a claim that hashing
the assembly alone identifies English content or proves a signed vendor pairing.
Future captures must retain the resource/formatter identities as well as game identity.

## Extraction and comparison performed

Used the existing pinned wrapper's `scripts/extract_localization.py`, without
modifying the wrapper or its tables, with output under ignored
`build/english-source-audit/extracted/`. The installed archive is unencrypted PCK
format 3 (engine header 4.5.1, flags 2). The extractor checks supported header/entry
flags, validates every selected entry against the archive's MD5 and parses its JSON
before writing any extracted table. Its code is indexed in `resources-sources.json`.

Extraction returned 92 tables across English and Simplified Chinese. All **46
English tables / 7,028 keys** match `sts2-cli/localization_eng` byte for byte at
wrapper commit `084d1aa3d8e118ca7ce8d8774ad16d6be9c92367`. No filename or key-set
difference was found. Per-table resource paths, byte lengths, key counts and SHA-256
values are in the manifest; raw native tables are not included in this artifact.
The companion adapter comparison independently records the same per-table result.

This resolves the contract's warning for **these exact resource bytes**. It does
not certify the wrapper's rendering, active native override configuration, or another
installation with a matching DLL and different PCK. The adapter report identifies
why identical dictionary bytes still produce non-native English output.

## Native loading and overrides

`LocManager` uses `res://localization`, maps `eng` to culture `en`, and loads the
English tables through its own loader. Source anchors are detailed in the formatting
report. User override tables and mod localization can alter the effective result;
loading the PCK is not proof that those layers are inactive. The capture preconditions
must establish English culture, no mods and no active localization override, and
record the actual effective table hashes or a checked equality to this base set.
`OverrideLanguageAsEnglish` alone must not be presumed to disable every mod path.
This audit extracted base resources; it did not launch the graphical game or inspect
its active user settings/override directories.

## Model-key inventory and limitations

The manifest performs a prefix lookup for every one of the 224 previously audited
victory roots. 221 have directly matching keys somewhere in the English table set.
The three Decimillipede segment variants have no own matching prefix; their shared
base class explicitly returns the `monsters` key `DECIMILLIPEDE_SEGMENT.name`
(`Models.Monsters/DecimillipedeSegment.cs:45–49`). Their missing direct prefixes
therefore do not establish missing English names. Record the actual model getter,
inherited behavior and renderer context rather than synthesizing a key from every ID.

Prefix counts establish resource discoverability only. They do not certify all
referenced keys, fixed dependencies, conditional page/description variants, generated
content, smart descriptions or emitted text. A runtime binding/lookup trace and
model-specific source consumers must establish that coverage. Excluded menu/profile
tables are counted because the archive contains them, without expanding simulator
scope. No table-count statistic is evidence that all its fields are publicly visible.

## Reproduce

```sh
PYTHONDONTWRITEBYTECODE=1 python3 docs/planning/english-rendering-audit/resources.py \
  --game-dir '/path/to/Slay the Spire 2'
```

The command rejects a different DLL/PCK/release/wrapper identity, extracts to the
ignored default location, compares tables, and regenerates `resources.json`. It
does not modify the installation or checked-in localization, run gameplay, apply
native formatter bindings or certify any rendered output. Use `--extracted` to
choose a different disposable local extraction directory.
