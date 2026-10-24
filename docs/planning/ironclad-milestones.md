# Implementation milestones for Ironclad victory parity

Resolution handoff for [Choose implementation milestones and dependency order for complete coverage](https://github.com/TonyinBio/sts2-cpp/issues/8).
The plan uses integrated slices, measurements during development and partial
selection support in the simulator. This is a
planning handoff; no engine or probe implementation is claimed.

## Selection and action model

Keep partial selection in the simulator, following the user's latest decision.
Pending prompts retain selected objects, their meaningful order, selection limits,
and available select/deselect/confirm operations. Preserve order whenever it changes
effect execution or future state. Clone/checkpoint/restore must retain partial
selection and resume the same prompt. Separate confirmation remains supported where
required by the mechanic. Complete ordered submissions may coexist where legal;
they do not replace the partial-selection capability.

For redundant menu cancellation, opening
optional removal and cancelling back is equivalent to not opening it when gameplay
state and future behavior are unchanged. Do not infer from this equivalence that
all cancellation effects are identical or that partial selections can be discarded.
Preserve actual gameplay effects, payment changes, reveals and future choices.

Keep the original 697-boundary dense pair and its provenance unchanged. Map partial
public inputs to the reference tool's atomic submissions explicitly; compare at
corresponding completed decisions and capture partial states independently. Do not
invent intermediate expected state by filtering the dense export. Declare clock
inputs/mapping for intermediate actions and compare gameplay RNG/state exactly.

## Gameplay choices and equivalent objects

The user further narrowed acceptance to strategically distinct gameplay choices.
Expose reward selection, including reclaiming the card stolen by Thieving Hopper,
and taking versus declining a relic. Do not automatically claim, skip or substitute
these choices in a convenience transport. Verify their effects and follow-up hooks.

Finished event pages and forced continue prompts with no differing gameplay outcome
are automatic transitions, not required public actions or independently captured
screens. Execute the underlying effects once in native order; do not skip the
transition itself. Preserve any actual reward, branch, target or selection choice.

The user accepts interchangeability of effectively identical objects, including
matching enchantments and other gameplay-relevant properties. Numeric identity or
distinguishing two equivalent copies is not an acceptance goal. Preserve references
or distinguish objects when their history, pending effects, attachments, location or
other state makes their future behavior differ. Compare equivalent objects through
a declared correspondence rather than requiring equal native/C++ labels. This does
not authorize collapsing a permanent deck card with its distinct combat copy or
losing a retained source when those relationships affect gameplay.

Use equivalent complete hidden gameplay state as the main correctness evidence.
The hidden-to-visible projection must preserve gameplay state/RNG and translate it
correctly: expose meaningful offers, choices, restrictions and intentional reveals,
while withholding hidden information. State equivalence and read purity alone do
not prove projection correctness. Prefer focused projection tests and source-backed
field mappings; do not require separate native screen captures merely to certify
artwork, redundant pages or equivalent duplicate identities. Retain targeted native
evidence where the mapping or a strategic choice is not established.

## Integrated milestones

Each milestone includes its state ownership, deterministic transitions, public
observation/action support, export correspondence, codec/clone validation, applicable
native comparisons and alternate/interaction regressions. Preserve verified setup,
RNG, standard maps, clocks, storage and replay foundations. Implement shared systems
through the slices that exercise them. No isolated foundation rewrite is required.

| Milestone | Working behavior | Dependencies and acceptance |
| --- | --- | --- |
| 1. Reusable run execution | Consecutive supported encounters with distinct deck/combat identities, retained history/inventory and gameplay-relevant object relationships; one real ordered nested choice survives clone/restore. | Existing RNG/setup/maps/codecs; remove single-combat/Survivor-specific restrictions through general ownership and typed continuations. Compare consecutive-room lifecycle and same/alternate resumed choices. Establish the public/native action mapping and measurement driver on supported workloads. |
| 2. General combat and inventory | Multiple enemies/entities, ordered listeners and mutations, targeting, damage/powers, card movement/draw/exhaust, repeats/autoplay and consumed-potion child choices. | Milestone 1 ownership; integrate required cards/powers/relics/potions/attachments by shared capability dependencies from the semantic audit. Cover removal during dispatch, captured costs, sparse slots, revival and completion as each becomes supported. Never claim full mechanics from only the recorded branch. |
| 3. Complete room loop | Rewards, shop/removal, rest, treasure and all audited event branches, with real child prompts and nested combat/reward return. | Shared lifecycle and relevant combat/content capabilities. Deliver vertical room slices in capability order; native public capture and regression work accompanies each. Preserve ordered offers, generation, stock/gold/hooks and the agreed service and selection semantics. Include fixed outputs and authored alternatives; unrelated broad-pool effects may remain explicitly unsupported. |
| 4. Acts and natural victory | Repeated act transitions, audited bosses/revival, final Architect encounter and automatic nonbranching dialogue/continuation and natural run completion through public C++ inputs. | Required room/content closure from milestones 2–3; remove act-zero restriction without losing native queues, topology/history or clocks. Incrementally grow the replayed prefix; final success requires natural victory, not synthetic terminal assignment. |
| 5. Ironclad acceptance | Complete observed/offered-plus-fixed mechanics and alternate branches, interactions, hidden-information protection, purity and checkpoint continuation for every supported logical stop. | All prior slices; exact gameplay replay on declared corresponding boundaries, evidence for meaningful public choices and correct pure projection, native self-replay for new fixtures, complete case/status matrix. Finish remaining regression gaps; first correct victory may precede 2× performance. |

Milestones 2 and 3 contain interdependent vertical slices: implement a room's required
mechanics and supporting models before accepting that room, while reusing shared
capabilities already delivered. Their numbers express prerequisite capability layers,
not a requirement to finish every combat model before beginning any room. Use the
existing semantic/dependency and lifetime inventories to derive implementation tickets.
Include the fixed conditional capabilities even when their model names come from a
different character; this does not require another character's complete run yet.

## Reference work and performance placement

Tooling timing is confirmed for the remaining gameplay surfaces as well: add reward
lists and choices (including reclaiming Thieving Hopper's stolen card), relic
take/decline, and required gameplay observations/projection checks alongside their
simulator implementation. Each feature's acceptance includes the reference-tool
extension and evidence needed to verify that feature. These are implementation
dependencies, not a separate up-front tooling project or another broad audit.
Redundant finished-page/continue controls and distinctions between interchangeable
objects do not become required tool features.

Extend the reference tool alongside simulator partial-selection implementation.
Require the resulting evidence before declaring those selections verified; there
is no separate early tool-only milestone.

The current atomic reference selector cannot validate select/deselect/confirm steps.
Extend reference tooling to capture the native partial-selection behavior that the
simulator now retains. This is concrete implementation work, not another task to
rediscover the known limitation or another preference question for the user.
Implement it alongside the choice-owning engine slice and demonstrate that path
before accepting its partial-selection parity. Do not block unrelated engine work
on completing every probe first.

Native public-screen value/order/reveal gaps also remain real. Add suitable probes
or independently instrumented consumers with the room/mechanic slices they verify.
Use source-backed translation tests over equivalent hidden state for established
fields, including query purity and hidden-information protection. Add targeted native
evidence for unresolved semantics or choices. Do not require a parallel graphical
observation corpus for every field. Faithful execution of new native probes in the
stub runtime is unproven; validate each required new capability as it is implemented.
Graphical screenshots, artwork and animation parity are excluded.

Start the equivalent warm benchmark driver in milestone 1 using supported workloads;
establish the native warm lifecycle, output profiles and explicit provenance. Add
integrated workloads as they become available and measure changes through all slices.
Full victory throughput cannot be measured until the simulator can execute victory.
Keep startup/kernel-only diagnostics distinct and use equivalent public outputs,
text requests and serialization in the compared timed work. Correctness remains the
priority. The existing final 2× gates and user review rules remain unchanged; no
performance exception or requirement to hit 2× before first correct victory is added.

## Outside-scope effects and subsequent work

Keep native generation pools/order/filters and RNG consumption. Additional broad-pool
effects outside observed-plus-fixed scope remain unsupported. Detect missing required
capability before publishing a false successful gameplay result. Prevalidate known
unsupported requests where possible; if unsupported behavior is discovered after an
accepted transition has started, use the existing invalid-simulation/failure boundary
contract. Publish diagnostics separately, stop execution and require restore or
reinitialization; never silently reroll, substitute or present a partially executed
state as a valid new gameplay boundary. The implementation spec must enumerate
nonexecuting capabilities actually supported for those candidates and test the stop.

Next handoff: turn these milestones into a buildable Ironclad specification, then
dependency-linked implementation tickets. Work still needed includes concrete fixture
builders/probes and implementation detail; those are deliverables to execute, not
reasons to repeat the accepted preference interview.

After Ironclad parity, use the user's additional character recordings to expand
reachability and coverage. Broader character/ascension/modifier certification, the
representative release corpus, exact-English presentation policy and Python bindings
remain staged later. The full-simulator destination and performance target remain;
this handoff does not claim their detailed planning is complete. The English decision
must remain deferred after this planning ticket closes, until Ironclad parity exists.
