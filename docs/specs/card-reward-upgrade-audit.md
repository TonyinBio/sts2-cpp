# Upgraded card rewards in the seed 1234 recording

Inspected 2026-10-23 against the real game v0.111.0, release commit `41cef1ea`,
`sts2.dll` SHA-256
`0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9`.

## Result

Upgraded rewards are generated. Independent execution of all 640 committed
actions in `build/recordings/trade-resumed-20261023-141703/actions.jsonl`
reproduced its final full checkpoint. Immediately before each reward choice or
skip, the audit inspected the pending real card models' serialized
`current_upgrade_level` and the native display's `upgraded` flag; they agreed.
Each offer is counted once, including skipped offers.

| Act | Reward screens | Offered cards | Upgraded cards | Upgraded cards taken |
| --- | ---: | ---: | ---: | ---: |
| 1 | 9 | 27 | 0 | 0 |
| 2 | 8 | 24 | 3 | 0 |
| 3, through action 640 | 5 | 15 | 5 | 0 |

The counts include one unupgraded event reward in act 2 (action 303); the other
screens came from combat. All upgraded offers came from combat:

| Act | Map coordinate (column, row) | Encounter | Upgraded cards | Recorded choice |
| --- | --- | --- | --- | --- |
| 2 | (4, 3) | Bowlbugs | Unrelenting+ | Shrug It Off |
| 2 | (3, 9) | Decimillipede | Howl from Beyond+, Pillage+ | Dominate |
| 3 | (0, 1) | Devoted Sculptor | Tremble+ | Skip |
| 3 | (1, 11) | Mecha Knight | Armaments+, Setup Strike+ | Skip |
| 3 | (0, 12) | The Lost and Forgotten | Perfected Strike+, Havoc+ | Headbutt |

Raw observations, provenance, and selection action IDs are retained in
`build/card-reward-upgrade-audit/result.json`. Rendered reward screens are in
the same directory as `action-000255-display.txt`, `action-000327-display.txt`,
`action-000443-display.txt`, `action-000562-display.txt`, and
`action-000592-display.txt`. These ignored artifacts cover the committed
640-action history, not subsequent gameplay in the resumed session.

## Why they were easy to miss

The recorder exports `card.IsUpgraded` as `upgraded` but uses the plain localized
card name (`tools/oracle/adapters/RecordingDisplay.cs:10–17`). The upstream reward
renderer ignores that flag when printing the name
(`sts2-cli/python/play.py:879–899`). It prints no `+` or explicit upgraded label.
For example, the Decimillipede reward display prints `Howl from Beyond` and
`Pillage`, although both real card models have upgrade level 1. Their upgraded
numeric values are present, but that does not clearly identify their status.
This establishes a presentation omission, not a failure of reward upgrades.

## Real engine rule

Sources are from the local decompilation of the pinned game assembly under
`build/combat-state-audit/decompiled/`:

- `MegaCrit.Sts2.Core.Rewards/CardReward.cs:160–178`: ordinary population uses
  `CardFactory.CreateForReward`.
- `MegaCrit.Sts2.Core.Factories/CardFactory.cs:90–103`: generated reward cards
  receive upgrade rolls unless options specify `NoUpgradeRoll`.
- The same factory at lines 283–305 draws from the reward RNG and, for upgradable
  non-Rare cards, adds `CurrentActIndex * UpgradedCardOddScaling` to the chance.
  Hooks can modify the result. A successful roll calls `CardCmd.Upgrade`.
- Factory line 23 sets scaling to 0.25 normally and 0.125 at Scarcity. Scarcity is
  ascension 7 (`MegaCrit.Sts2.Core.Entities.Ascension/AscensionLevel.cs:3–16`).

At this recording's ascension 0, the unmodified chance is therefore 25% per
eligible non-Rare card in act 2 and 50% in act 3. Rare cards receive no act-based
upgrade chance. These are probabilities, not a guaranteed fraction of each
three-card offer.
