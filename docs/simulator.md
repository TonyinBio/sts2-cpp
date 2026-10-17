# Combat simulator and reference parity

The native C++20 library implements the `ironclad_starter_shrinker_v1` feature for the
pinned v0.111.0 game DLL. Its reference and simulator adapters share combat-v1. It is
a supported vertical slice of combat, not an implementation of every card or full runs.

## Native API

`make_starter_combat(uint64_t)` creates an independent Ironclad A0 combat with the
starter deck, one 38-HP Shrinker Beetle, and a shuffled five-card hand. Its numeric seed
initializes the combat shuffle RNG directly; it does not reproduce the game's string
seed, map generation, or prior full-run RNG consumption. For exact reference replay,
import the initial reference snapshot instead.

`legal_actions(state)` returns a fixed-capacity list of affordable hand-card actions
and end turn. Native actions use current hand indices; target 0 is the sole enemy,
-1 means no target. Defend must omit a target; attacks require enemy 0. Invalid actions
return false without modifying state or RNG. `step` assumes a valid supported state,
as constructed by setup/import; arbitrary edits to pile counts/IDs violate that contract.

Copy `CombatState` by value to branch AI search. It contains numeric card identities,
three fixed-capacity piles, compact power state, current enemy move/history count,
HP/block/energy, and the shuffle RNG. History is reconstructed from the fixed AI sequence,
so long simulations do not grow heap storage. Empty exhaust/playing piles need no kernel
storage for these mechanics at settled boundaries. No strings, model objects, JSON,
virtual dispatch, or owning pointers are used by the native kernel.

Only the Shuffle RNG advances in this profile. The other 14 reference streams are held
in the immutable test-adapter context and compared exactly at every boundary. They are
not copied with native search branches. Additional mechanics that consume those streams
must explicitly add the required mutable state before being supported.

On the tested x86_64 GCC build, native state occupies 168 bytes. A GoogleTest allocation
check covers combat setup, copying, legal action enumeration and complete rollouts;
these operations make no heap allocations. JSON adapters and diagnostics intentionally
allocate outside the simulation loop.

## Mechanics and ordering

- Strike deals 6 damage; Defend gains 5 block; Bash costs 2, deals 8 damage, then applies
  2 Vulnerable if combat is still active. Other supported cards cost 1 energy.
- Shrink multiplies powered attack damage by 0.7 and Vulnerable by 1.5. Apply multipliers
  together and floor once, before block. Bash's own hit precedes its debuff.
- Played instances move to discard; at end turn, remaining hand cards append in order.
  Draw five on the next player turn, reshuffling as necessary.
- Reference `StableShuffle` first sorts by ordinal model ID then upgrade level. All
  supported cards are unupgraded. The ten-element .NET insertion sort preserves order
  among equal models; C++ uses the same ordering without adding instance tiebreakers.
  It then applies descending Fisher–Yates using the reference NextInt scaling.
- RNG implements SplitMix64 initialization and xoshiro256** output. NextInt uses the
  upper 53 bits scaled through a double, not modulo or a C++ uniform distribution.
  Serialized counters and all four state words are retained exactly. Do not enable
  fast-math options that could change reference-compatible arithmetic.
- Beetle moves: Shrink, Chomp (7 damage), Stomp (13), then Chomp/Stomp repeatedly.
  Turn-start power counters, duration ticks, new power identities, block clearing,
  and the final performed move are exported at matching boundaries.
- Victory removes combat cards/powers, clears player block, and heals up to 6 HP from
  Burning Blood. Defeat retains the engine's piles after hand discard, removes player
  powers, and stops before the next player draw. No post-combat rewards are simulated.

Unsupported cards, upgrades, relics, character mechanics, selections, targets, pending
queues, encounters, or initial counters are rejected by the importer. The core has a
capacity of ten cards because it implements this exact starter loadout. Expanding the
profile requires mechanics, native capacity/state updates where necessary, reference
export coverage, and new parity cases.

## Import and replay

`tests/parity/simulator_adapter.*` translates validated canonical JSON to/from native
state. It accepts only fresh round-1 player-action snapshots with the default loadout,
full-health player, and full-health A0 beetle (38–40 HP). It imports every RNG stream and
preserves the recording's card identities. An exact initial export/import round trip
rejects unrepresented state, including unknown counters, costs, powers, flags, or IDs.
Mid-combat arbitrary imports are outside this initial adapter.

`parity_replay actions.json states.json` checks the raw action-file SHA-256, parses the
action/result envelopes, checks reference game/revision identity, and imports the initial
snapshot once. It executes only the prerecorded decisions. Subsequent expected snapshots
are used solely for comparison, never to repair/resynchronize simulator state. It stops
at the first invalid action or divergent field and prints recording/action identity,
JSON Pointer, expected value, and actual value. All fields and array orders are compared.

Checked-in reference cases cover attacks/defense, multiple shuffles, Bash/Vulnerable,
Shrink, power expiry/reapplication and identities, victory healing/cleanup, and defeat.
Their metadata and action digests are retained. Offline tests require no game installation.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/parity_replay tests/parity/examples/starter.actions.json \
  tests/parity/fixtures/starter.states.json
```

## Live differential campaign

`tools/oracle/check_simulator.py` builds the same pinned temporary reference host as the
recorder, generates varied string seeds and legal player decisions, records complete
states-v1 traces, and invokes C++ replay. It mixes end-turn-only policies, attack-priority
policies, and reproducible random policies. Actions come from affordable cards in the
live reference snapshot; the recorder itself still executes supplied actions only.

```sh
python3 tools/oracle/check_simulator.py \
  --game-dir '/mnt/c/Program Files (x86)/Steam/steamapps/common/Slay the Spire 2' \
  --episodes 32 --policy-seed 17
```

Pass `--replay` and `--validator` to use release tools. Generated action files, reference
states and transcripts remain under `build/parity-campaign/` so a failed case can be
replayed directly. This checks the supported profile against sts2-cli, not against a
separately running Godot game; existing adapter/runtime patch limitations still apply.

## Benchmark

```sh
cmake --preset release
cmake --build --preset release
./build/release/simulation_benchmark
```

The benchmark copies from 256 varied starting states and runs one million complete combats,
including action enumeration and transitions. Initialization, JSON validation, reference
processes, and fixture comparison are excluded from timing. It reports native state size,
combats/second, transitions/second, and an observable checksum. Results describe this
small supported encounter, not full-game simulation performance.

Validation for this implementation: 78 CTest entries pass in debug and release; 12
native unit tests and 7 digest/import/parity tests also pass under ASan/UBSan. A live
32-episode campaign (`--policy-seed 17`) matched all 566 transitions: 27 victories and
5 defeats. A local release benchmark measured approximately 1.3 million supported
combats/second and 22.8 million transitions/second. These are single-machine measurements,
not a portable performance guarantee or throughput for unimplemented game content.
