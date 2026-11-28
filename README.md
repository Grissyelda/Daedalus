# Daedalus

An all-in-one game editor and engine built on id Tech 4: one program that opens into an editor where you build, light, script and play a level without ever needing to leave.

Daedalus is a fork of [RBDOOM-3-BFG](https://github.com/RobertBeckebans/RBDOOM-3-BFG), a modernized Doom 3 BFG Edition source port. That gives us DX12 and Vulkan through NVRHI, physically based rendering, baked global illumination, soft shadows and glTF 2 model support out of the gates.

> **Status: pre-alpha.** You can block out a level, light it, place things, make reusable pieces, bring in models and play it in the same window, with live edits while the game is paused. Logic, AI, dialogue and the other gameplay systems aren't built yet, so it isn't ready to make a whole game with.

## Goals

- **One executable.** It starts in the editor, and Play runs the game in the same window. Shipped games are built without the editor.
- **Uniquely approachable tools.** Brush blockout and mesh placement, a property inspector, a content browser, node-graph scripting, a conversation editor and a visual UI editor, with undo everywhere and readable error messages. Think Project Spark or Dreams, but with a much higher ceiling. Every system has three levels: **Create**, **Understand** and **Control**. For example, behavior starts as simple When/Do blocks, which open up into node graphs, which open up into Doom Script.
- **Live iteration.** Changes to maps, scripts, definitions, UI and imported models show up in the running game within seconds.
- **Standalone.** New projects need no Doom 3 game data.
- **Two looks.** A stylized material path and a PBR material path, switchable per project and per material.
- **Platforms:** Windows, Linux and Steam Deck (Steam Deck is the performance floor).
- **Single-player** games.

## What works today

- **Building levels:** brushes, seven drag-in shapes, carving, cutting, corner editing, texture lock, per-face painting, a 3D view and a top-down plan view, with undo for everything.
- **Things:** lights with presets, doors, player starts and every placeable definition, edited through Inspector cards.
- **Pieces (prefabs):** linked copies with per-copy overrides, edited in place, nestable.
- **Models:** drag in a glTF, GLB or OBJ file and it becomes a placeable model with PBR materials, updating by itself when you save it again in Blender.
- **Play:** in the same window, from the player start or the camera; pausing brings the editor back and edits flow into the game, each marked as in the game, next time you play, or rebuilt when you resume.
- **Background builds** with leak finding.
- **Problems:** errors, warnings and notes about the level, each pointing at the thing at fault with a next step and often a one-click fix. They show in the Outliner, the view and the Inspector too; Play asks first when there are errors, and **Check every level** checks the whole project.
- **Help:** a page for every panel and tool, and lessons that tick off as you go.

## Next up

- A material editor with PBR presets, a view of the level's areas and portals, and light looks with one-click baked lighting.
- Then behavior: When/Do rule cards on objects that open up into node graphs and Doom Script.

## Building

Clone with submodules:

```
git clone --recurse-submodules https://github.com/Grissyelda/Daedalus.git
```

You need CMake 3.25 or newer, [ISPC](https://github.com/ispc/ispc/releases) on your `PATH`, and:

- **Windows:** Visual Studio 2022. For the Vulkan renderer, the [Vulkan SDK](https://vulkan.lunarg.com/) as well; otherwise add `-DUSE_VULKAN=OFF` to build DirectX 12 only.
- **Linux / Steam Deck:** GCC or Clang, Ninja, SDL2, OpenAL and the Vulkan headers and loader (on Ubuntu: `ninja-build libsdl2-dev libopenal-dev libvulkan-dev`), plus [DXC](https://github.com/microsoft/DirectXShaderCompiler/releases) for compiling shaders.

Then, from the `neo` folder, pick a preset (`cmake --list-presets` shows them all):

| Preset | Builds |
| --- | --- |
| `windows-vs2022` | Windows, with the editor |
| `windows-game` | Windows, game only (what you ship) |
| `linux-release` | Linux, with the editor |
| `linux-debug` | Linux, with the editor and debug info |
| `steamdeck-release` | Steam Deck / Linux, game only |

```
cmake --preset linux-release
cmake --build --preset linux-release
```

Shaders compile into `content/renderprogs2` as part of the build. Every push also builds Windows and Linux packages on GitHub Actions (see the **Build** workflow's artifacts).

The editor has scripted tests that drive it through console commands and check what it prints:

```
bash scripts/test/run_editor_tests.sh build/linux-release/Daedalus
```

Each test is a folder in `scripts/test/editor/`. CI runs them on every push.

## Running

Run the executable from the repository root (or a package folder), next to `content/`. It opens the editor:

- The level fills the middle. The **tool dock** at the bottom has five tabs: **Build** (move, turn, resize, draw, cut, corners, and shapes to drag in), **Paint** (click surfaces to paint them), **Logic** (doors and the player start), **Light** and **Kits** (ready-made things to drop in). Keys 1 to 5 switch tabs; hold **Space** over the level for a wheel of every tool.
- The **Outliner**, **Inspector** (settings for the selection, as cards, with the raw keys under Advanced), **Content** and **Problems** (what's wrong with the level and how to fix it) panels dock around it. Drag a panel's tab to rearrange them, or out of the window onto another monitor (`editor_detachablePanels 0` turns that off). **View → Reset layout** puts them back.
- **Play** (top centre, **F5**) saves and runs the open level. **Esc** pauses it and brings the editor back with the game still loaded: changes go into the game, and brush changes rebuild the level around you when you carry on (**F5**). **F10** stops.
- Levels are compiled by `rbdmap`, which ships next to the editor and runs in the background (the bar left of Play shows how far it's got), so the editor never freezes. Play builds when the level needs it; **File → Build** (**Ctrl+B**) builds without playing. What the build says shows up in **Problems**, with the full build log a click away.
- **Models:** drag a `.glb`, `.gltf` or `.obj` file onto the window, use **Kits → Bring in**, or drop it in the project's `import/` folder. It's copied into `models/imported/` with its textures and materials, and appears as a tile in **Kits**. Saving the source file again updates the model in the level. The Inspector's Model card sets its size (pick what program it was made in) and which way it faces.
- **Pieces** (prefabs): select things and press **Make piece** to save them in `maps/prefabs/`, then place copies from **Kits**. Copies stay linked: **Edit piece** changes the piece right where a copy sits (the rest of the level turns to ghosts); then **Save to every copy** changes the piece and every copy, or **Save to this copy** keeps the changes in that copy alone. Undo undoes changes to pieces too, and the previous version of a piece is kept as `maps/prefabs/<name>.bak`. A double-click picks one part of a copy; changing it changes only that copy, and the Inspector marks it with **Revert** and **Apply to all copies**. Pieces can contain pieces.
- **File → New Project** creates a project folder from `template/` and asks whether it starts with the stylized or the PBR look. **File → Open Project** switches to another project.
- Start with `+set editor 0` to run as the game, without the editor. `editor_uiScale` makes the editor's text and controls bigger or smaller.
- **Help** (**F1**) has a page for every part of the editor, reached from the **?** buttons on panels, cards and tools, and lessons that tick themselves off as you go, with **Show me** to make the right button glow. Pages and lessons are Markdown files in `content/editor/help/`; a project can add its own, or replace the engine's, in its own `editor/help/` folder (the file format is described in `neo/tools/imgui/editor/EditorHelp.h`).

`content/` holds the engine's own content: default materials, the `daedalussans` font (generated from DejaVu Sans), prototype grid textures, the default player and a test room. The editor's UI fonts are [Inter](https://rsms.me/inter/) (SIL Open Font License) and [Lucide](https://lucide.dev) icons (ISC license), in `content/editor/fonts`. The scripts in `scripts/content/` regenerate the fonts, textures and test map.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). In short: enable the hygiene hook, and commit with your GitHub noreply address.

## License

Daedalus's code is licensed under the GNU General Public License v3 with id Software's additional terms. See [LICENSE.md](LICENSE.md) and [LICENSE_EXCEPTIONS.md](LICENSE_EXCEPTIONS.md) for third-party components under other licenses.

Games made with Daedalus must publish their engine and game code under the same terms. Their art, maps, audio and writing are not covered by the GPL and can stay proprietary.

Daedalus contains no Doom 3 game data, which remains covered by its original EULA.

## Credits

Daedalus stands on the work of id Software (Doom 3 and Doom 3 BFG Edition), Robert Beckebans and the RBDOOM-3-BFG contributors, and the wider id Tech 4 community. See [docs/UPSTREAM.md](docs/UPSTREAM.md) for the exact upstream version.
