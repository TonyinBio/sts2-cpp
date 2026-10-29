# Native selection callback and policy probe

Run the installed pinned game assembly and resource pack in Godot 4.5.1 Mono,
using real Godot scenes, card holders, signals, native selection initialization
and callbacks. This replaces neither selection methods nor their collections.
No Harmony patch, stub UI implementation or expected checkpoint is used.

```sh
python3 tools/oracle/check_native_choice_callbacks.py \
  --game-dir '/path/to/Slay the Spire 2' \
  --godot '/path/to/Godot_v4.5.1-stable_mono_linux.x86_64' \
  --simulator build/debug/sts2_run_scenario \
  --output-dir /tmp/native-hand-callbacks
```

Requires .NET 9 and a Godot 4.5.1 Mono distribution with its bundled NuGet SDK
packages. The script does not download a runtime or launch a graphical window.
It copies these sources into a new output directory and builds against the
installed pinned `sts2.dll`; game files are read only. Output retains sources,
logs, source/runtime/resource hashes, callback traces and native assembly MVID.
Each scenario runs twice in independently initialized processes and compares its
callback trace and result. Build/runtime errors fail the check. After freeing the
supplied scene contexts and awaiting two process frames, native resource caches
can still report leaked allocations during process teardown. Only the exact
allocation/resource leak diagnostics after `PROBE CLEANUP COMPLETE` are classified
separately; all lines remain in logs and in `shutdown_allocation_diagnostics`.
Any earlier error, other error format or failed cleanup fails the check.

## What executes

The declared scenario creates a native Ironclad test run and mutable Shrinker
encounter, generates its monster and starts the native combat turn loop. There
is no imported state. Native setup, shuffle, initial draw and hooks execute.
The probe supplies an `NGame` context (input/controller/hotkey managers and scene
containers), then instantiates the actual packed combat-room scene. It attaches
its actual combat UI and initializes native card views. Declared decks contain
four Defends and, for a model consumer, its source Purity or Armaments. Native
deck setup uses native card creation/pile commands before combat starts; neither
generation pools nor expected snapshots determine the setup.
This is a scene consumer scenario, not full game startup. Native `TestMode` is
used for run/combat setup, and is disabled before creating the card views.

Three isolated callback scenarios call native `NPlayerHand.SelectCards` with
synthetic selection preferences: 0–2 ordered selection, zero confirmation and
1–1 selection. They invoke the actual native simple-mode select callback,
selected-container deselection and confirmation. They distinguish replacement
of the last selected card at capacity, deselect/reselect ordering, pending task
lifetime and confirmation availability. Native returned object identities are
mapped back to scenario card indices.

The fourth scenario plays the native deck's Purity and invokes its actual
`OnPlayWrapper` as a zero-resource autoplay with source pile visuals skipped.
Its real `Purity.OnPlay` calls `CardSelectCmd.FromHand`, reserves/synchronizes a
native choice and initializes the actual hand scene. There is no test selector.
The same actual UI callbacks fill its native three-card limit, replace the last
selection, deselect and reselect. Confirmation resumes the real native consumer;
the probe awaits the wrapper and asserts exhaust order `[1, 3, 0, source]`.
It uses native `BlockingPlayerChoiceContext` and
`NonInteractiveMode.AutoSlayerCheck` to skip waits. No selection field is seeded;
reflection reads native selection state/TCS and invokes native callbacks.

The fifth scenario runs native Armaments through `OnPlayWrapper` and
`FromHandForUpgrade`. Its actual upgrade-mode holder callback replaces selections
0, 1 and 2; required confirmation resumes the native effect and upgrades only card
2. The source and all candidates began as real permanent deck cards and combat
copies. No source is inserted directly into a selection collection.

The sixth scenario initializes the actual overlay stack and packed combat-pile
selection screen. It invokes native `CardSelectCmd.FromCombatPile` using Headbutt's
own prompt/preferences, clicks discard card 1 through `OnCardClicked`, and awaits
the native task. That single click submits without confirmation. The native
`CardPileCmd.Add` consumer moves the returned card to the draw-pile top, with pile
visuals disabled. This isolates the command/callback consumer; Headbutt's full
damage/model ordering is checked separately by the atomic exact-comparison corpus.

Every hand-observation read is bracketed by a fingerprint of run/player RNG save
representations, HP/Block, energy/round and ordered native piles/upgrades. The pile
observer also repeats that fingerprint before input. These checks establish purity
on the enumerated surface, not an unrestricted whole-game state audit.

With `--simulator`, independently initialized C++ scenarios use the same visible
source/four-Defend configuration. Equivalent Defends are mapped by candidate order.
Each native Purity/Armaments callback trace is compared with the public typed C++
selection projection: candidate models/upgrades, ordered selections, minimum and
maximum, automatic submission, pending state and confirmation availability.
No private C++ state chooses player input; the scenario transport publishes public
hand metadata and selection IDs from its existing typed observation.

Every accepted public input, including confirmation, must increment the public
boundary and configured two-second clock once. Native UI callbacks do not themselves
advance the recorder's transport action clock. The report therefore records this
explicit logical-input mapping separately from the atomic corpus's zero-step
clock correspondence; it does not assert equal native wall-clock passage.

Source anchors in the pinned assembly's decompilation:

- `NPlayerHand.SelectCards`, `SelectCardInSimpleMode`, `DeselectCard`,
  `RefreshSelectModeConfirmButton`, `OnSelectModeConfirmButtonPressed`.
- `NSelectedHandCardContainer.DeselectCard` and its real signal connection.
- `CardSelectCmd.FromHand`, `Purity.OnPlay`, `CardCmd.Exhaust`,
  `CardModel.OnPlayWrapper` and `CombatManager.StartCombatInternal`.
- `CardSelectCmd.FromHandForUpgrade`, `Armaments.OnPlay`,
  `NPlayerHand.SelectCardInUpgradeMode` and `CardCmd.Upgrade`.
- `CardSelectCmd.FromCombatPile`, `Headbutt.OnPlay`,
  `NCombatPileCardSelectScreen.OnCardClicked`/`CompleteSelection`, `CardPileCmd.Add`.

## Evidence limits

This establishes these native callbacks, two model-consumer continuations, the
pile-command consumer, and the specified public prompt/clock mapping. Separate
atomic exact-comparison fixtures verify full model effects/payment; these UI
consumers use zero-resource autoplay. It does not certify full-screen rendering,
manual action-queue routing, whole-game instrumentation purity, nested hooks or
all alternate model branches. Those independent acceptance gates remain separately
tracked in the ticket's evidence ledger.
The scenario's supplied game context and native test setup are recorded choices;
this is not a claim that full native scene startup ran.
