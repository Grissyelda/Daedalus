# Decision records

Short records of the decisions that shape Daedalus's code: what was decided, why, and what follows from it. Read them before changing the parts they cover.

A record is never rewritten once accepted. If a decision changes, add a new record that supersedes the old one and mark the old one's status.

| # | Decision | Status |
|---|---|---|
| [0001](0001-pieces-are-expanded-copies.md) | Pieces are expanded into the level as tagged copies | Accepted |
| [0002](0002-model-import-pipeline.md) | Imported models: the source and a settings file are canonical, everything else is generated | Accepted |
| [0003](0003-undo-keeps-whole-map-snapshots.md) | Undo keeps whole-map snapshots, plus the text of files a change wrote | Accepted |
| [0004](0004-editor-materials.md) | Materials made in the editor: one file each, with a settings line the editor reads | Accepted |

## Template

```
# NNNN Title: the decision in a few words

Status: Proposed | Accepted | Superseded by NNNN
Date: YYYY-MM-DD

## Context
What problem this solves, and the forces at play.

## Decision
What we do, stated plainly.

## Consequences
What follows: what gets easier, what gets harder, the limits we accept.
```
