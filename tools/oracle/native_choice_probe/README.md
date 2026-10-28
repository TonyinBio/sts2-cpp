# Native hand callback probe

Run the installed pinned game assembly and resource pack in Godot 4.5.1 Mono,
using real Godot scenes, card holders, signals, native selection initialization
and callbacks. This replaces neither selection methods nor their collections.
No Harmony patch, stub UI implementation or expected checkpoint is used.

```sh
python3 tools/oracle/check_native_choice_callbacks.py \
  --game-dir '/path/to/Slay the Spire 2' \
  --godot '/path/to/Godot_v4.5.1-stable_mono_linux.x86_64' \
  --output-dir /tmp/native-hand-callbacks
```

Requires .NET 9 and a Godot 4.5.1 Mono distribution with its bundled NuGet SDK
packages. The script does not download a runtime or launch a graphical window.
It copies these sources into a new output directory and builds against the
installed pinned `sts2.dll`; game files are read only. Output retains sources,
logs, source/runtime/resource hashes, callback traces and native assembly MVID.
Each scenario runs twice in independently initialized processes and compares its
callback trace and result. Build/runtime errors fail the check. Native shutdown
currently warns about leaked scene objects/RIDs; those warnings are retained.

## What executes

The declared scenario creates a native Ironclad test run and mutable Shrinker
encounter, generates its monster and starts the native combat turn loop. There
is no imported state. Native setup, shuffle, initial draw and hooks execute.
The probe supplies an `NGame` context (input/controller/hotkey managers and scene
containers), then instantiates the actual packed combat-room scene. It attaches
its actual combat UI and initializes native card views for four hand cards.
This is a scene consumer scenario, not full game startup. Native `TestMode` is
used for run/combat setup, and is disabled before creating the card views.

Three isolated callback scenarios call native `NPlayerHand.SelectCards` with
synthetic selection preferences: 0–2 ordered selection, zero confirmation and
1–1 selection. They invoke the actual native simple-mode select callback,
selected-container deselection and confirmation. They distinguish replacement
of the last selected card at capacity, deselect/reselect ordering, pending task
lifetime and confirmation availability. Native returned object identities are
mapped back to scenario card indices.

The fourth scenario creates native Purity in the combat and invokes its actual
`OnPlayWrapper` as a zero-resource autoplay with source pile visuals skipped.
Its real `Purity.OnPlay` calls `CardSelectCmd.FromHand`, reserves/synchronizes a
native choice and initializes the actual hand scene. There is no test selector.
The same actual UI callbacks fill its native three-card limit, replace the last
selection, deselect and reselect. Confirmation resumes the real native consumer;
the probe awaits the wrapper and asserts exhaust order `[1, 3, 0, source]`.
It uses native `BlockingPlayerChoiceContext` and
`NonInteractiveMode.AutoSlayerCheck` to skip waits. No selection field is seeded;
reflection reads native selection state/TCS and invokes native callbacks.

Source anchors in the pinned assembly's decompilation:

- `NPlayerHand.SelectCards`, `SelectCardInSimpleMode`, `DeselectCard`,
  `RefreshSelectModeConfirmButton`, `OnSelectModeConfirmButtonPressed`.
- `NSelectedHandCardContainer.DeselectCard` and its real signal connection.
- `CardSelectCmd.FromHand`, `Purity.OnPlay`, `CardCmd.Exhaust`,
  `CardModel.OnPlayWrapper` and `CombatManager.StartCombatInternal`.

## Evidence limits

This establishes these native scene callbacks and one real command/consumer
continuation. It does not establish C++ projection equivalence or complete #18
acceptance. Separate atomic exact-comparison fixtures must connect these semantics
to C++ execution. It does not certify manual payment, action-queue routing,
nonzero action clocks, whole-game instrumentation purity, other source cards,
combat-pile screens, nested hook prompts or all alternate/upgrade branches.
The scenario's supplied game context and native test setup are recorded choices;
this is not a claim that full native scene startup ran.
