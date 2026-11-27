/*
===========================================================================

GrizzTech4 editor: bringing models in

Copyright (C) 2026 the GrizzTech4 authors

This file is part of GrizzTech4, which is based on the Doom 3 BFG Edition GPL
Source Code and RBDOOM-3-BFG. It is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version.

===========================================================================
*/

#ifndef GRIZZ_EDITOR_IMPORT_H
#define GRIZZ_EDITOR_IMPORT_H

/*
===============================================================================

	Model import

	A model file from anywhere (a .glb, .gltf or .obj, say from Blender) is
	brought into the project as one folder:

		models/imported/<name>/<name>.glb	the model (a .gltf becomes a .glb with
											everything inside it; an .obj is turned
											to stand up the game's way)
		models/imported/<name>/textures/	its textures as .png files
		models/imported/<name>/<name>.import	where it came from and its settings
		materials/imported_<name>.mtr		a material for each of its materials

	The file it came from is watched: saving it again (from Blender, say)
	updates the model in the editor and in a paused game a moment later.
	Files saved into the project's "import" folder come in by themselves.

===============================================================================
*/

namespace GrizzEditor
{
namespace Import
{

// what a model was made in, which sets how big it comes out
enum madeIn_t
{
	MADE_IN_METERS,			// glTF and most tools (the default)
	MADE_IN_CENTIMETERS,	// some FBX and Unreal exports
	MADE_IN_INCHES,			// game units: 1 unit is an inch
	MADE_IN_CUSTOM,
	MADE_IN_COUNT
};

const char*	MadeInLabel( madeIn_t madeIn );
float		MadeInScale( madeIn_t madeIn, float custom );

struct model_t
{
	idStr		name;			// the folder name under models/imported
	idStr		model;			// the model path the game loads, e.g. models/imported/crate/crate.glb
	idStr		source;			// where it came from: an OS path, or one relative to the project
	idStr		sourceType;		// glb, gltf or obj
	madeIn_t	madeIn = MADE_IN_METERS;
	float		customScale = 1.0f;
	float		turn = 0.0f;	// extra turn in degrees, for models that face the wrong way
	int			materials = 0;
	int			textures = 0;
	int			meshes = 0;
	bool		skinned = false;
	idStrList	warnings;		// things the import couldn't do, shown in Problems

	// watching the source
	ID_TIME_T	sourceTime = 0;
	int			sourceLength = 0;
	ID_TIME_T	pendingTime = 0;	// a change was seen at this time and length...
	int			pendingLength = -1;
	int			pendingSince = 0;	// ...this many ms ago; it's imported once it stops changing
	bool		sourceMissing = false;
};

// bring a model in. A file already brought in (the same source) is updated instead.
// model is the path to give a thing's "model" key; message says what happened.
bool			ImportFile( const char* sourcePath, idStr& name, idStr& message );

// bring a model in again from its source, with its current settings
bool			Reimport( const char* name, idStr& message );

// change what a model was made in (its size) or its turn, and update it
bool			SetMadeIn( const char* name, madeIn_t madeIn, float customScale, idStr& message );
bool			SetTurn( const char* name, float degrees, idStr& message );

// everything brought in so far (read from models/imported when the editor starts)
const idList<model_t>& Models();
void			Refresh();
const model_t*	FindByName( const char* name );
const model_t*	FindByModel( const char* modelPath );

// is this the path of a model the game can load?
bool			IsModelFile( const char* path );

// the project's import folder, as an OS path; it's made if it isn't there
idStr			ImportFolder();

// call once a frame: watches sources and the import folder, and does the work
// queued by them and by dropped files. Returns true when a model changed.
bool			Poll();

// what the last Poll() did, for the status line ("" when nothing)
const char*		LastActivity();

// files dropped on the window from the desktop; safe to call from any thread
void			QueueDroppedFile( const char* osPath );
bool			TakeDroppedFile( idStr& osPath );

// open a folder in the system's file browser
void			ShowFolder( const char* osPath );

// ask the system for a model file to bring in; false if cancelled or not possible here
bool			BrowseForModel( idStr& osPath );

} // namespace Import
} // namespace GrizzEditor

#endif
