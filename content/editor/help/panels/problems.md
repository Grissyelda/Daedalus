@order 4
# Problems

Problems checks the open level as you work and lists what would go wrong in the game. Each problem says what's wrong, where, and what to do about it.

## How bad it is

- **Errors** break the game, or leave something out of it entirely: no player start, a thing whose type doesn't exist, two things with the same name.
- **Warnings** make something look or behave wrong: a material or picture that isn't there (it shows as a checkerboard), a model that's missing, a link to something that doesn't exist, a copy of a piece whose piece has gone.
- **Notes** are worth knowing, but nothing is broken: what bringing a model in couldn't do, or a model whose source file has moved.

The panel's tab shows how many there are, with the worst one's icon.

## Finding and fixing

- Click a problem's text (or **Show**) to select the thing and look at it.
- Many problems have a fix button: **Add a player start**, **Rename it**, **Remove the link**, **Paint with** the paint material, **Unlink** a lost piece, or **Show the leak**. Every fix can be undone.
- Things with problems get an icon in the Outliner and a marker in the view; hover either to read what's wrong.
- Select a thing and the Inspector lists its problems at the top, with the same fixes.

## Before you play

If the level has errors, **Play** asks first: **Play anyway**, **Show me** the first error, or **Cancel**. Warnings never stop you.

## Every level

**Check every level** reads every level and piece in the project, saved files and the open level as it is now, and lists those with problems. **Open** one to show and fix them.

## The build

What the last [build](play/building) said shows here too, leaks included. **Build log** shows everything the level compiler printed.
