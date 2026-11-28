# 0002 Imported models: the source and a settings file are canonical, everything else is generated

Status: Accepted
Date: 2026-11-27

## Context

Models come from Blender and other tools as glTF (.glb, .gltf) or OBJ files, with pictures beside them or inside them, in different units and with different "up" axes. RBDOOM-3-BFG's loaders are strict: the glTF reader needs buffers and bufferViews before anything uses them and resolves external files from the virtual file system root, and the OBJ reader only accepts triangles with position, texture and normal indices on every corner.

The editor must keep a model up to date when its source file changes, without the user re-entering settings.

## Decision

For a model named `<name>`, two things are canonical:
- the source file, wherever it lives (an OS path, or a path inside the project);
- `models/imported/<name>/<name>.import`, the settings: source, the program it was made in (units), size, turn, picture hashes and notes from the last import, and an `options - scale S - rotate T` line the engine's glTF loader reads.

Everything else is generated and rewritten on each import:
- `models/imported/<name>/<name>.glb`, one self-contained GLB with all buffers in one chunk, pictures embedded and buffers written first; or `<name>.obj`, converted to Z-up as (x, -z, y), scaled, triangulated, every corner `v/vt/vn`, one object per material;
- `models/imported/<name>/textures/*.png`, including an `_rmao` map built from metal/roughness and occlusion;
- `materials/imported_<name>.mtr`, with materials named `<model path without extension>/<material>`, which the glTF model loader looks up first.

The editor polls the source and reimports it shortly after it stops changing. Only what changed is reloaded: materials, single pictures, and the model.

## Consequences

- Deleting the generated files loses nothing; the next import makes them again.
- The game never reads the source file, so shipped games don't need it.
- Pictures are rewritten only when their source hash changes, which keeps reloads quick.
- FBX and skinned models with animation aren't covered yet; skinned models import standing still.
- Undo doesn't cover import settings yet (see 0003).
