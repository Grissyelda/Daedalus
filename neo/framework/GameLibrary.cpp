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

#include "precompiled.h"
#pragma hdrstop

#include "GameLibrary.h"

idCVar games_steamPath( "games_steamPath", "", CVAR_SYSTEM | CVAR_ARCHIVE, "a Steam folder to look for games in, besides the ones found on their own" );
idCVar games_searchStores( "games_searchStores", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "look for games in Steam and GOG installs" );

static const char* GAMES_FILE = "daedalus_games.txt";

struct gameInfo_t
{
	const char*		id;
	const char*		name;
	gameSupport_t	support;
	const char*		text;
};

static const gameInfo_t gameInfo[GAME_KIND_COUNT] =
{
	{ "doom3bfg",	"DOOM 3 BFG Edition",			SUPPORT_PLAY_EDIT,	"Daedalus plays and edits it: DOOM 3, Resurrection of Evil and The Lost Mission." },
	{ "doom3",		"DOOM 3",						SUPPORT_PLAY_EDIT,	"Daedalus reads its data to play and edit it." },
	{ "d3xp",		"DOOM 3: Resurrection of Evil",	SUPPORT_PLAY_EDIT,	"Daedalus reads its data, on top of DOOM 3's, to play and edit it." },
	{ "quake4",		"Quake 4",						SUPPORT_EDIT_PORT,	"Daedalus edits its levels and assets. To play, install a Quake 4 port and Daedalus hands the level to it." },
	{ "prey",		"Prey (2006)",					SUPPORT_EDIT_PORT,	"Daedalus edits its levels and assets. To play, install a Prey port and Daedalus hands the level to it." },
	{ "wolf2009",	"Wolfenstein (2009)",			SUPPORT_BROWSE,		"Daedalus can look inside its files. Its formats differ more from id Tech 4's, so editing comes later." },
};

static idList<installedGame_t>	games;
static idStrList				userFolders;
static bool						searched = false;
static bool						foldersLoaded = false;

/*
===============================================================================

	Names

===============================================================================
*/

const char* GameLibrary::Id( gameKind_t kind )
{
	return ( kind >= 0 && kind < GAME_KIND_COUNT ) ? gameInfo[kind].id : "";
}

const char* GameLibrary::Name( gameKind_t kind )
{
	return ( kind >= 0 && kind < GAME_KIND_COUNT ) ? gameInfo[kind].name : "";
}

gameSupport_t GameLibrary::Support( gameKind_t kind )
{
	return ( kind >= 0 && kind < GAME_KIND_COUNT ) ? gameInfo[kind].support : SUPPORT_BROWSE;
}

const char* GameLibrary::SupportLabel( gameSupport_t support )
{
	switch( support )
	{
		case SUPPORT_PLAY_EDIT:
			return "Play and edit";
		case SUPPORT_EDIT_PORT:
			return "Edit; play with a port";
		default:
			return "Look inside";
	}
}

const char* GameLibrary::SupportText( gameKind_t kind )
{
	return ( kind >= 0 && kind < GAME_KIND_COUNT ) ? gameInfo[kind].text : "";
}

/*
===============================================================================

	Looking at folders

	Names are matched without regard to case: installs copied between
	Windows and Linux often change it.

===============================================================================
*/

static idStr CleanPath( const char* path )
{
	idStr clean = path;
	while( clean.Length() > 0 && ( clean[0] == ' ' || clean[0] == '\t' ) )
	{
		clean = clean.c_str() + 1;
	}
	clean.StripTrailingWhitespace();
	clean.StripLeading( '"' );
	clean.StripTrailing( '"' );
	clean.BackSlashesToSlashes();
	while( clean.Length() > 1 && clean[clean.Length() - 1] == '/' && !( clean.Length() == 3 && clean[1] == ':' ) )
	{
		clean.CapLength( clean.Length() - 1 );
	}
	return clean;
}

// the child of dir with this name (any case); folders or files
static bool FindChild( const idStr& dir, const char* name, bool folder, idStr& out )
{
	idStrList folders, files;
	if( !Sys_ListFolder( dir.c_str(), folders, files ) )
	{
		return false;
	}
	const idStrList& list = folder ? folders : files;
	for( int i = 0; i < list.Num(); i++ )
	{
		if( list[i].Icmp( name ) == 0 )
		{
			out = dir;
			out.AppendPath( list[i] );
			out.BackSlashesToSlashes();
			return true;
		}
	}
	return false;
}

