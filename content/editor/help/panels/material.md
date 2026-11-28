@order 5
# Material panel

Shows one material: a picture of it, how many faces use it in the level, and, for materials made in Daedalus, everything about it. Open it with **Edit** on the [Paint](tools/paint) tab, the settings button on a [Material card](cards/material), or by double-clicking a material in **Content**.

## Making a material

- **Drop pictures on the editor.** A set of pictures (a colour picture, a normal map, a roughness map ...) becomes a material, with each picture in the right place. Which is which comes from the file names, the way asset sites and Blender name them: `brick_wall_color.png`, `brick_wall_normal.png`, `brick_wall_roughness.png`. The material is named after what they share (`brick_wall`) and starts from a preset that fits the name.
- **New** (here, on the Paint tab, or in Content) makes one from a preset, with no pictures yet.
- Height, opacity and specular maps aren't used yet; Daedalus says so when you drop them.

## Changing it

Changes show in the level as soon as you let go of a control, and **undo** puts them back.

- **Like**: a preset (Stone, Brick, Wood, Painted metal, Bare metal, Plastic, Fabric, Ground, Foliage, Glass, Water, Glowing). It sets how rough and shiny it is, whether you can see through it, and how it sounds underfoot. Start there, then tweak.
- **Pictures**: colour, normal, roughness, metal, occlusion and glow. Drop pictures on the panel to put them into this material, or **Choose** one for a slot. A slot without a picture uses the slider below it instead.
- **Look**: the tint, roughness (0 smooth and shiny, 1 rough and matte), metal (1 for bare metal only), glow, and see-through: no, cut out (using the colour picture's transparency, for leaves and fences), or see-through all over (glass, water; these aren't lit).
- **Behaviour**: what it sounds like when walked on or hit, whether both sides are drawn, whether it casts shadows, and whether things bump into it.

**Paint with it** makes it the paint material; **Put on selection** puts it on everything selected.

## Materials made elsewhere

Materials written by hand, or made by bringing in a model, can't be changed here. **Make an editable copy** makes one that can, with the same pictures; put the copy on surfaces to use it.

## Where it's kept

Each material is one file, `materials/<name>.mtr`, with its pictures in `textures/<name>/`. Near the top of the material is one settings line that the panel reads; it writes everything else.

## One picture is enough

A material only needs a **colour** picture. The **Roughness**, **Metal** and **Glow** sliders stand in for the other pictures. To get bumps too, use **Make bumps and roughness** under the pictures: it reads the light parts of the colour picture as raised and the dark parts as dents, and makes a bumps (normal) picture and a roughness picture from it. **Bumps** sets how deep they look. Undo puts your own pictures back.

Where to get pictures: free ready-made sets from ambientCG or Poly Haven (drop a whole set on the editor and every slot fills in), Krita or GIMP to paint your own, Material Maker to build them from patterns, and ArmorPaint or Blender to paint on a model.
