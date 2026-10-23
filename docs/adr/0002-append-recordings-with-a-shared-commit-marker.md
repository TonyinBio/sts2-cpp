---
status: accepted
---

# Append separate action and state recordings with one commit marker

The recorder will append to separate `actions.jsonl` and `states.jsonl` files in
one directory instead of repeatedly validating and rewriting complete recording
history. New actions and snapshots will be validated incrementally, reusing the
existing definitions and retaining session metadata for cross-record checks.

An atomically replaced `commit.json` will identify the committed byte length of
each file. Every action, including actions replayed while loading, is committed
before the next input: the recorder writes and synchronizes the new data,
atomically replaces the marker, and synchronizes its directory. Recovery uses
only those committed prefixes and truncates any uncommitted tails before
further appends. This preserves a matched action/state prefix without rotating
two complete copies or combining actions and states in one journal.

Manual save and normal exit will export the existing complete `actions.json`,
`states.json`, and `reference.json` files. An explicit export command will also
materialize the committed journal prefix after interruption. Existing consumers
and complete-document validation remain supported through those exports;
ordinary per-action recording does not process previously committed history.

Existing complete action/state JSON pairs remain accepted by replay and
validation tools. The recorder's `--load` continues to accept the existing
upstream action-log JSON save format; loading JSONL recordings is outside this
change's scope.

The design was confirmed before implementation. The
[interactive recording contract](../specs/interactive-recording-spec.md#checkpoints-and-files)
defines the journal records, durability boundary, recovery, and export behavior.
