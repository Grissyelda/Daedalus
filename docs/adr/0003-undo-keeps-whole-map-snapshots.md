# 0003 Undo keeps whole-map snapshots, plus the text of files a change wrote

Status: Accepted
Date: 2026-11-27

## Context

Every editor operation must be undoable, including ones that touch many brushes and entities at once (carving, pieces, repainting a material across a level) and ones that write other files (saving a piece). Per-operation undo code is easy to get wrong and drifts as operations are added.

## Decision

Each change is bracketed by `Document::BeginChange( label )` and `EndChange()`. BeginChange takes a snapshot of the whole map as text (the same format as a saved .map, in memory). Undo and redo swap the current map with the stored text, and restore the selection that went with it.

A change that writes another file (a piece, for example) records that file's text before and after with `RecordFileWrite`. Undo and redo write it back, and remove a file the change created.

The history keeps at most 200 steps and 256 MB of snapshot text, dropping the oldest first. A change that altered nothing is dropped (`CancelChange`), and continuous edits such as a drag make a single step.

## Consequences

- Any operation is undoable as soon as it's wrapped in BeginChange/EndChange; nothing else is needed.
- Undo can never get out of step with the map, since it restores the map itself.
- Memory grows with map size times history length; very large maps will need a cheaper scheme (diffs or per-entity snapshots), which would be a new record.
- Files outside the map are only covered when the change records them; import settings aren't yet.
- After every change, an `afterChange` hook runs (pieces use it to track overrides), so hooks must be quick.
