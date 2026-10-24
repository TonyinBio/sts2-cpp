# Public identities, distinctions and paired-state cases

This is a planning/test handoff for the existing decision contract, not a new
interface design or an executed test suite. Native source findings and adapter
limitations live in the companion reports. All cases below still require execution
against an independently instrumented native public path and the eventual C++ API.

## Cross-cutting native fields

Source paths below are relative to `build/combat-state-audit/decompiled/`.

| Source and inspected range | Finding and concrete source-derived case |
| --- | --- |
| `MegaCrit.Sts2.Core.Nodes.CommonUi/NTopBar.cs:307–349`, `Initialize` | Ascension icon/text is enabled above zero; modifiers enumerate in run order. HP, gold, room/floor, boss, portrait, deck and potion widgets initialize separately. Test A0 versus A1 and zero versus two modifiers; an absent ascension icon is not evidence of an unknown configured ascension. Excluded achievement/profile UI does not expand simulator scope. |
| `MegaCrit.sts2.Core.Nodes.TopBar/NTopBarHp.cs:104–113`, `UpdateHealth`; `MegaCrit.sts2.Core.Nodes.TopBar/NTopBarGold.cs:113–118`, `Initialize` | HP displays current/max, gold displays its actual value including zero. HP/gold animations are presentation, not additional gameplay input stops. Observe settled values rather than intermediate animation frames. |
| `MegaCrit.Sts2.Core.Nodes.Potions/NPotionContainer.cs:208–264,295–307`, `Initialize`, `GrowPotionHolders`, `Add` | Creates holders from maximum capacity, then places potions by `PotionSlots.IndexOf`. Test occupied/empty/occupied slots: preserve the empty middle holder and capacity. The injected display preserves occupied slot indices but omits empty holders and capacity; upstream instead enumerates compact `Player.Potions`. |
| `MegaCrit.Sts2.Core.Nodes.Relics/NRelicInventoryHolder.cs:229–241`, `RefreshAmount` | Display counter only when `ShowCounter` and the run is in progress; otherwise hide it. Test a shown zero counter versus a hidden counter. Do not expose every private integer or dynamic variable as a visible counter. |
| `MegaCrit.Sts2.Core.Nodes.Screens/NDeckViewScreen.cs:236–310`, `OnPileContentsChanged`, sort handlers, `DisplayCards` | Copies deck cards and applies user-selected obtained/type/cost/alphabet sorting priorities. Inspection order is a view property; changing a sort must not reorder the authoritative deck. Test obtained ascending/descending and cost ties. |
| `MegaCrit.Sts2.Core.Nodes.Screens.GameOverScreen/NGameOverScreen.cs:647–671`, `AnimateScoreLines` | Score lines appear in floor, gold, optional elites, optional bosses, optional ascension order. Elites/bosses/ascension are omitted at zero, rather than rendered as zero-valued lines. Compare loss/victory and A0/A1; capture actual values through `ScoreUtility`, not invented arithmetic. Profile discoveries, unlock progress and services remain excluded. |

These anchors establish the listed rules, not every top-bar hover tip, terminal badge,
history field or English substitution. The English-source audit owns exact rendering.
The native-prompt report covers terminal acknowledgment and history access; additional
runtime cases must establish their settled boundary mapping.

## Identity and relationship requirements

The game UI uses object relationships and transient screen positions, not the proposed
public C++ IDs. Stable public IDs are an existing contract requirement. Native pointers,
list indices and definition IDs therefore cannot serve as independent expected ID
values. Test identity relationships under a declared native-to-C++ correspondence.

`tools/oracle/adapters/RunRecordingSession.cs:489–506` clears its combat ID registries
at a new combat and assigns `card_N` by `PlayerCombatState.AllCards` enumeration.
Those IDs are privileged recording labels. `RecordingDisplay.DisplayCard` instead
uses model ID plus current index; duplicate cards can share that model ID. Neither
establishes stable public identity. Public identities must retain the distinct
deck/combat-copy links established by the lifetime audit and distinguish object
identity from a potion slot, displayed position, option and prompt identity.

Required relationship cases:

- Reorder a visible hand or inspection view: each same object keeps its ID; two
  copies of a definition remain distinct. Returning an object to a previously visited
  pile does not create a replacement identity.
- Consume a potion that opens a card choice: slot empties while the source relationship
  remains owned by the continuation. A new potion in that slot has a new instance ID.
- Create an unseen object, observe repeatedly, and later reveal it: reads cannot
  consume identity allocation or alter its eventual public ID. Hidden allocation
  counts must not be inferable from unrelated visible IDs or error messages.
