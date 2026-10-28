# Ordered combat card choices

Scope: [#18](https://github.com/TonyinBio/sts2-cpp/issues/18), pinned build
`0.111.0+41cef1ea`. This sheet separates implemented behavior from native evidence.
The complete evidence ledger is maintained below; a source audit or atomic model
comparison does not establish native scene initialization or partial callbacks.

## Public gameplay

`apply_combat` / `apply_action` accepts `select_card`, `deselect_card` and
`confirm_selection` with the current boundary and typed `PromptId`. `observe`
publishes the current candidates, ordered selected cards, minimum/maximum count,
automatic submission policy and confirmation availability. `ActionSpaceView` and
the run-level `get_actions` enumerate these inputs. Invalid inputs and inspection
leave gameplay, selection, RNG and clocks unchanged.

Selection state belongs to byte-copyable `CombatData`: its prompt identity, candidate references,
ordered selections and typed effect accompany the suspended source-card frame.
Cloning and binary checkpoints preserve these values and the next prompt identity.
Candidate and selection lists use pointer-free inline sequences with independently
owned overflow; the common partial-prompt clone test verifies allocation-free
cloning and relocation by `memcpy`. Binary checkpoint revision 7 records these
operands and rejects older incompatible layouts.
The source card remains in Play, and its energy cost and play hooks run once.
Confirmation resumes the source frame without replaying its completed instructions.

| Card | Suspended effect and constraints |
| --- | --- |
| Purity / upgraded | Retain; select zero through three/five hand cards, then confirm. Exhaust them in selection order; finish and exhaust Purity. |
| Armaments | Gain five Block, then select an eligible unupgraded hand card and confirm. Zero/one eligible candidates resolve automatically. Only the combat copy upgrades. |
| Armaments upgraded | Gain five Block and upgrade all eligible hand cards automatically. |
| Headbutt / upgraded | Deal nine/twelve damage, then choose a discard card for the draw-pile top. Zero/one candidates resolve automatically; a multi-candidate selection submits immediately. |
| Burning Pact / upgraded | Exhaust one hand card, then draw two/three. Zero/one candidates resolve automatically; a multi-candidate hand prompt requires confirmation. |
| True Grit | Gain seven Block, then exhaust a random hand card using the owned combat-card-selection RNG. |
| True Grit upgraded | Gain nine Block, then select one hand card to exhaust. Zero/one candidates resolve automatically; a multi-candidate hand prompt requires confirmation. |

Hand selection at the cap replaces the last selected card. Deselecting and selecting
again appends that card at the end. A combat-pile prompt uses its own immediate
submission policy. A caller cannot confirm, cancel or select a different card
outside the current prompt. The existing Survivor discard transport retains its
separate immediate single-card behavior.

The engine remains independent of JSON, filesystem access and game assemblies.
The declared custom-deck scenario builder initializes from native setup definitions
and a supplied deck, without changing generated pools or consuming expected states.
Purity's additional immutable saved-card default is audited separately because the
original setup audit covered character card pools. It adds no pool membership.

## Action and clock correspondence

Every accepted public input consumes one boundary and one configured clock step.
A rejected input or inspection consumes neither. A played card and its partial
selection inputs therefore have explicit individual public boundaries.

Historical `actions-v1` selection arrays submit an entire selection in one native
action. `tools/oracle/check_ordered_choices.py` declares a zero-step clock when
comparing those atomic model results with several public inputs. It compares every
field on the existing native snapshot surface through the existing exact comparator.
Its `public-actions.json` records each native action's public action interval.
This establishes the declared completed model comparison; it neither certifies a
nonzero historical clock projection nor treats an atomic selector as native UI.

`sts2_run_scenario` accepts explicit `select_card` / `deselect_card` actions with a
candidate index, optional expected prompt identity, and `confirm_selection`.
Its response keeps the public selection projection separate from the full native
diagnostic snapshot. It verifies query purity and same-choice clone/restore futures
at every submitted boundary.

## Evidence ledger

| Surface | Current evidence |
| --- | --- |
| C++ gameplay and ownership | `CardChoices` tests cover all five roots, ordered selection, cap replacement, deselect/reselect, confirmation, source cost, random selection, draw continuation, retain, empty/forced resolution, and partial clone/checkpoint futures. |
| Source contracts | Pinned `CardSelectCmd.FromHand`, `FromHandForUpgrade`, `FromCombatPile`; `NPlayerHand.SelectCards`, `SelectCardInSimpleMode`, `SelectCardInUpgradeMode`, `DeselectCard`; `NCombatPileCardSelectScreen.OnCardClicked`; each of the five native card classes. |
| Immutable Purity default | `selection-defaults.json`, captured from actual `ModelDb.Card<Purity>().ToMutable().ToSerializable()` with pinned assembly provenance. |
| Native atomic model comparison | Capture tooling ported; successful per-case artifacts and final matrix pending. |
| Native partial callbacks | Actual scene/callback harness under development; no atomic selector or manually seeded selection fields qualify. |
| Nested hook interactions | Pending integration with the responsible supported hook models; no unimplemented hook is silently skipped or certified. |

Exact English, other characters and broader unsupported content remain outside
this slice. A missing native callback or interaction result remains an evidence gap
and prevents closing #18.
