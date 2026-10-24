# Victory model semantic dependency audit

Resolution evidence for [Audit semantic dependencies of every model observed in victory run](https://github.com/TonyinBio/sts2-cpp/issues/9),
within [Map the complete headless STS2 simulator build](https://github.com/TonyinBio/sts2-cpp/issues/1).
This audit supplies planning facts. It implements no content and certifies no new
runtime behavior. The separately preserved dense recording establishes only its
declared route and comparison surface.

## Coverage and source identity

All 224 roots in the confirmed `../victory-mechanics/scope-roots.json` are covered:
71 cards, 31 relics, 15 potions, 39 powers, 30 monsters, 22 encounters, 11 events,
three enchantments and two afflictions. Owned models, runtime models and actual
offers are included; unused setup queues and unused pool declarations do not add
roots. Root upgrades, authored parameter variants, conditional hooks and unchosen
options are included rather than assuming the recorded action exercised them.

| Artifact | Contents |
| --- | --- |
| [cards.md](cards.md), [cards.json](cards.json) | Every card/attachment root; costs, effects, upgrade changes, branches, state, fixed dependencies, random generation and regression cases; additional required powers |
| [inventory.md](inventory.md), [inventory.json](inventory.json) | Every relic/potion root; acquisition/use/trigger order, counters, conditional outputs, pool boundaries and supporting definitions |
| [encounters.md](encounters.md), [encounters.json](encounters.json) | Every power/monster/encounter/event root; authored effects, move/branch/RNG/ascension indexes, mutable storage, alternate options and fixed/finite dependencies |
| [shared-capabilities.md](shared-capabilities.md) | Listener enumeration, play/potion execution, selection, mutations, generation policies, AI, death, map and cross-system ordering requirements |
| [source-index.json](source-index.json) | Source identities, root coverage and artifact hashes; an integrity index, not proof the interpretation is correct |

The installed assembly was independently rehashed on 2026-10-24 and matched the
contract's `0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.
Sources are the existing local ILSpy 9.1.0.7988 decompilation described in
`docs/specs/combat-runtime-audit.md`; source paths and line numbers refer to those
hashed local files. A fresh single-type decompilation of Hook matched the existing
Hook source byte for byte. That is a provenance spot check, not a fresh decompilation
of every indexed file. No game assembly or full decompiled source is published.
Reproduction requires the pinned local installation; the published metadata cannot
replace access to the referenced source definitions.

## Required implementation expansion

The root set is not closed under its own executable semantics. Required additions
include these definitions/capabilities; their individual source records hold details:

- EntropyPower, JugglingPower, ManglePower, SetupStrikePower, OneTwoPunchPower,
  FreeAttackPower, StampedePower, TheGambitPower, ViciousPower, CuriousPower,
  ImprovementPower, StranglePower and RetainHandPower, with the shared
  TemporaryStrengthPower behavior and existing root power dependencies.
- Momentum from Punch Dagger and Nimble/Swift from Self Help Book; the existing
  Glam/PerfectFit/Sharp and Ringing/Tainted attachment semantics remain required.
- Dazed from Eye With Teeth, Wound from Painful Stabs and Normality from the
  unchosen holes-event option. Include unplayability, Ethereal, turn hooks and
  non-upgradeability as defined, not just creation of their IDs.
- Archaic Tooth's fixed character-dependent transformation outputs, described above;
  these conditional outputs are not random-pool candidates. Its unmapped-card Doubt
  helper fallback is documented separately from normally offered legal branches.
- BowlbugNectar, the finite authored alternative in the entered Bowlbugs encounter,
  and Architect/TheArchitectEventEncounter for the Architect event. Full encounter
  support includes its authored finite composition alternatives. Decimillipede
  subclasses share their required base behavior; they are not independent shortcuts.
- SpoilsActMap topology, quest lifecycle, and the forced Vakuu selector used by
  Whispering Earring. Conditional pet/dealer, star-resource and orb-evoke capabilities
  arise from fixed transformation outputs even though the recorded run is Ironclad.

Identity checks, tooltip references and presentation special cases do not create
unconditional effect dependencies. Examples include Archaic Tooth's starter lookup
keys, references to Paper Phrog/Krane or Cruelty/Debilitate in compatibility checks,
and Whispering Earring's Imbued ordering explanation. Preserve the predicate or
ordering behavior when relevant; do not promote every referenced type into a new
implementation root. The per-model JSON distinguishes authored dependencies from
scan-only reference candidates; no regex reference list is a semantic closure proof.

## Random outcomes and support boundaries

Preserve full native definitions, pool order, unlock/single-player constraints,
eligibility, duplicates, upgrades, IDs and RNG consumption. Relevant broad generators
include Skill/Colorless Potion, Jack of All Trades, Splash, Mad Science Chaos,
Entropy transformation, Vexing Puzzlebox, Alchemical Coffer, Sea Glass, merchant and
reward creation, Relic Trader, and ancient/event offer pools.

Those generators do not require every possible additional outcome's effect to become
part of the first milestone merely through pool membership. Additional effects outside
the observed-plus-fixed scope can remain explicitly unsupported under the earlier
scope decision. Their identity/generation must still be correct. Do not filter,
reroll, substitute, silently skip passive effects or claim a successful supported
branch that would execute an unimplemented model. Ownership is not proof of passive
support, and correct display is not effect implementation.

Finite authored model branches are different: all nine Tinker Time type/rider
combinations, encounter composition alternatives and fixed transformation mappings
belong to the audited model's own semantics. An unobserved branch is not grounds
for omitting it. Transient previews and defensive fallbacks are marked separately;
arbitrary invalid persisted combinations are not legal native variants.

## Acceptance and sequencing handoff

[Define parity acceptance beyond the recorded victory route](https://github.com/TonyinBio/sts2-cpp/issues/4)
must turn the source-backed scenarios into explicit evidence gates:

1. Per-root base/upgrade/parameter and alternate-choice cases, including observed
   offers never selected. Prioritize thresholds, empty/one/many candidate paths,
   counter reset/persistence and lethal/nonlethal behavior.
2. Cross-model cases for ordered hooks, repeat/autoplay, generation, consumption,
   power stacking/prevention, damage caps, exhaust/hand capacity, revival and terminal
   processing; compare complete semantic state and future behavior, not just HP.
3. Checkpoint/clone/resume cases for suspended operations, captured targets/resources,
   source removal and nested selections, coordinated with the separate lifetime audit.
4. Seeded native generation vectors across pool/filter/upgrade branches, retaining
   unsupported candidates. Define precisely where unsupported effects stop a run,
   what state can be published at failure, and which nonexecuting operations on
   those models are supported. Do not treat them as ordinary illegal player input
   unless the native input was actually illegal.
5. Independent visibility/prompt and exact-English evidence from their existing
   audits. Neither these source summaries nor privileged state snapshots certify
   player-visible field/reveal rules or final English rendering.

[Choose implementation milestones and dependency order for complete coverage](https://github.com/TonyinBio/sts2-cpp/issues/8)
must include these fixed dependencies and shared capabilities in integrated slices,
each with state ownership, transitions, codec/clone validation and evidence. This
audit does not prescribe a foundation rewrite or split the implementation into
tickets. Full-target content reachability, additional random outcome implementations
and representative certification corpus remain subsequent planning concerns.

No fresh oracle branch recordings or C++ tests were run for this static audit.
Source identity/root/citation integrity was checked; that does not establish exhaustive
interaction coverage or prove every conditional is reachable. Model-local private
storage candidates are recorded, while complete retention/lifetime classification
belongs to its existing task. No contract is weakened, correctness exception approved,
or ADR contradicted; no new glossary term is needed.

## Recheck artifact integrity

```sh
python3 docs/planning/victory-semantics/verify.py \
  --game-dll '/path/to/pinned/sts2.dll'
```

This checks exact root coverage, recorded source hashes and indexed citation bounds
against the local decompilation, then regenerates the source index. It is deliberately
not a gameplay test or a claim of semantic completeness.