- Clone/restore a partial nested choice: prompt/option/object identities and selection
  relationships survive. Two identical-looking consecutive prompts remain distinct;
  stale prompt and boundary actions reject atomically.

These constrain the implementation without choosing a public ID encoding, promising
numeric equality with native labels, or authorizing a change to complete-state IDs.

## Paired-hidden-state noninterference cases

Use equivalent public histories and explicit object correspondences. A privileged
test builder may create paired states, but expected public values come from audited
native inspection paths. Reject impossible pairs. A field is eligible to vary only
when its native display/action dependencies are unchanged. The observation, action
list, IDs, ordering, exposed restrictions and on-demand safe feedback must agree.

| Case | Hidden variation | Hold fixed / expected comparison |
| --- | --- | --- |
| NI-RNG | Future RNG state/counters | Already generated public offers, current intents, visible values and legal inputs. No RNG fields, seed derivations or debug diagnostics leak; future actions may subsequently diverge. |
| NI-DRAW | Permute draw backing sequence among distinct native inspection sort keys | Same cards and visible modifications, counts, current hand and public identity history; no explicit sequence reveal active. Same sorted inspection and public actions. Do not extend this assertion blindly to same-key duplicates; see combat report's tie-order gap. |
| NI-AI | Change a future AI roll/history element that does not feed current intent/hover/previews | Same current visible intent and target eligibility. Exclude all current visible forecast dependencies from the variation. Never substitute an AI move-state name for a public intent. |
| NI-MAP | Vary unrevealed unknown-room result or future act generation | Same visible map topology, node icons, visited path, available travel and boss disclosure. A current map/boss reveal invalidates the pair if it exposes the changed fact. |
| NI-OFFER | Change not-yet-generated or inaccessible future stock/reward choices | Current offered objects, displayed locked/sold state, costs and legal inputs. Revealing the changed stock is a later intentional divergence. |
| NI-FRAME | Change an opaque retained execution detail not used by current public fields | Same prompt owner, instruction, options, restrictions, partial selection and visible captured feedback. This cannot omit semantic state from checkpoints. |
| NI-ID | Allocate/retire an otherwise unobservable instance or alter hidden storage slot placement | Same public identity history and visible object graph. Public IDs and enumerated options do not reveal allocation order/count or future pool positions. |
| NI-ERROR | Submit the same stale/malformed public action to equivalent pairs | Equal public restriction category/reason and unchanged full state/RNG/IDs; privileged diagnostics stay outside the public channel. Distinguish native accepted attempts, including full-inventory purchase failure, from rejected inputs. |

For an explicit reveal mechanic, add before/during/after triples: variation invisible
before reveal, only the specified fact different while revealed, then no extra live
hidden information afterward. A player may remember previously revealed information;
identical current screens alone do not make public histories equivalent. Visibility
duration and whether an inspection is modal come from that mechanic's native source,
not a universal reveal flag. The combat report identifies audited examples and gaps.

## Missing, hidden, zero and empty

Keep these distinctions in typed semantic records; no JSON encoding is requested.

| Example | Required distinction |
| --- | --- |
| No combat at a map stop | Combat record inapplicable, not a synthetic combat with zero enemies/resources |
| Empty inspectable discard pile | Known empty sequence/count zero, not inaccessible or unknown contents |
| Hidden intent/private power | No public details; do not invent a zero attack or a disabled action explaining private causes |
| Shown zero relic counter | Present value zero; `ShowCounter=false` is a different visibility state |
| Empty potion holder | Known empty slot at its position, not a missing list entry |
| No eligible options | Empty offered sequence; do not fabricate blocked alternatives or infer cancellation/skip |
| Variable selection with zero chosen | Current partial selection is empty and may allow confirm; it is not cancellation or reward skip |
| Uncertified English text | Explicit certification limit; does not relax structured correctness or hidden-information protection |

## Inspection purity experiment

For every supported logical input stop, capture complete owned state, RNG, clock,
identity allocator, boundary/prompt identity and pending work. Run repeated public
observation/action enumeration, every available inspection/sort/preview and requested
formatting; pending-input `advance` must return the same stop. Capture again and
require equality, then execute the same next input on inspected and control clones
and compare future public and complete state. Preserve the native distinction between
local inspection UI changes and gameplay mutations. Selection toggles are accepted
prompt inputs and are not covered by the read-only assertion.

`tools/oracle/check_play.py:13–75` already specifies a bounded seeded display-on/off
comparison, repeated display, map inspection and complete-state equality. Its trace
does not cover every room, nested prompt, reveal, preview or hidden-state pair; it was
not rerun here. Do not convert that narrow regression into universal purity evidence.
