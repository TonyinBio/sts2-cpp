# Ironclad victory acceptance case and capture-gap inventory

Planning evidence for [Audit Ironclad victory acceptance cases and native capture gaps](https://github.com/TonyinBio/sts2-cpp/issues/13), under
[Map the complete headless STS2 simulator build](https://github.com/TonyinBio/sts2-cpp/issues/1).
The policy is the resolution of [Define parity acceptance beyond the recorded victory route](https://github.com/TonyinBio/sts2-cpp/issues/4).
This artifact identifies cases and prerequisite tools; it implements no simulator,
probe or new runtime corpus and claims no newly passing gameplay comparisons.

## Evidence already available

The preserved dense recording supplies 696 canonical actions and 697 boundaries,
including initial state and natural victory. Its historical independent oracle
self-replay and original-checkpoint agreement are recorded in
[the fixture](../../../tests/parity/fixtures/reference/run-victory-1234/README.md).
That result is retained, not rerun here. The fixture's last recorded C++ attempt
stopped unsupported at the first action; no new C++ coverage is inferred.

The semantic audit supplies 224 observed/offered roots and 32 additional supporting
records. Supporting records are records, not necessarily 32 distinct additional
definitions or an exhaustive recursive closure. The audit also names system edges,
such as SpoilsActMap and fixed pet/orb capabilities, that are not new root rows.
The [lifetime audit](../instance-lifetimes/README.md) specifies retained references
and retirement/restore regressions. The [visibility audit](../public-visibility-audit/README.md)
supplies independently source-grounded public-case specifications, not runtime goldens.

| Artifact | Purpose |
| --- | --- |
| [models.md](models.md), [models.json](models.json) | Every root linked to exact audit record, source hash, branch/variant/RNG/dependency requirements and first route evidence; supporting records retained separately |
| [public-cases.json](public-cases.json) | 10 combat, 16 prompt and eight hidden-state-pair families, with source lines and pending evidence status |
| [fields.json](fields.json) | All 30 top-level fields and 4,416 observed leaf-path/type shapes across the 697 states, including dynamic object keys; not a schema or comparator |
| [route-examples.json](route-examples.json) | Stored action/boundary anchors for offers, accepted purchase failure, consumed-source selections, sparse slots and victory |
| [cases.md](cases.md) | Concrete cross-system setups, comparisons and capture dependencies |
| [capture-gaps.md](capture-gaps.md) | Probe feasibility, public case applicability and work to sequence |
| [inputs.json](inputs.json) | SHA-256 identities of the preserved inputs; integrity does not prove interpretation |

## Comparison surfaces and graphical exclusions

Keep the existing exact privileged comparator unchanged for the dense route. Compare
all exported gameplay values, explicit clock results, RNG, IDs/links, native model
storage, ordering, pending choices and run flow. Observed null/empty values and
metadata remain checked; an apparent profile/UI name is not grounds to delete a
field. For example, `extra_fields.test_subject_kills` changes during the final boss.
Player unlock/discovery fields can affect generation or conditions. Do not replace
them with guessed constants or remove them under a UI exclusion.

No monster drawing, texture, pixel, animation-frame or layout-coordinate payload
was found in this export's structure. `/map_drawings` is the explicit annotation
candidate and is null at all 697 boundaries. Keeping that null check introduces no
graphics implementation requirement. No historical fixture or comparator is changed.
Future non-null annotation payloads need a declared comparison-surface revision;
this audit does not approve a wildcard ignore list. `/acts/*/saved_map` and visited
coordinates encode gameplay topology and remain required.

Native icons can convey gameplay: intent types, attack counts, enchantments,
afflictions, locked choices and revealed node types need semantic equivalents in
structured observations. Their sprite artwork, positioning, sounds, wiggles and
animation duration are excluded. Capture settled semantic output, not transient
frames. Animation-driven cosmetic RNG must not be introduced by a pure query.
This is not permission to alter gameplay RNG or assume all native Chaotic consumers
are irrelevant; their exact-English policy remains deferred.

Privileged equality does not establish a complete checkpoint or a public observation.
Missing native runtime members require the existing ownership/lifetime audit and
restore-and-continue evidence. Public expectations must originate in independent
native consumers. Stable C++ IDs are checked through object correspondences and
visibility histories, not literal equality to `card_N` recording labels.

## Per-model case construction

Use each root's exact record, not its mere presence in a snapshot. For cards and
attachments, retain the enumerated legal branches, upgrade differences, regression
scenarios, parameters and fixed dependencies. For relics/potions, exercise acquisition,
use, ordering, sparse capacity, counters and reset/persistence according to the
record. For powers/monsters/encounters/events, split the indexed predicates, authored
moves/options and parameter/ascension branches into concrete reachable cases.
Retain source paths and predicate line numbers when deriving each executable fixture.

The index deliberately preserves the original audit's generic instructions where
it has not supplied an executable setup. It is a traceable obligation inventory,
not a claim that every predicate has a reachable fixture or exhaustive interaction
coverage. Fixture authors must establish reachability within the observed-plus-fixed
single-player scope, document inapplicable branches and retain unresolved cases.
Do not require other-character run recordings to establish the first Ironclad route.
Fixed conditional outputs already agreed in scope still require their semantics;
that does not promote every unrelated character's content into the first milestone.

For each case, keep separate implementation, native reference and interaction status.
Attach a pinned setup, exact input sequence, compared surfaces, expected independent
native output, source anchors and result. Native branch capture must replay itself
before serving as reference. C++ regression expectations must not feed transitions.
Do not mark every model verified because it appears in a passing route or has a
nonempty audit record.

## Scope and handoff

[Choose implementation milestones and dependency order for complete coverage](https://github.com/TonyinBio/sts2-cpp/issues/8)
can now sequence the concrete capture capabilities and integrated engine slices.
No new broad audit ticket is needed for the same known gaps. Its decisions still
include exact public-to-recorded action mapping, probe placement, support-stop
semantics for outside-scope generated effects and dependency ordering. Those are
existing sequencing/interface questions, not choices silently settled here.

Other-character recordings, wider ascension/modifier certification and unrelated
Ironclad content remain deferred. [Exact-English presentation context](https://github.com/TonyinBio/sts2-cpp/issues/14)
is separately deferred past Ironclad parity. Native English goldens are still missing;
resource equality does not fill that gap. No gameplay/prose correctness exception,
public JSON requirement, contract change or new glossary meaning is introduced.

## Reproduce this static inventory

From the repository root:

```sh
python3 docs/planning/ironclad-acceptance/build_inventory.py
```

This parses preserved JSON/gzip and Markdown only. It checks 224-root linkage,
696-action/697-state alignment and 34 public-family extraction and regenerates the
inventory. Its ordinary JSON parser is not the exact runtime comparator. Source-file
hashes are inherited from the semantic audits; the resolution records the fresh
local hash/link checks performed for this artifact. No live game, native UI,
performance benchmark or C++ suite was run for this documentation-only task.
