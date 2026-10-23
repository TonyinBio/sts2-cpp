# Specifications

Use these sheets to understand the simulator design, the files used by comparison tests,
and what has been checked against the installed game.

Shared domain language lives in the root [glossary](../../GLOSSARY.md).
Cross-cutting architectural decisions live in [docs/adr](../adr/), starting with
[complete state versus observations](../adr/0001-separate-complete-state-from-observations.md).
These sheets retain detailed requirements, schemas and source evidence.

## Document roles

| Material | Location and role |
| --- | --- |
| Domain terminology | [GLOSSARY.md](../../GLOSSARY.md); canonical gameplay and parity terms |
| Architectural decisions | [docs/adr/](../adr/); decisions with rationale and source links |
| Intended system | [Full-game design](engine/full-game-simulator-design.md) and [architecture](engine/full-game-simulator-architecture.md); targets and proposed structure, not proof of implementation |
| Normative contracts | [Matching target](engine/matching-contract.md), [decision contract](engine/decision-contract.md), [state inventory](engine/state-inventory.md), and format sheets below |
| Current coverage | [Implementation status](engine/implementation-status.md); implemented subsets and remaining work |
| Reference evidence | Runtime/model audits and state-access probe; limited to their inspected builds and scenarios |
| Future work | GitHub Issues as configured in [issue-tracker.md](../agents/issue-tracker.md); link relevant contracts rather than copying them |

Existing specs remain version-controlled reference documents. Their presence does not
make unfinished work agent-ready. Publish a scoped feature spec to GitHub when its
behavior and verification are settled; retain links to the relevant sheets and evidence.

## Where to start

| I want to… | Read |
| --- | --- |
| Understand the intended full-game simulator | [Full-game design](engine/full-game-simulator-design.md) |
| See the proposed files, classes and memory ownership | [Full-game architecture](engine/full-game-simulator-architecture.md) |
| See the pinned full-game matching target, decisions and state inventory | [Matching contract](engine/matching-contract.md) |
| Understand run and combat snapshots | [Run parity state](run-parity-spec.md) |
| Play while recording actions and states | [Interactive recording](interactive-recording-spec.md) |
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
| [Run parity state](run-parity-spec.md) | Native run and combat snapshot contract; bounded starter exporter | [run-parity-v1.schema.json](../../data/schemas/run-parity-v1.schema.json) |
| [Action recording](action-recording-spec.md) | Implemented action format; selection actions have limited adapter support | `data/schemas/actions-v1.json` |
| [Oracle recording](oracle-recording-spec.md) | Implemented reference recorder for the audited Ironclad/Shrinker encounter | `data/schemas/states-v1.json` |
| [Real-game save JSON](real-game-save-json-spec.md) | Native v20 save structure, with separate saving/loading checks | [serializable-run-v20.schema.json](../../data/schemas/serializable-run-v20.schema.json) |

The [full-game design](engine/full-game-simulator-design.md) describes the intended system.
It does not describe the current implementation's coverage. The probe and audit sheets
record evidence for the inspected build and scenarios; they do not establish support
for other mechanics or game versions.

## Terms used in these sheets

Use the [glossary](../../GLOSSARY.md) for domain terms. Format sheets define their
wire fields and local terminology. In these sheets, an adapter reads engine state or
sends actions in the test format; an envelope is the outer recording structure carrying
identity and source information alongside snapshots. RNG means random-number generator;
JSON Pointer identifies a JSON value, for example `/players/0/deck/3`.

## Reading and maintaining the sheets

**Must** and **must not** mark requirements. Examples show how the format is used;
audit results describe what was observed. Keep open questions and unsupported cases
explicit rather than guessing defaults or expanding support based on one passing test.

These sheets follow the [MindStudio software-spec guide](https://www.mindstudio.ai/blog/how-to-write-a-software-spec):
start with a clear overview, describe data and behavior precisely, and state error cases
and unresolved questions. Use plain language; keep exact code names where they identify
fields, commands, or game behavior.
