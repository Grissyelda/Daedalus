/*
===========================================================================

Daedalus: finding the id Tech 4 games the user has installed

Copyright (C) 2026 the Daedalus authors

This file is part of Daedalus, which is based on the Doom 3 BFG Edition GPL
Source Code and RBDOOM-3-BFG. It is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version.

===========================================================================
*/

#ifndef __GAMELIBRARY_H__
#define __GAMELIBRARY_H__

/*
===============================================================================

	Your games

	Finds id Tech 4 games the user has installed, so their data can be read,
	edited and played. Daedalus never ships any of it: the data stays in the
	user's own install and is only read from there.

	A game is found by the files its install always has. Places searched:
	folders the user added (a game's folder, or a folder of games), Steam
	libraries and GOG installs.

===============================================================================
*/

enum gameKind_t
{
	GAME_DOOM3_BFG,		// DOOM 3 BFG Edition: .resources files
	GAME_DOOM3,			// DOOM 3 (2004): base/*.pk4
	GAME_DOOM3_ROE,		// Resurrection of Evil, inside a DOOM 3 install: d3xp/*.pk4
	GAME_QUAKE4,		// q4base/*.pk4
	GAME_PREY,			// base/*.pk4 with Prey's own files
	GAME_WOLF2009,		// Wolfenstein (2009): SP/base/*.pk4
	GAME_KIND_COUNT
};

enum gameSupport_t
{
	SUPPORT_PLAY_EDIT,	// Daedalus plays and edits it
	SUPPORT_EDIT_PORT,	// Daedalus edits it; playing goes through a port the user installs
	SUPPORT_BROWSE		// Daedalus can look inside it; editing comes later
};

struct installedGame_t
{
	gameKind_t	kind;
	idStr		folder;		// the install's top folder, forward slashes
	idStr		data;		// the folder that holds the game's data, e.g. <folder>/base
	idStr		foundBy;	// "your folder", "Steam", "GOG"
};

namespace GameLibrary
{
const char*		Id( gameKind_t kind );			// "doom3", "quake4", ... (stable, used in files)
const char*		Name( gameKind_t kind );		// "DOOM 3"
gameSupport_t	Support( gameKind_t kind );
const char*		SupportLabel( gameSupport_t support );	// "Play and edit"
const char*		SupportText( gameKind_t kind );			// one sentence for the user

// adds what is installed in this one folder (DOOM 3 adds Resurrection of Evil too)
int				Identify( const char* folder, const char* foundBy, idList<installedGame_t>& out );

// every game that can be found now; searches again when Refresh() is called
const idList<installedGame_t>&	Games();
void			Refresh();

// folders the user added; kept in the save folder, never in a project
const idStrList&	Folders();
int				AddFolder( const char* folder );	// how many games it holds; 0 means nothing was added
bool			RemoveFolder( const char* folder );

// a project over a game: <engine folder>/mod_<id>/project.daedalus naming the game's data folders.
// Its own files are the mod; the game's stay where they are. Made if it's not there yet.
bool			CanOpen( gameKind_t kind );
// the game this project reads, if it is a mod project ("mod_doom3"); GAME_KIND_COUNT if none
gameKind_t		ProjectGame( const char* project );
idStr			ModProjectName( const installedGame_t& game );
// the level a new game starts on ("" for the game's own menu)
const char*		FirstLevel( gameKind_t kind );
bool			MakeModProject( const installedGame_t& game, idStr& project, idStr& error );

void			RegisterCommands();
}

#endif