// "base/pak000.pk4": folders, then a file; "base/" ends in a folder
static bool FindPath( const idStr& root, const char* relative, idStr& out )
{
	idStr at = root;
	idStr rest = relative;
	while( rest.Length() > 0 )
	{
		const int slash = rest.Find( '/' );
		if( slash < 0 )
		{
			return FindChild( at, rest, false, out );
		}
		idStr part = rest.Left( slash );
		rest = rest.c_str() + slash + 1;
		idStr next;
		if( !FindChild( at, part, true, next ) )
		{
			return false;
		}
		at = next;
	}
	out = at;
	return true;
}

static bool Has( const idStr& root, const char* relative )
{
	idStr unused;
	return FindPath( root, relative, unused );
}

static bool HasAnyPk4( const idStr& folder )
{
	idStrList folders, list;
	if( !Sys_ListFolder( folder.c_str(), folders, list ) )
	{
		return false;
	}
	for( int i = 0; i < list.Num(); i++ )
	{
		idStr ext;
		list[i].ExtractFileExtension( ext );
		if( ext.Icmp( "pk4" ) == 0 )
		{
			return true;
		}
	}
	return false;
}

static void AddGame( idList<installedGame_t>& out, gameKind_t kind, const idStr& folder, const idStr& data, const char* foundBy )
{
	installedGame_t& game = out.Alloc();
	game.kind = kind;
	game.folder = folder;
	game.data = data;
	game.foundBy = foundBy;
}

/*
=================
GameLibrary::Identify

The checks go from the most particular to the least: Prey and DOOM 3 both keep
pk4s in base/, so Prey's own files are looked for before DOOM 3's.
=================
*/
int GameLibrary::Identify( const char* folderPath, const char* foundBy, idList<installedGame_t>& out )
{
	idStr folder = CleanPath( folderPath );
	if( Sys_IsFolder( folder.c_str() ) != FOLDER_YES )
	{
		return 0;
	}

	// a data folder picked by mistake: look at the install around it
	idStr last = folder;
	last.StripPath();
	if( last.Icmp( "base" ) == 0 || last.Icmp( "q4base" ) == 0 || last.Icmp( "d3xp" ) == 0 )
	{
		idStr up = folder;
		up.StripFilename();
		if( up.Length() > 0 && up.Icmp( folder ) != 0 )
		{
			const int found = Identify( up, foundBy, out );
			if( found > 0 )
			{
				return found;
			}
		}
	}

	const int before = out.Num();
	idStr data;

	if( FindPath( folder, "base/_common.resources", data ) )
	{
		data.StripFilename();
		AddGame( out, GAME_DOOM3_BFG, folder, data, foundBy );
	}
	else if( FindPath( folder, "q4base/", data ) && ( HasAnyPk4( data ) || Has( folder, "Quake4.exe" ) ) )
	{
		AddGame( out, GAME_QUAKE4, folder, data, foundBy );
	}
	else if( FindPath( folder, "SP/base/assets.pk4", data ) || FindPath( folder, "base/assets.pk4", data ) )
	{
		data.StripFilename();
		AddGame( out, GAME_WOLF2009, folder, data, foundBy );
	}
	else if( FindPath( folder, "base/", data ) && HasAnyPk4( data ) &&
			 ( Has( folder, "PREYDed.exe" ) || Has( folder, "prey.exe" ) || Has( folder, "prey.x86" ) || Has( folder, "base/pak040.pk4" ) ) )
	{
		AddGame( out, GAME_PREY, folder, data, foundBy );
	}
	else if( FindPath( folder, "base/", data ) && Has( folder, "base/pak000.pk4" ) &&
			 ( Has( folder, "Doom3.exe" ) || Has( folder, "doom.x86" ) || Has( folder, "doom.x86_64" ) || Has( folder, "base/pak008.pk4" ) || Has( folder, "base/gamex86.dll" ) ) )
	{
		AddGame( out, GAME_DOOM3, folder, data, foundBy );
		idStr roe;
		if( FindPath( folder, "d3xp/", roe ) && HasAnyPk4( roe ) )
		{
			AddGame( out, GAME_DOOM3_ROE, folder, roe, foundBy );
		}
	}

	return out.Num() - before;
}

/*
===============================================================================

	Searching

===============================================================================
*/

