# Specifications

Use these sheets to understand the simulator design, the files used by comparison tests,
and what has been checked against the installed game.

## Where to start

| I want to… | Read |
| --- | --- |
| Understand the intended full-game simulator | [Full-game design](full-game-simulator-design.md) |
| Understand run and combat snapshots | [Run parity state](run-parity-spec.md) |
| Record player decisions and replay them | [Action recording](action-recording-spec.md), then [Oracle recording](oracle-recording-spec.md) |
| Read or validate a native game save | [Real-game save JSON](real-game-save-json-spec.md) |
| Check what game state the adapter can read | [State-access probe](oracle-state-access-spec.md) |
| Find gameplay data missing from native saves | [Combat runtime audit](combat-runtime-audit.md), then [Model field audit](combat-model-field-audit.md) |

## Current formats and support

The JSON schemas define file structure and value types. The sheets also define rules
that a schema cannot check, such as whether an action is legal or an ID points to the
right object. A file that passes schema validation is not proof that both engines match.

| Sheet | Status and scope | Schema |
| --- | --- | --- |
| [Run parity state](run-parity-spec.md) | Native run and combat snapshot contract; bounded starter exporter | [run-parity-v1.schema.json](schemas/run-parity-v1.schema.json) |
| [Action recording](action-recording-spec.md) | Implemented action format; selection actions have limited adapter support | `tests/parity/schema/actions-v1.json` |
| [Oracle recording](oracle-recording-spec.md) | Implemented reference recorder for the audited Ironclad/Shrinker encounter | `tests/parity/schema/states-v1.json` |
| [Real-game save JSON](real-game-save-json-spec.md) | Native v20 save structure, with separate saving/loading checks | [serializable-run-v20.schema.json](schemas/serializable-run-v20.schema.json) |

The [full-game design](full-game-simulator-design.md) describes the intended system.
It does not describe the current implementation's coverage. The probe and audit sheets
record evidence for the inspected build and scenarios; they do not establish support
for other mechanics or game versions.

## Terms used in these sheets

| Term | Meaning here |
| --- | --- |
| Parity | The game and simulator produce matching state or behavior for the same inputs. Each sheet states what is compared. |
| Oracle / reference | The installed game, run through the `sts2-cli` adapter, used as the comparison source. |
| Adapter | Code that reads an engine's state or sends actions to it in the test format. |
| Snapshot | A record of game state at one point in play. |
| Boundary | A capture point: the game is ready for input, waiting for a choice, or finished. |
| Model ID | The ID of a game definition, such as a card type. |
| Instance ID | The ID of one particular card, creature, power, or relic. Two copies of a card have different instance IDs. |
| RNG | Random-number generator. Its saved state determines its future random values. |
| Native | The game's own data or format. |
| Profile | A named format or supported set of mechanics, such as `actions-v1`. |
| Envelope | The outer part of a recording file, containing IDs and source information alongside snapshots. |
| JSON Pointer | A path to a value in a JSON document, such as `/players/0/deck/3`. |

## Reading and maintaining the sheets

**Must** and **must not** mark requirements. Examples show how the format is used;
audit results describe what was observed. Keep open questions and unsupported cases
explicit rather than guessing defaults or expanding support based on one passing test.

These sheets follow the [MindStudio software-spec guide](https://www.mindstudio.ai/blog/how-to-write-a-software-spec):
start with a clear overview, describe data and behavior precisely, and state error cases
and unresolved questions. Use plain language; keep exact code names where they identify
fields, commands, or game behavior.
