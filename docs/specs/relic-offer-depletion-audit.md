# Relic offers and repeat appearances in the installed game

Inspected on 2026-10-23 against the real game's decompiled `sts2.dll`, not the
simulator or `sts2-cli` shop implementation. Target: v0.111.0, release commit
`41cef1ea`; assembly SHA-256
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.
The installed assembly and release metadata were checked during this investigation.
Sources below use the existing local ILSpy decompilation in
`build/combat-state-audit/decompiled/` (ignored build artifacts).

## Finding

In ordinary shop and player-bag random reward generation, **an offered relic is
removed from the run's available relic pool when the offer is generated**. Buying
it is unnecessary. Passing up a shop relic therefore prevents that relic from
being randomly offered again later in the run through these paths. However, the
treasure-chest path has a pool-exhaustion exception, including in single-player:
its shared bag can refresh an exhausted rarity and restore previously offered relics.
Thus the strict answer to whether recurrence is ever possible is yes; normally,
passing up a shop relic removes it from subsequent random offers.

This is a source-traced conclusion, not a live seeded-run experiment. It applies
to the inspected game version and ordinary pool-based offers; it does not assert
that every scripted relic grant uses this mechanism.

## Engine evidence

All paths in this table are relative to `build/combat-state-audit/decompiled/`.

| Source | Behavior establishing the result |
| --- | --- |
| `MegaCrit.Sts2.Core.Entities.Merchant/MerchantInventory.cs:78–90,126–138` | Creating a normal merchant populates three relic entries, including one shop-rarity entry. |
| `MegaCrit.Sts2.Core.Entities.Merchant/MerchantRelicEntry.cs:25–39` | Constructing each entry calls `FillSlot`, which calls `RelicFactory.PullNextRelicFromBack`; this happens before any purchase. |
| `MegaCrit.Sts2.Core.Factories/RelicFactory.cs:64–77` | Pulling for a shop consumes `player.RelicGrabBag.PullFromBack` and also removes the result from the shared grab bag. The engine's own method documentation says: “The pulled relic will never be seen again on this run.” |
| `MegaCrit.Sts2.Core.Runs/RelicGrabBag.cs:156–170` | `PullFromBack` selects an eligible relic, executes `availableDeque.RemoveAt(num)`, and returns it. Selection consumes the pool entry. |
| `MegaCrit.Sts2.Core.Entities.Merchant/MerchantRelicEntry.cs:55–65` | Purchase separately spends gold and obtains the already selected relic; purchase is not the trigger for the earlier pool removal. |
| `MegaCrit.Sts2.Core.Entities.Players/Player.cs:305–309`; `MegaCrit.Sts2.Core.Runs/RelicGrabBag.cs:38,60–67,218–242` | Ordinary players receive `new RelicGrabBag()`, whose refresh flag defaults to false. Exhaustion falls through to other rarity queues rather than restoring previously offered relics. |
| `MegaCrit.Sts2.Core.Rewards/RelicReward.cs:75–95`; `MegaCrit.Sts2.Core.Factories/RelicFactory.cs:45–49`; `MegaCrit.Sts2.Core.Runs/RelicGrabBag.cs:129–145` | Random relic rewards use the same player's bag, pulling from its front and removing the selected entry. A relic removed for an earlier shop offer is consequently unavailable to these rewards too. |
| `MegaCrit.Sts2.Core.Runs/RelicGrabBag.cs:274–314` | Native serialization saves and restores the remaining relic lists, preserving depletion through an ordinary save/load. |

`MerchantRelicEntry.SetModel` also marks the relic as seen for collection tracking
(`MerchantRelicEntry.cs:47–53`). That tracking is separate from the `RemoveAt`
operation responsible for run-level depletion.

## Scope and exceptions

Treasure selection uses the shared grab bag, including with one player:
`MegaCrit.Sts2.Core.Multiplayer.Game/TreasureRoomRelicSynchronizer.cs:98–109`
pulls from `_sharedGrabBag` for each player without a multiplayer-only condition.
That bag is constructed with refresh enabled (`MegaCrit.Sts2.Core.Runs/RunState.cs:276`).
Although its documentation describes multiplayer use
(`MegaCrit.Sts2.Core.Runs/IRunState.cs:138–145`), the call path above also applies
in single-player. If the requested rarity's queue is empty, `GetAvailableDeque`
refreshes that rarity from the original list
(`MegaCrit.Sts2.Core.Runs/RelicGrabBag.cs:218–226,325–343`). This can restore a relic
removed for an earlier shop offer. Default relic eligibility does not exclude
previously seen relics (`MegaCrit.Sts2.Core.Models/RelicModel.cs:434–437`). This is
a source-level exhaustion exception, not a demonstration that a normal-length run
actually reaches it.

Unchosen multiplayer treasure relics additionally have a fallback queue
(`MegaCrit.Sts2.Core.Runs/RelicGrabBag.cs:30–36,193–215`).

The factory returns Circlet when no eligible relic remains
(`MegaCrit.Sts2.Core.Factories/RelicFactory.cs:13,47,75`). This fallback is not a
previously consumed ordinary relic being restored to the pool.

## Joss Paper in the seed 1234 recording branches

The retained recordings explain the apparent repeat across the user's resumed
sessions. An older branch offered **Red Skull, Joss Paper, Burning Sticks** at
act 2, coordinate `(3, 5)`: see
`build/recordings/my-run2-shop-resumed/.pending-so1htl7g/states.json`,
transition `action-000295`. Joss Paper is already absent from that state's player
grab bag, consistent with removal on offer.

The newer branch instead offered **Red Skull, Gorget, Burning Sticks** at that
same shop: `build/recordings/shop-resumed-20261023-032054/states.jsonl:290`.
Its act 3 shop at `(2, 5)` offered **Centennial Puzzle, Joss Paper, Miniature Tent**:
`build/recordings/trade-resumed-20261023-141703/states.jsonl:6`.
Joss Paper remains in the newer branch's player bag at its earlier checkpoints
and is removed at this later shop.

These are different action histories. Comparing
`my-run2-before-second-removal/saves/Ironclad_1234_before-second-removal.json`
with `trade-resumed-20261023-141703/saves/Ironclad_1234_20261023_142756.json`
under `build/recordings/`, the first difference is saved command 159 (index 158):
the older history goes directly to act 2 `(3, 1)`, whereas the newer history enters
the Ancient at `(3, 0)` and chooses option 2 before continuing. Thus the earlier
Joss Paper offer belongs to a discarded branch, not an earlier shop in the latest
reconstructed run. This evidence does not indicate pool exhaustion or a relic
being reinserted within one uninterrupted action history.