// "path"  "D:\\SteamLibrary" lines of libraryfolders.vdf
static void SteamLibraries( const idStr& root, idStrList& libraries )
{
	libraries.AddUnique( root );
	idStr vdfPath;
	if( !FindPath( root, "steamapps/libraryfolders.vdf", vdfPath ) && !FindPath( root, "config/libraryfolders.vdf", vdfPath ) )
	{
		return;
	}
	FILE* f = fopen( vdfPath.c_str(), "rb" );
	if( f == NULL )
	{
		return;
	}
	idStr text;
	char buffer[4096];
	size_t n;
	while( ( n = fread( buffer, 1, sizeof( buffer ) - 1, f ) ) > 0 && text.Length() < 1024 * 1024 )
	{
		buffer[n] = 0;
		text += buffer;
	}
	fclose( f );

	int at = 0;
	while( ( at = text.Find( "\"path\"", false, at ) ) >= 0 )
	{
		at += 6;
		const int open = text.Find( '"', at );
		if( open < 0 )
		{
			break;
		}
		idStr path;
		int i = open + 1;
		for( ; i < text.Length() && text[i] != '"'; i++ )
		{
			if( text[i] == '\\' && i + 1 < text.Length() )
			{
				i++;
			}
			path += text[i];
		}
		at = i + 1;
		path = CleanPath( path );
		if( path.Length() > 0 && Sys_IsFolder( path.c_str() ) == FOLDER_YES )
		{
			libraries.AddUnique( path );
		}
	}
}

// a game's folder, or a folder of games one level down
static void SearchFolder( const idStr& folder, const char* foundBy, idList<installedGame_t>& out )
{
	if( GameLibrary::Identify( folder, foundBy, out ) > 0 )
	{
		return;
	}
	idStrList children, files;
	if( !Sys_ListFolder( folder.c_str(), children, files ) )
	{
		return;
	}
	children.SortWithTemplate( idSort_PathStr() );
	for( int i = 0; i < children.Num(); i++ )
	{
		idStr child = folder;
		child.AppendPath( children[i] );
		GameLibrary::Identify( child, foundBy, out );
	}
}

static void LoadFolders()
{
	if( foldersLoaded )
	{
		return;
	}
	foldersLoaded = true;
	userFolders.Clear();
	char* buffer = NULL;
	if( fileSystem->ReadFile( GAMES_FILE, ( void** )&buffer ) <= 0 || buffer == NULL )
	{
		return;
	}
	idStrList lines;
	idStr text = buffer;
	fileSystem->FreeFile( buffer );
	text.Replace( "\r", "" );
	int start = 0;
	for( int i = 0; i <= text.Length(); i++ )
	{
		if( i == text.Length() || text[i] == '\n' )
		{
			idStr line = text.Mid( start, i - start );
			start = i + 1;
			line = CleanPath( line );
			if( line.Length() > 0 && line[0] != '#' )
			{
				userFolders.AddUnique( line );
			}
		}
	}
}

static void SaveFolders()
{
	idStr text = "# Folders searched for id Tech 4 games. One per line.\n";
	for( int i = 0; i < userFolders.Num(); i++ )
	{
		text += userFolders[i] + "\n";
	}
	fileSystem->WriteFile( GAMES_FILE, text.c_str(), text.Length(), "fs_savepath" );
}

static bool SameFolder( const idStr& a, const idStr& b )
{
	return a.Icmp( b ) == 0;
}

void GameLibrary::Refresh()
{
	LoadFolders();
	searched = true;
	idList<installedGame_t> found;

	for( int i = 0; i < userFolders.Num(); i++ )
	{
		SearchFolder( userFolders[i], "your folder", found );
	}

	idStrList steamRoots;
	idStrList storeFolders;
	if( games_steamPath.GetString()[0] )
	{
		steamRoots.AddUnique( CleanPath( games_steamPath.GetString() ) );
	}
	if( games_searchStores.GetBool() )
	{
		Sys_GameStoreFolders( steamRoots, storeFolders );
	}
	idStrList libraries;
	for( int i = 0; i < steamRoots.Num(); i++ )
	{
		SteamLibraries( CleanPath( steamRoots[i] ), libraries );
	}
	for( int i = 0; i < libraries.Num(); i++ )
	{
		idStr common;
		if( FindPath( libraries[i], "steamapps/common/", common ) )
		{
			SearchFolder( common, "Steam", found );
		}
	}
	for( int i = 0; i < storeFolders.Num(); i++ )
	{
		SearchFolder( CleanPath( storeFolders[i] ), "GOG", found );
	}

	// the same install can be reached more than one way: keep the first
	games.Clear();
	for( int i = 0; i < found.Num(); i++ )
	{
		bool seen = false;
		for( int j = 0; j < games.Num() && !seen; j++ )
		{
			seen = games[j].kind == found[i].kind && SameFolder( games[j].folder, found[i].folder );
		}
		if( !seen )
		{
			games.Append( found[i] );
		}
	}
}

