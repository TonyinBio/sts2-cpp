# Native serialization fixture

`serializable-run-v20.json` was emitted by the v0.111.0 game's `SaveManager.ToJson`
from the synthetic input in `tools/oracle/native_run_fixtures.py`. It is a golden
serialization fixture, not a playable run or a personal Steam save. Some model and
property names are intentionally synthetic so all serialized value shapes can be
exercised without requiring particular gameplay scenarios.

It covers all 15 RNG streams, uint64 boundaries, exact decimal values, all seven saved
property buckets, ordered/nested cards, enchantments, relics, potions, modifiers,
maps, history choices, tagged dynamic values, room rewards, and a native drawing packet.

The live `tools/oracle/check_native_run.py` suite compares freshly serialized output
against this capture and checks native member values before and after round trips.
Do not regenerate the capture just to hide a failing comparison; investigate changed
fields and update the pinned format deliberately.
