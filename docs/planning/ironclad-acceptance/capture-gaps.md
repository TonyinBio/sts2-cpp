# Native capture prerequisites and feasibility

This is a static feasibility assessment from the audited source paths and current
adapter, not a successful live prototype. Native scenes/callbacks exist for the
required semantic surfaces; their ability to execute under the current headless
stub runtime is **not established**. Do not label a probe ready because its source
consumer has been identified.

## What the current tools can establish

`tools/oracle/adapters/RunRecordingSession.cs` installs `RunSelector : ICardSelector`
(around lines 70–128), captures selector preferences, and uses test reward selectors.
Its action path supplies a whole selection array. `ExportRunFlowState` and
`ExportRunChoice` (around 506–550) expose privileged flow and more preferences than
`OracleDisplay`, but neither exports independently observed partial native UI state.
`tools/oracle/reference_runtime.py` injects that selector and builds with Godot stubs.
Those paths support native model execution/dense diagnostics; they do not establish
native UI consumer output or scene/callback execution.

`RecordingDisplay.cs::OracleDisplay` (20–66) rejects reward-frame display and lacks
treasure/final-page/general-continuation rendering. The convenience transport can
auto-claim/skip/proceed using privileged run flow. Its recorded operations remain
useful input evidence, but its caller's screen sequence is not the public C++ action
sequence. The recording's 697 atomic stops do not include every intermediate partial
selection stop required by a public interface.

## Capability work for implementation sequencing

| Capability | Native path / source-backed cases | Missing prerequisite and completion evidence |
| --- | --- | --- |
| Public value/visibility capture | Combat consumer/inspection paths C1–C22, top bar and inventory invariants; all eight recorded room categories | Probe actual bound/rendered values, visibility predicates and current targets with pinned native identity. Keep public capture separate from privileged setup/diagnostics. Produce first independent capture for each exercised screen before claiming automation works. |
| Partial selector and preview callbacks | P01–P05, P13/P14 where reachable; NPlayerHand, simple grid, deck/pile selectors and preview parent return | Drive select/deselect/replace-at-cap/confirm/preview-cancel/whole-close separately. Capture prefs plus offered/selected object relationships and enabled actions after each callback. Do not replace UI callbacks with atomic ICardSelector replies. |
| Reward/treasure/continuation capture | P06/P09/P10/P12/P16, rewards screens, NTreasureRoom, event pages and RunManager return paths | Observe before reveal, after reveal, claim/skip and proceed/return independently, including nonterminal overlay ownership and Architect ending. Eliminate silent convenience-transport decisions in the public test driver. |
| Native view ordering | Combat pile passive/selection ordering, equal-key draw ties, event/reward/shop option sequence | Capture short and sufficiently long equal-key visibly different lists on the native runtime; retain native culture and sort inputs. A selector adapter's order is not an expected UI order. |
| Identity correspondence | Duplicate cards, movement/reappearance, deck/combat-copy links, consumed potion source and prompt nesting | Record native object correspondence privately, expose only visibility-history-safe C++ IDs, test stale references and nonleaking identity allocation. Do not compare literal native pointer/recording-label/C++ ID values. |
| Paired-state and purity harness | NI-RNG/DRAW/AI/MAP/OFFER/FRAME/ID/ERROR and X19/X20 | Establish legal reachable preconditions and equal public histories. Native visible expectations require independent consumer evidence; C++ clone/checkpoint construction alone cannot establish a native golden. Separate contract-only identity/purity assertions from native numeric output. |
| Branch/state/RNG capture | Every model obligation plus X01–X17; existing exporter provides a bounded surface | Native reachable scenario builders and branch recordings with source/build/probe provenance and independent replay. Add privileged hook traces only when needed to distinguish ordering or omitted state. Do not initialize from expected checkpoints or assume native-save restoration captures suspended frames. |

Two possible probe environments remain work to evaluate: instrument actual pinned
native scenes, or execute independently instrumented native consumers with enough
runtime support to preserve their decision behavior. Neither implementation is
selected here. A helper that reproduces a predicate in Python/C++ is not independent
native evidence. Instrumentation must demonstrate that it does not change gameplay,
RNG or offered inputs, and must record its patches/runtime identity separately.
If native UI execution cannot be made faithful in the stub environment, report that
limit and use the actual native runtime rather than certify the stub's approximation.

## Case applicability and scope

The 34 public families preserve all existing audit cases so gaps are not lost.
They are not 34 automatically mandatory new gameplay features. Ten combat families,
16 prompt families and eight NI families must be applied to the supported closure:

- Ordinary combat, existing piles, visible intents/powers, target validation and
  safe-query families apply through the audited roots. Ambergris and Droplet are
  source examples; they do not automatically add their effects to this milestone.
- P01/P02/P03/P04/P06/P07/P08/P09/P10/P12/P16 cover the relevant selector, shop,
  rest, reward, treasure, event, nested return and ending semantics. Use a supported
  native caller for each variant. A constructed preference combination without
  a reachable caller is not runtime scope evidence.
- P05/P13/P14 require demonstrated supported callers for dynamic pile, bundle and
  separate relic selector respectively. Keep them as conditional obligations; do
  not invent a visible prompt when the native call auto-selects or returns empty.
- P11 applies to reached topology and supported travel; A10 second-boss and unrelated
  modifier behavior do not become first-run certification requirements. Existing
  authored root branch obligations remain unchanged.
- P15's graphical seed/modifier editor is outside the first in-run capture gate.
  Supplied configuration, canonical seed and identity validation remain required;
  randomize-menu/checkbox artwork and unrelated modifier mechanics are deferred.
- Terminal platform/profile discovery, badges and surrounding menus remain excluded.
  Natural ending semantics and in-run score fields already required by the public
  contract cannot be omitted under a generic presentation exemption.

For every exercised screen/prompt, record: reachable setup; native caller; actual
visible ordered objects/values; semantic enabled/disabled state; offered action and
selection constraints; native object correspondence; partial/return state; before/
after privileged diagnostic state; comparison surface and pinned provenance. Exact
English remains separately uncertified, while structured prompt meaning must match.
No screenshot/artwork comparison, UI transport API or public JSON is required.

## Remaining work is explicit

This task has specified the case/gap inventory; the native probes, corpus captures,
oracle self-replays for new cases and C++ acceptance tests still need execution.
They belong in the implementation plan with the engine slices they verify, not a
second broad fact-gathering ticket for these same known gaps. Choose integration
order in [the milestone decision](https://github.com/TonyinBio/sts2-cpp/issues/8).
Deferred English presentation/context and broader recordings keep their own scope.