const idList<installedGame_t>& GameLibrary::Games()
{
	if( !searched )
	{
		Refresh();
	}
	return games;
}

const idStrList& GameLibrary::Folders()
{
	LoadFolders();
	return userFolders;
}

int GameLibrary::AddFolder( const char* folderPath )
{
	LoadFolders();
	idStr folder = CleanPath( folderPath );
	idList<installedGame_t> found;
	SearchFolder( folder, "your folder", found );
	if( found.Num() == 0 )
	{
		return 0;
	}
	bool known = false;
	for( int i = 0; i < userFolders.Num() && !known; i++ )
	{
		known = SameFolder( userFolders[i], folder );
	}
	if( !known )
	{
		userFolders.Append( folder );
		SaveFolders();
	}
	Refresh();
	return found.Num();
}

bool GameLibrary::RemoveFolder( const char* folderPath )
{
	LoadFolders();
	idStr folder = CleanPath( folderPath );
	for( int i = 0; i < userFolders.Num(); i++ )
	{
		if( SameFolder( userFolders[i], folder ) )
		{
			userFolders.RemoveIndex( i );
			SaveFolders();
			Refresh();
			return true;
		}
	}
	return false;
}

/*
===============================================================================

	Commands

===============================================================================
*/

static void ListGames_f( const idCmdArgs& args )
{
	GameLibrary::Refresh();
	const idList<installedGame_t>& list = GameLibrary::Games();
	common->Printf( "Games found: %d\n", list.Num() );
	for( int i = 0; i < list.Num(); i++ )
	{
		const installedGame_t& game = list[i];
		common->Printf( "game: %s | %s | %s | %s | found in %s\n", GameLibrary::Id( game.kind ), GameLibrary::Name( game.kind ),
						GameLibrary::SupportLabel( GameLibrary::Support( game.kind ) ), game.data.c_str(), game.foundBy.c_str() );
	}
	if( list.Num() == 0 )
	{
		common->Printf( "No id Tech 4 games found. Add the folder you installed one to with addGameFolder <folder>.\n" );
	}
}

static void AddGameFolder_f( const idCmdArgs& args )
{
	if( args.Argc() < 2 )
	{
		common->Printf( "usage: addGameFolder <folder>   a game's folder, or a folder of games\n" );
		return;
	}
	const char* folder = args.Args( 1, -1 );
	const int found = GameLibrary::AddFolder( folder );
	if( found == 0 )
	{
		common->Printf( "No id Tech 4 games in %s; nothing was added.\n", folder );
		return;
	}
	common->Printf( "Added %s: %d game%s.\n", folder, found, found == 1 ? "" : "s" );
}

static void RemoveGameFolder_f( const idCmdArgs& args )
{
	if( args.Argc() < 2 )
	{
		common->Printf( "usage: removeGameFolder <folder>\n" );
		return;
	}
	const char* folder = args.Args( 1, -1 );
	if( GameLibrary::RemoveFolder( folder ) )
	{
		common->Printf( "Removed %s. Its games stay where they are; Daedalus just stops looking there.\n", folder );
	}
	else
	{
		common->Printf( "%s isn't one of your game folders.\n", folder );
	}
}

void GameLibrary::RegisterCommands()
{
	cmdSystem->AddCommand( "listGames", ListGames_f, CMD_FL_SYSTEM, "lists the id Tech 4 games found on this computer" );
	cmdSystem->AddCommand( "addGameFolder", AddGameFolder_f, CMD_FL_SYSTEM, "adds a folder to look for games in: a game's folder, or a folder of games" );
	cmdSystem->AddCommand( "removeGameFolder", RemoveGameFolder_f, CMD_FL_SYSTEM, "stops looking for games in a folder" );
}
