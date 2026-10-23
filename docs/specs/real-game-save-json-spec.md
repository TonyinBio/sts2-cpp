# Native Run JSON Schema

This document describes the JSON files the game uses to save a run. The schema checks
file structure and value types; passing it does not prove that a save can be played or
fully restored.

See the [shared terms](README.md#terms-used-in-these-sheets) for definitions of parity, snapshot, boundary, and model ID.

## On this page

- [Scope](#scope)
- [Native data format](#native-data-format)
- [Required, optional, and missing values](#required-optional-and-missing-values)
- [Validation](#validation)
- [Tests for saving and loading without data loss](#tests-for-saving-and-loading-without-data-loss)
- [Reference build](#reference-build)

| Attribute | Value |
| --- | --- |
| Schema | [serializable-run-v20.schema.json](../../data/schemas/serializable-run-v20.schema.json) |
| Native type | `MegaCrit.Sts2.Core.Saves.SerializableRun` |
| Native save version | 20 |
| Inspected game | v0.111.0, release commit `41cef1ea` |
| JSON Schema dialect | Draft 2020-12 |

## Scope

This schema describes the game's native JSON run-save representation. It requires no
wrapper, model dictionary, packed RNG conversion, or simulator-specific fields. A file
matching it has the native `SerializableRun` layout consumed by sts2-cli's `load_save`.

The definition is derived from the installed game's `JsonSerializationUtility.Options`
serialization contracts, including its generated serializer contexts and custom
converters. It contains 55 reusable definitions covering players, saved cards,
enchantments, relics, potions, modifiers, maps, room state, rewards, history, unlocks,
generation odds, saved properties, and RNG. Native history and room structures have
explicit definitions rather than unrestricted object placeholders.

This is a version-pinned structural description, not an official Mega Crit schema or a
guarantee that every structurally valid document can launch a playable run. The game
must still resolve models, validate state relationships, and initialize the run.

## Native data format

- The root uses `schema_version: 20` and `players: [...]`.
- Model IDs are native strings such as `CARD.BASH`, not numeric indexes.
- RNG uses `rng: {seed, rngs: {stream_name: {...}}}`. Run stream names and player
  stream names retain their native snake-case spelling.
- Every present RNG stream has `counter`, `s0`, `s1`, `s2`, and `s3`. The counter is
  a signed int32; the words are numeric uint64 values. Player RNG seeds and player
  network IDs are also numeric uint64 values. They are not decimal strings or Base64.
- Saved properties use native typed arrays of `{name, value}` objects. Nested saved
  cards and all seven property buckets are represented.
- `map_drawings` is null or a Base64 string containing a gzip-compressed native
  packet. It is not a JSON drawing object. Payload decompression and packet validity
  are outside JSON Schema validation.
- Event-history `variables` contain native `SerializableDynamicVar` records. Their
  `type` values are case-sensitive: `None`, `BaseDynamic`, `DynamicString`, `Decimal`,
  `String`, and `Bool`. Their separate native serializer retains this casing even
  though ordinary run enums use snake case.
- Original key casing is preserved, including `dailyTime` and history `TextKey`.

Parse and write uint64 values with exact integers. JavaScript `Number` can lose RNG bits
and must not be used as an intermediate representation.

## Required, optional, and missing values

Required fields reflect unconditional fields written by the native serializer.
Conditionally omitted fields remain optional: empty collections, default-valued card
upgrade levels, nullable acquisition floors, absent enchantments/properties, and other
fields governed by the native serialization conditions.

Nullable fields remain nullable where declared by native serialization metadata. This
allows native explicit nulls while still accepting the usual omitted forms. Computed
fields marked `JsonIgnore` are excluded. Unknown fields are rejected so this schema
describes the inspected format rather than silently accepting typos or unrelated
extensions.

The native deserializer can be more permissive than this schema: it may supply CLR
defaults for absent fields or ignore unknown fields. Those permissive parser behaviors
are not a declaration that the result is a complete native checkpoint.

### Numeric limits and random streams

Numeric bounds follow the underlying native integer types. The schema intentionally does
not replace them with guessed gameplay bounds for HP, ascension, counters, or collection
sizes. The native class also represents multiplayer saves, so `players` is not
artificially limited to one entry; sts2-cli's loading behavior is separate.

Stream dictionaries enumerate the known native stream keys but do not require every key:
they are native dictionaries and can serialize subsets. Complete checkpoint validation
must separately verify the stream set required by the target build. The schema does not
fabricate missing streams or enforce RNG algorithm invariants.

### Checks outside the schema

Other checks requiring the game or a semantic validator include model existence and
categories, model-specific saved-property names, consistent player references, map/act
relationships, exact decimal precision, and numeric upper bounds on uint64 values
encoded as dictionary property names. JSON Schema `format` and `contentEncoding`
annotations alone do not validate all native custom encodings.

## Validation

Use a Draft 2020-12 validator with integer-safe JSON parsing. For example, with the
Python `jsonschema` package available, run this from the repository root after replacing
`path/to/current_run.save`:

```python
import json
from pathlib import Path
from jsonschema import Draft202012Validator, FormatChecker

schema = json.loads(
    Path("data/schemas/serializable-run-v20.schema.json").read_text()
)
save = json.loads(Path("path/to/current_run.save").read_text())
Draft202012Validator.check_schema(schema)
Draft202012Validator(schema, format_checker=FormatChecker()).validate(save)
```

The schema passed a Draft 2020-12 meta-schema check and validated a generated native
checkpoint, a reconstructed native checkpoint, and a richer document serialized and
deserialized by the game's own `SaveManager`. The richer sample covers every saved
property bucket, nested cards, enchantments, history choices, tagged event variables,
room rewards, daily metadata, uint64 limits, signed counter limits, and an actual native
compressed drawing string. These checks verify the structure of saved data, not claims that
synthetic model/property combinations form a playable run.

Four valid-input checks and 25 invalid-input checks passed, covering native omission
rules and rejection of wrong versions, compact-format fields, malformed RNG data,
integer overflow, invalid enums, incorrect saved-property shapes, and malformed
history-variable records.

The existing C++ parity validator implements a smaller schema subset and does not
automatically consume this native schema. Existing parity profiles are unchanged.

## Tests for saving and loading without data loss

The offline suite checks exact comparisons, the checked-in native-serialized fixture,
and native schema constraints. It uses Python integers and `Decimal`, so comparisons do
not round uint64 words or precise decimals through floating point. Object member order
is ignored; array order, missing fields, nulls, booleans, and all metadata remain
significant. There is no general field-ignore or default-normalization list.

```sh
python3 -m pip install -r tools/oracle/requirements-test.txt
python3 tests/oracle/test_native_run.py -v
```

The tests are also registered under the CTest `native_run` label. Select the Python
interpreter with the test dependencies when configuring:

```sh
cmake --preset debug -DPython3_EXECUTABLE="$(command -v python3)"
ctest --preset debug -L native_run --output-on-failure
```

Without `jsonschema`, the seven comparison tests still run and the six schema tests are
explicitly skipped. The live suite requires the schema dependency and never substitutes
a mock serializer:

```sh
python3 tools/oracle/check_native_run.py \
  --game-dir '/path/to/Slay the Spire 2'
```

It builds a disposable copy of the pinned CLI with an injected C# probe, leaving the
submodule and game files unchanged. The probe reads the native serialized members
directly before and after `SaveManager.ToJson` / `SaveManager.FromJson`. This catches
values lost during the first serialization, which serializing the resulting JSON twice
could miss. Compressed drawing packets are also compared in full after gzip
decompression; native drawing member data is checked separately.

### Cases that must preserve data

Eight preservation cases cover typed properties, nested cards/history, exact decimal
values, all 15 RNG streams, both signed counter limits, nonempty drawing data, native
default insertion, object ordering, and a generated checkpoint. A separate deliberate
one-bit RNG mutation must produce exactly one difference. The synthetic capture is also
compared with the checked-in golden fixture. No timestamps or counters are removed from
these comparisons.

### Known data losses in the game serializer

Two additional tests record observed native losses rather than silently accepting them
as equivalent data:

| Native input member | After native serialization/deserialization |
| --- | --- |
| `saved_map.points[].can_modify = false` | `true`: the writer omits false, while the constructor defaults to true |
| `rooms.elite_encounter_ids = []` | `null`: the writer omits the empty list, while the constructor leaves it null |

The live suite explicitly expects and reports these two changes as `known_native_loss`.
They are not passing preservation cases or blanket normalization exceptions. A changed
loss signature fails the regression check. This documents the inspected native behavior;
it does not repair the game serializer. Drawing coordinates are tested on the native
packet quantization grid, since that binary format cannot preserve arbitrary precision.

The report and process/build logs are written under ignored `build/native-run-check/`.
The checked-in fixture is synthetic serialization data, not a playable run or a personal
Steam save. Saving and loading these samples does not prove that a complete running game can be restored.

## Reference build

Original `sts2.dll` SHA-256:
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.

The pinned CLI revision is `084d1aa3d8e118ca7ce8d8774ad16d6be9c92367`; its [load/save
implementation](https://github.com/wuhao21/sts2-cli/blob/084d1aa3d8e118ca7ce8d8774ad16d6be9c92367/src/Sts2Headless/RunSimulator.cs)
delegates native serialization to the installed game assembly. A new game build or save
version requires rechecking its serialization contracts and custom converters.
