# Specifications

These documents define the supported combat profile, interchange formats, and reference
verification procedures. JSON schemas under `tests/parity/schema/` define structural
constraints; the specifications also define semantic and execution requirements.

| Specification | Contract | Machine-readable schema |
| --- | --- | --- |
| [Combat state](combat-state-spec.md) | Snapshot fields, identities, ordering, boundaries, and exact comparison | `combat-v1.json` |
| [Action recording](action-recording-spec.md) | Combat setup, ordered player decisions, and replay rules | `actions-v1.json` |
| [Oracle recording](oracle-recording-spec.md) | Reference result envelope, provenance, supported mechanics, and counters | `states-v1.json` |
| [Oracle state access probe](oracle-state-access-spec.md) | Probe procedure, verified build, evidence, and limitations | Not applicable |

Start with the combat state specification for snapshot fields. Use the action and oracle
recording specifications together for replay inputs and expected outputs. The state-access
probe is a bounded verification record; it does not establish support for other mechanics.
