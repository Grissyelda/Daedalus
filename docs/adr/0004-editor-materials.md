# 0004 Materials made in the editor: one file each, with a settings line the editor reads

Status: Accepted
Date: 2026-11-27

## Context

The Material panel lets people make and change materials without writing material text. The engine reads Doom 3 material declarations (.mtr); people may also open them in a text editor. There must be one canonical representation, the editor must be able to read back what it wrote, and undo must cover material changes.

## Decision

Each material made in the editor is one file, `materials/<name>.mtr`, holding one declaration called `textures/<name>`. Its pictures live in `textures/<name>/`.

Inside the declaration, the first line is a comment holding the settings: `// daedalus-material preset "..." tint "r g b" roughness .. metalness .. glow .. transparency .. opacity .. twosided .. shadows .. solid .. surface ".." <slot>_picture ".."`. That line is canonical. The editor reads only it, and writes everything else in the declaration from it: the base colour stage, normal map, an `rmaomap` made from the roughness, metal and occlusion pictures (or the slider values), the glow stage, and the surface type, two-sidedness, shadow and collision keywords. A see-through material is written as an unlit `translucent` blend stage.

Pictures put into a material are converted to PNG and named `<slot>_<hash>.png`, the hash being of the picture's pixels. The `rmao` picture is named the same way.

A change writes the file and reloads it, as one undo step (the file's text before and after is recorded with the level's undo; see 0003). Undo writes the old text back and reloads changed declarations.

## Consequences

- People can edit the settings line by hand; anything else they change in the file is overwritten by the next change made in the panel. The file's header says so.
- Since picture names change when their content does, undo always finds the pictures an older version used, and nothing needs reloading but the declaration. Old pictures stay in the folder; a later clean-up step can remove unused ones.
- Hand-written and imported materials aren't edited in place; the panel offers an editable copy.
- New pictures are first referenced after the level loaded, so the engine now sends queued images to the GPU at the end of each frame (`common_frame.cpp`); before, drawing one stopped the engine.
