/*
===========================================================================

Daedalus editor: making and changing materials

Copyright (C) 2026 the Daedalus authors

This file is part of Daedalus, which is based on the Doom 3 BFG Edition GPL
Source Code and RBDOOM-3-BFG. It is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version.

===========================================================================
*/

#ifndef __EDITOR_MATERIALS_H__
#define __EDITOR_MATERIALS_H__

/*
===============================================================================

	Materials made in the editor

	Each material is one file, materials/<name>.mtr, holding one material
	called textures/<name>, with its pictures in textures/<name>/:

		color.png  normal.png  rough.png  metal.png  ao.png  glow.png
		rmao.png   (roughness, metal and occlusion put together for the
		            renderer; made from the three above or from the values)

	The first line inside the material is the canonical settings:

		// daedalus-material preset "Stone" roughness 0.85 metal 0 ...

	The editor reads that line and writes everything below it. Editing the
	line by hand works; editing the rest by hand is undone on the next change
	in the editor (see docs/adr/0004).

	Pictures dropped on the editor become a material: which picture is which
	is worked out from the file names (wall_color.png, wall_Normal.jpg,
	wall_roughness.png ...), the name from what they share, and the preset
	from the name and the pictures.

===============================================================================
*/

namespace DaedalusEditor
{
namespace Materials
{

enum slot_t
{
	SLOT_COLOR,
	SLOT_NORMAL,
	SLOT_ROUGH,
	SLOT_METAL,
	SLOT_AO,
	SLOT_GLOW,
	SLOT_COUNT
};

enum transparency_t
{
	SEE_NONE,			// solid
	SEE_CUTOUT,			// see-through where the colour picture's alpha is low
	SEE_THROUGH			// partly see-through all over, like glass (not lit)
};

struct settings_t
{
	idStr		preset = "Custom";
	idVec3		tint = idVec3( 1.0f, 1.0f, 1.0f );
	float		roughness = 0.7f;		// without a roughness picture
	float		metal = 0.0f;			// without a metal picture
	float		glow = 0.0f;			// how bright the glow picture (or the colour) shines
	int			transparency = SEE_NONE;
	float		opacity = 1.0f;			// for SEE_THROUGH
	bool		twoSided = false;
	bool		shadows = true;
	bool		solid = true;			// the player bumps into it
	idStr		surface = "none";		// for footsteps and impacts: stone, metal, wood ...
	idStr		pictures[SLOT_COUNT];	// project paths (textures/<name>/color.png), empty for none
};

struct preset_t
{
	const char*	name;
	const char*	hint;
	float		roughness;
	float		metal;
	float		glow;
	int			transparency;
	float		opacity;
	bool		twoSided;
	const char*	surface;
};

const preset_t*	Presets( int& count );
void			ApplyPreset( settings_t& s, const char* preset );

const char*		SlotLabel( slot_t slot );			// "Colour", "Normal" ...
const char*		SlotKey( slot_t slot );				// "color", "normal" ... (file names and settings)
const char*		SlotHint( slot_t slot );

// the surface types the game knows, for the Surface setting
const char**	SurfaceTypes( int& count );

bool			IsPictureFile( const char* path );

// which slot a picture is for, from its name; known is false when it's only a guess (colour)
slot_t			GuessSlot( const char* fileName, bool& known );

// a material name from a picture's file name, with the slot word taken off
idStr			NameFromPicture( const char* fileName );

idStr			DeclName( const char* name );		// textures/<name>
idStr			FilePath( const char* name );		// materials/<name>.mtr
idStr			PictureFolder( const char* name );	// textures/<name>
idStr			UniqueName( const char* base );		// a name no material or file uses yet

// the editor's name for a material it made ("" if it didn't make it)
idStr			EditableName( const char* declName );

bool			Load( const char* name, settings_t& s );

// write the material (and its rmao picture); before/after are the file's text, for undo
bool			Save( const char* name, const settings_t& s, idStr& error, idStr* before = NULL, idStr* after = NULL );

// a new material from a preset, with no pictures
bool			Create( const char* name, const char* preset, idStr& error );

// put a picture (any file the editor can read) in a slot, as a .png in the material's folder
bool			SetPicture( const char* name, settings_t& s, slot_t slot, const char* osPath, idStr& error );

// make the bumps (normal) and/or roughness pictures from the colour picture; bumps and roughness are 0..1
bool			MakeFromColor( const char* name, settings_t& s, float bumps, float roughness, bool makeNormal, bool makeRough, idStr& error );

// pictures dropped together: one material each set; returns the names made
bool			CreateFromPictures( const idStrList& osPaths, idStrList& made, idStr& message );

// an editable material made from any other material's pictures
bool			MakeEditableCopy( const char* declName, idStr& name, idStr& error );

// load the material file again (after an edit, or after undo put it back)
void			Reload( const char* name, bool picturesChanged );

// a material that draws a picture flat, for thumbnails in the editor's panels
const idMaterial* Thumbnail( const char* declName );

// faces in the map using the material
int				CountUses( const idMapFile* map, const char* declName );

// files dropped on the window that are pictures (the rest go to the model importer)
void			QueueDroppedPicture( const char* osPath );
bool			TakeDroppedPictures( idStrList& osPaths );	// a batch, once nothing's been dropped for a moment

} // namespace Materials
} // namespace DaedalusEditor

#endif
