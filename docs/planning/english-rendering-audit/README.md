# Pinned English sources and rendering evidence

Resolution evidence for
[Establish authoritative pinned English sources and rendering evidence](https://github.com/TonyinBio/sts2-cpp/issues/12),
within [Map the complete headless STS2 simulator build](https://github.com/TonyinBio/sts2-cpp/issues/1).
This is a planning audit. It establishes installed base-resource provenance and
native rendering constraints; it implements no renderer, binding or public transport.
It does not certify exact English output or add runtime public-interface coverage.

## Evidence supplied

| Artifact | Evidence surface |
| --- | --- |
| [resources.md](resources.md), [resources.json](resources.json), [resources.py](resources.py) | Reproducible extraction, explicit installed DLL/PCK/formatter identity, all English table hashes/counts, wrapper comparison and prefix inventory for the 224 victory roots |
| [formatting.md](formatting.md) | Native loading/overrides/culture, variable binding, formatter semantics, dynamic description contexts and meaningful markup; source-derived capture cases |
| [prompts-feedback.md](prompts-feedback.md) | Prompt/reason/feedback consumers, emission-time versus live values, merchant dialogue and terminal context; source-derived capture cases |
| [adapter.md](adapter.md), [adapter-resource-comparison.json](adapter-resource-comparison.json) | Wrapper table comparison, native lookup patches and non-native approximation paths; offline adapter diagnostic examples, explicitly separated from native goldens |
| `*-sources.json`, [source-index.json](source-index.json), [verify.py](verify.py) | Source identity and citation-bound integrity; not semantic or runtime certification |

Native game sources are the existing local ILSpy 9.1.0.7988 decompilation under
ignored `build/combat-state-audit/decompiled/`. The installed assembly was rehashed
and matched the existing pinned v0.111.0 contract. Five SmartFormat types were
freshly decompiled from the installed dependency; reconstruction commands and hashes
are in the formatting report. No assembly, full decompiled source or raw native
localization table is published in this artifact.

## Findings

The installed PCK SHA-256 is
`c60f672ee7804e6aefa1e19a582fa1c80b126a7b0eef4d084d3abf110df2eab7`.
Extraction validated the selected PCK entries and produced 92 English/Chinese tables.
All **46 English tables / 7,028 keys match the pinned submodule byte for byte**.
The manifest binds the PCK, matching game DLL, release metadata, formatter libraries
and extractor version explicitly. The prior warning about submodule provenance is
resolved for these bytes; another DLL/PCK pairing requires its own check.

Resource bytes and rendered output are separate. Native `eng` uses culture `en`,
specific SmartFormat 3.3.0 source/formatter registrations, typed dynamic variables,
model-specific context and card-preview composition. Upgrade state, pile, target,
enchantment/affliction, current combat and keyword order can all affect the text.
Numeric paths differ in rounding, grouping, conversion and base-versus-preview use.
Names may be inherited or override model-ID-derived keys: three segment models use
one shared native name key rather than their own prefixes. Prefix coverage is not a
proof of complete lookup or rendered-context coverage.

The adapter cannot establish native exactness despite matching dictionary bytes.
Its native initialization includes unconditional lookup/existence patches and culture
differences; its public display often returns raw templates and numeric dictionaries.
Python helpers select guessed conditional branches, truncate nesting and strip
meaningful markup. Offline helper outputs in the adapter report demonstrate those
limitations only; none is labeled a native golden.

Native formatting also has fallback/error paths. Outside TestMode a formatting error
can return raw template text after privileged logging. Therefore a string result,
the absence of a process crash or a key's existence is insufficient certification.
Capture lookup/fallback/error status separately from the returned text. Do not expose
logged variable dictionaries or internal object state as public feedback.

Some feedback is formatted and stored at emission, while rest hover/other descriptions
are formatted from current values. Reformatting historical feedback from current
objects can change its meaning. Random merchant and loss dialogue and profile-based
Ancient/Architect text are not determined solely by the gameplay seed. Their selected
presentation and visible stage must be captured; repeated observation must not reroll
them or expose future lines. This factual finding does not select a new simulator RNG
policy or bring profiles/platform services into scope.

## Correction to the preceding visibility evidence

The prior slot-local shop audit observed wiggle/sound and did not trace the separate
inventory subscription. `NMerchantInventory` also subscribes purchase failure to
merchant dialogue; `MerchantDialogueSet` supplies localized status-dependent pools,
and the native dialogue consumer chooses/formats a line. Native shop failure can
therefore include English text as well as animation/sound. The preceding visibility
report is clarified and linked to the complete path in this audit. Internal purchase
status names still must not be substituted for the actual selected visible dialogue.

## Independent rendering evidence still required

The companion case matrices specify concrete native captures for names, dynamic and
upgrade descriptions, base/preview rounding, plural/conditional branches, resource
icons, meaningful markup, nested prompts, disabled options/reason priority, event and
rest text, merchant feedback, emission-time numbers and terminal fields. **Native
rendered goldens remain uncaptured and every proposed runtime case remains pending.**
No live gameplay oracle or C++ test was run in this audit.

Each future golden needs:

- Actual native visible consumer and stage, language/culture, effective table hashes,
  dependency identities and verified absence of unexpected overrides/mod localization.
- Structured visible preconditions, exact selected key/context and immutable bound
  values; privileged setup/checkpoints remain separate from expected public text.
- Native formatted output and an audited semantic representation of markup/icons,
  including line breaks and meaningful emphasis. Scene/base-control captions require
  their own resource capture; model methods alone do not establish those labels.
- Emission order and retained output for feedback, plus declared random presentation
  and profile-context preconditions. A captured fixed line is not universally exact
  behavior and is not an approved correctness exception.
- Before/after complete-state and future-execution checks for formatting/preview
  purity, including RNG, identity allocation, boundary/prompt state and pending work.

The missing probe must call unpatched native description/prompt/feedback consumers
with their actual context and capture outputs before downstream mutation. Existing
`oracle_display`, a raw table loader, atomic TestMode selectors and filtered dense
snapshots do not provide this evidence. A probe's evidence encoding is independent
of a product API; this work requires no public JSON transport.

Registered list/substring/regex formatter semantics, additional custom controls and
new model-specific descriptions need further source/capture coverage when supported.
The source reports declare their inspected families rather than claiming every
template or every full-target model is certified.

## Planning handoff

[Define parity acceptance beyond the recorded victory route](https://github.com/TonyinBio/sts2-cpp/issues/4)
owns the remaining evidence decision: source-backed versus runtime-certified claims,
required rendering corpus, exact comparison surface, and presentation/profile-context
policy where current exactness and excluded services intersect. If that decision
needs a contract exception, the user must approve it explicitly; this task grants none.
These questions refine that existing ticket rather than creating a duplicate decision.

[Choose implementation milestones and dependency order for complete coverage](https://github.com/TonyinBio/sts2-cpp/issues/8)
owns placement of native capture tools, per-content rendering coverage and the later
English gate. Existing decisions still require safe on-demand text at the first C++
milestone with explicit uncertified wording, exact English for full completion, and
equivalent requested output in performance measurements. Rendering stays outside
kernel transitions; hidden-information protection remains unconditional.

No new domain term or ADR is needed. No acceptance contract, correctness exception,
binding scope, public transport or renderer implementation is changed.

## Reproduce checks

```sh
PYTHONDONTWRITEBYTECODE=1 python3 docs/planning/english-rendering-audit/resources.py \
  --game-dir '/path/to/Slay the Spire 2'
python3 docs/planning/english-rendering-audit/verify.py
```

The first command validates/extracts the exact local resource pair. The second checks
indexed source hashes/ranges and extracted table comparisons and writes the integrity
index. Reconstruct local dependency decompilations as described in the formatting
report when needed. These checks certify artifact identity, not rendered output.
