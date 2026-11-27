@order 5
@alias models import
# Models

Bring in models made in Blender or anywhere else, and place them like any other thing. **.glb** and **.gltf** (glTF 2) and **.obj** files work. Their pictures and materials come with them.

## Three ways to bring one in

- **Drag the file onto the editor** from your desktop. It lands where you drop it.
- **Kits > Bring in**, then **Browse** (or paste where the file is).
- **Save it into your project's import folder.** It comes in by itself. **Content > Models > Import folder** opens it.

Once a model is in, it's a tile in **Kits** and a row in **Content > Models**: click or drag to add more.

## It updates by itself

The editor watches the file a model came from. Save it again (in Blender, say) and a moment later the model changes in the editor, and in a paused game too. **Update now** on the Model card reads it again straight away.

## Size and facing

glTF files are made in meters, and the editor turns them into game units (1 unit is an inch; the player is about 1.8 m tall). If a model comes out far too big or small, select it and change **Made in** on the Model card: **Meters**, **Centimeters**, **Game units**, or a size of your own. **Turn** turns the model itself a quarter at a time, for models that face the wrong way. To turn just one copy, use **Facing** on the Placement card instead.

## Where it goes

Each model gets a folder, `models/imported/<name>`, with its model file, a `textures` folder and a `.import` file holding its settings, plus a material file, `materials/imported_<name>.mtr`. These are written again whenever the model is brought in again, so make changes in the model's own file, not here.

> Characters with skeletons come in standing still for now; animation comes with the gameplay tools.
