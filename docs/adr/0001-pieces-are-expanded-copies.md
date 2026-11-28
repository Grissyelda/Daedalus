# 0001 Pieces are expanded into the level as tagged copies

Status: Accepted
Date: 2026-11-27

## Context

Pieces (prefabs) are reusable groups of brushes and things, such as a doorway or a house. Copies must stay linked to their piece, allow per-copy changes, nest inside other pieces, and work with the existing level compiler (dmap) and game without either knowing pieces exist.

The two usual approaches are a reference that the compiler and game expand at load time, or a copy expanded into the level by the editor.

## Decision

A piece is an ordinary map file in `maps/prefabs/<name>.map`, built around its own origin.

Placing it adds to the level:
- an anchor entity (`func_prefab`) holding the piece's name and placement (`prefab`, `origin`, `angle`, `prefab_placed`), which the game inhibits and dmap doesn't flood from (`noFlood`);
- a full copy of the piece's brushes and entities, each tagged with `prefab_owner` (the anchor's name) and `prefab_part` (its name inside the piece). Brushes carry the tags as brushDef3 keys. Member names are `<copy>_<part>`, and links between parts are renamed to match.

What one copy changes is an override: the changed settings are listed in `prefab_overrides` on that thing ("brush" for shape and paint), and parts deleted from one copy are listed in the anchor's `prefab_removed`. After every change, the editor compares each copy with a fresh expansion of its piece and records the differences. A copy whose anchor moved is rebuilt instead. Levels bring their copies up to date when they open.

A piece's own changes to the pieces inside it are stored as overrides in the piece's file and applied as plain values on expansion, to a depth of 8.

## Consequences

- dmap and the game need no changes: they see ordinary brushes and entities. A level with pieces builds and plays even if the piece files are gone.
- Levels are larger, since every copy is written out in full.
- Keeping copies in step is the editor's job; editing a level's text by hand can confuse the override tracking.
- A deleted part of a nested copy is only remembered one level deep.
- Writing a piece file is part of the undo step (see 0003), so undo puts the old piece back.
