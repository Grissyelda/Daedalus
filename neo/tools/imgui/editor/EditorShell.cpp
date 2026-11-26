/*
===========================================================================

GrizzTech4 GPL Source Code
Copyright (C) 2026 GrizzTech4 contributors

This file is part of GrizzTech4, which is based on RBDOOM-3-BFG and the
Doom 3 BFG Edition GPL Source Code.

GrizzTech4 is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

GrizzTech4 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GrizzTech4.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

#include "precompiled.h"
#pragma hdrstop

#if defined( GRIZZ_EDITOR )

#include "EditorShell.h"
#include "EditorDocument.h"
#include "EditorView.h"

#include "../../imgui/BFGimgui.h"
#include "../../libs/imgui/imgui_internal.h"	// DockBuilder API
#include "../../libs/imgui/ImGuizmo.h"
#include "renderer/RenderCommon.h"

idCVar editor( "editor", "1", CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT, "start in the GrizzTech4 editor (0 = run as the game)" );
idCVar editor_startMap( "editor_startMap", "testmaps/gt4_test_room", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT, "map the editor opens at startup" );
idCVar editor_gridSize( "editor_gridSize", "8", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_NOCHEAT, "editor grid size in units" );
idCVar editor_snap( "editor_snap", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT, "snap moves to the editor grid" );

extern idCVar r_materialPath;

namespace GrizzEditor
{

/*
===============================================================================

	State

===============================================================================
*/

static bool			commandsRegistered = false;
static bool			layoutBuilt = false;
static bool			requestLayoutReset = false;
static bool			startupDone = false;

static bool			showOutliner = true;
static bool			showInspector = true;
static bool			showContentBrowser = true;
static bool			showProblems = true;
static bool			showAbout = false;
static bool			openNewProjectPopup = false;
static bool			openSaveAsPopup = false;
static bool			openNewMapPopup = false;

static idStrList	mapList;
static bool			mapListDirty = true;
static int			selectedMap = -1;

static char			outlinerFilter[64] = "";
static char			materialFilter[64] = "textures/";
static char			entityFilter[64] = "";
static char			newKeyName[64] = "";
static char			newKeyValue[256] = "";
static char			mapNameBuffer[128] = "";
static char			brushMaterialBuffer[256] = "";
static int			brushMaterialFor = -1;

static bool			gizmoDragging = false;
static idStr		statusMessage;
static int			statusTime = 0;

// New Project dialog
static char			newProjectName[64] = "MyGame";
static int			newProjectLook = -1;		// -1 = not chosen yet, 0 = stylized, 1 = PBR
static idStr		newProjectResult;
static bool			newProjectSucceeded = false;

static const char* WINDOW_OUTLINER			= "Outliner";
static const char* WINDOW_INSPECTOR			= "Inspector";
static const char* WINDOW_CONTENT_BROWSER	= "Content Browser";
static const char* WINDOW_PROBLEMS			= "Problems";

// quiet, wrapped hint text
static void Hint( const char* text )
{
	ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
	ImGui::TextWrapped( "%s", text );
	ImGui::PopStyleColor();
}

static void Status( const char* text )
{
	statusMessage = text;
	statusTime = Sys_Milliseconds();
	common->Printf( "Editor: %s\n", text );
}

static float GridSize()
{
	return ( float )idMath::ClampInt( 1, 512, editor_gridSize.GetInteger() );
}

static idVec3 SnapToGrid( const idVec3& v )
{
	if( !editor_snap.GetBool() )
	{
		return v;
	}
	const float g = GridSize();
	return idVec3( idMath::Rint( v.x / g ) * g, idMath::Rint( v.y / g ) * g, idMath::Rint( v.z / g ) * g );
}

/*
===============================================================================

	Play / Stop

===============================================================================
*/

static bool IsPlaying()
{
	if( session == NULL )
	{
		return false;
	}

	const idSession::sessionState_t state = session->GetState();
	return state == idSession::LOADING || state == idSession::INGAME;
}

static void Play( const char* mapName )
{
	if( mapName == NULL || mapName[0] == '\0' )
	{
		common->Warning( "Play: no map open" );
		return;
	}

	// compile the map first when it has never been compiled or was saved since
	ID_TIME_T mapTime = FILE_NOT_FOUND_TIMESTAMP;
	ID_TIME_T procTime = FILE_NOT_FOUND_TIMESTAMP;
	fileSystem->ReadFile( va( "maps/%s.map", mapName ), NULL, &mapTime );
	fileSystem->ReadFile( va( "maps/%s.proc", mapName ), NULL, &procTime );
	if( procTime == FILE_NOT_FOUND_TIMESTAMP || ( mapTime != FILE_NOT_FOUND_TIMESTAMP && mapTime > procTime ) )
	{
		common->Printf( "Editor: compiling %s before playing\n", mapName );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "dmap %s\n", mapName ) );
	}

	common->Printf( "Editor: playing %s (F10 stops)\n", mapName );
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "map %s\n", mapName ) );
}

// Play the open level: save it first so the game sees exactly what's in the editor
static void PlayDocument()
{
	if( !document.IsOpen() )
	{
		return;
	}
	if( document.IsDirty() && !document.Save() )
	{
		Status( "Couldn't save the level, so it can't be played" );
		return;
	}
	mapListDirty = true;
	Play( document.Name() );
}

static void Stop()
{
	if( IsPlaying() )
	{
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
	}
}

static void OpenMap( const char* mapName )
{
	if( document.IsDirty() && document.IsOpen() )
	{
		// never lose work silently: save what's open before switching
		document.Save();
	}
	if( document.Load( mapName ) )
	{
		const idMapFile* map = document.Map();
		for( int i = 0; i < map->GetNumEntities(); i++ )
		{
			if( idStr::Icmp( map->GetEntity( i )->epairs.GetString( "classname" ), "info_player_start" ) == 0 )
			{
				view.camera.origin = map->GetEntity( i )->epairs.GetVector( "origin" ) + idVec3( 0, 0, 64 );
				view.camera.yaw = map->GetEntity( i )->epairs.GetFloat( "angle" );
				view.camera.pitch = 0.0f;
				break;
			}
		}
		Status( va( "Opened %s", mapName ) );
	}
	else
	{
		Status( va( "Couldn't open %s", mapName ) );
	}
}

static void EditorPlay_f( const idCmdArgs& args )
{
	if( args.Argc() > 1 )
	{
		Play( args.Argv( 1 ) );
	}
	else
	{
		PlayDocument();
	}
}

static void EditorStop_f( const idCmdArgs& args )
{
	Stop();
}

static void EditorOpen_f( const idCmdArgs& args )
{
	if( args.Argc() != 2 )
	{
		common->Printf( "usage: editorOpen <map, e.g. testmaps/gt4_test_room>\n" );
		return;
	}
	OpenMap( args.Argv( 1 ) );
}

static void EditorSave_f( const idCmdArgs& args )
{
	if( args.Argc() == 2 )
	{
		document.SaveAs( args.Argv( 1 ) );
	}
	else
	{
		document.Save();
	}
	mapListDirty = true;
}

static void EditorUndo_f( const idCmdArgs& args )
{
	document.Undo();
}

static void EditorRedo_f( const idCmdArgs& args )
{
	document.Redo();
}

// lets tests and scripts drive the editor: editorSelect <entity> [primitive]
static void EditorSelect_f( const idCmdArgs& args )
{
	if( args.Argc() < 2 )
	{
		document.selection.Clear();
		return;
	}
	document.selection.entity = atoi( args.Argv( 1 ) );
	document.selection.primitive = args.Argc() > 2 ? atoi( args.Argv( 2 ) ) : -1;
	if( !document.SelectionIsValid() )
	{
		document.selection.Clear();
	}
}

static void EditorMove_f( const idCmdArgs& args )
{
	if( args.Argc() != 4 || document.SelectedEntity() == NULL )
	{
		common->Printf( "usage: editorMove <x> <y> <z> (moves the selection)\n" );
		return;
	}
	document.BeginChange( "Move" );
	document.TranslateSelection( idVec3( atof( args.Argv( 1 ) ), atof( args.Argv( 2 ) ), atof( args.Argv( 3 ) ) ) );
	document.EndChange();
}

static void EditorCamera_f( const idCmdArgs& args )
{
	if( args.Argc() < 4 )
	{
		common->Printf( "camera at %s yaw %.1f pitch %.1f\nusage: editorCamera <x> <y> <z> [yaw] [pitch]\n", view.camera.origin.ToString( 1 ), view.camera.yaw, view.camera.pitch );
		return;
	}
	view.camera.origin.Set( atof( args.Argv( 1 ) ), atof( args.Argv( 2 ) ), atof( args.Argv( 3 ) ) );
	if( args.Argc() > 4 )
	{
		view.camera.yaw = atof( args.Argv( 4 ) );
	}
	if( args.Argc() > 5 )
	{
		view.camera.pitch = atof( args.Argv( 5 ) );
	}
}

static void EditorWireframe_f( const idCmdArgs& args )
{
	view.wireframe = !view.wireframe;
}

static bool IsValidProjectName( const char* name );
static bool CreateProject( const char* name, int look, idStr& result );

static void EditorNewProject_f( const idCmdArgs& args )
{
	if( args.Argc() != 3 || ( idStr::Icmp( args.Argv( 2 ), "stylized" ) != 0 && idStr::Icmp( args.Argv( 2 ), "pbr" ) != 0 ) )
	{
		common->Printf( "usage: editorNewProject <name> <stylized|pbr>\n" );
		return;
	}

	if( !IsValidProjectName( args.Argv( 1 ) ) )
	{
		common->Warning( "editorNewProject: use letters, numbers, - and _ only, and not an engine folder name" );
		return;
	}

	idStr result;
	if( !CreateProject( args.Argv( 1 ), idStr::Icmp( args.Argv( 2 ), "stylized" ) == 0 ? 0 : 1, result ) )
	{
		common->Warning( "editorNewProject: %s", result.c_str() );
	}
}

static void RegisterCommands()
{
	if( commandsRegistered )
	{
		return;
	}
	commandsRegistered = true;

	cmdSystem->AddCommand( "editorPlay", EditorPlay_f, CMD_FL_SYSTEM, "plays the open level (or the given map) in the editor window" );
	cmdSystem->AddCommand( "editorStop", EditorStop_f, CMD_FL_SYSTEM, "stops playing and returns to the editor" );
	cmdSystem->AddCommand( "editorOpen", EditorOpen_f, CMD_FL_SYSTEM, "opens a map in the editor: editorOpen <map>" );
	cmdSystem->AddCommand( "editorSave", EditorSave_f, CMD_FL_SYSTEM, "saves the open level: editorSave [new map name]" );
	cmdSystem->AddCommand( "editorUndo", EditorUndo_f, CMD_FL_SYSTEM, "undoes the last edit" );
	cmdSystem->AddCommand( "editorRedo", EditorRedo_f, CMD_FL_SYSTEM, "redoes the last undone edit" );
	cmdSystem->AddCommand( "editorSelect", EditorSelect_f, CMD_FL_SYSTEM, "selects an entity (and optionally one of its brushes): editorSelect <entity> [brush]" );
	cmdSystem->AddCommand( "editorMove", EditorMove_f, CMD_FL_SYSTEM, "moves the selection: editorMove <x> <y> <z>" );
	cmdSystem->AddCommand( "editorCamera", EditorCamera_f, CMD_FL_SYSTEM, "shows or sets the editor camera: editorCamera <x> <y> <z> [yaw] [pitch]" );
	cmdSystem->AddCommand( "editorWireframe", EditorWireframe_f, CMD_FL_SYSTEM, "toggles brush outlines in the editor view" );
	cmdSystem->AddCommand( "editorNewProject", EditorNewProject_f, CMD_FL_SYSTEM, "creates a project from the template: editorNewProject <name> <stylized|pbr>" );

	// F10 always gets back to the editor while playing
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "bind F10 editorStop\n" );
}

/*
===============================================================================

	Content

===============================================================================
*/

static void RefreshMapList()
{
	mapList.Clear();

	idFileList* files = fileSystem->ListFilesTree( "maps", ".map", true );
	if( files != NULL )
	{
		for( int i = 0; i < files->GetNumFiles(); i++ )
		{
			idStr name = files->GetFile( i );
			name.StripLeadingOnce( "maps/" );
			name.StripFileExtension();
			mapList.Append( name );
		}
		fileSystem->FreeFileList( files );
	}

	if( selectedMap >= mapList.Num() )
	{
		selectedMap = -1;
	}

	mapListDirty = false;
}

/*
===============================================================================

	New Project

===============================================================================
*/

static bool IsValidProjectName( const char* name )
{
	if( name == NULL || name[0] == '\0' )
	{
		return false;
	}

	for( const char* c = name; *c; c++ )
	{
		const bool ok = ( *c >= 'a' && *c <= 'z' ) || ( *c >= 'A' && *c <= 'Z' ) || ( *c >= '0' && *c <= '9' ) || *c == '_' || *c == '-';
		if( !ok )
		{
			return false;
		}
	}

	// these folder names belong to the engine
	return idStr::Icmp( name, BASE_GAMEDIR ) != 0 && idStr::Icmp( name, "template" ) != 0 && idStr::Icmp( name, "base" ) != 0;
}

// copies every file below srcOS into dstOS, keeping the folder structure
static int CopyFolder( const idStr& srcOS, const idStr& dstOS )
{
	int copied = 0;

	idStrList files;
	if( Sys_ListFiles( srcOS.c_str(), "", files ) > 0 )
	{
		for( int i = 0; i < files.Num(); i++ )
		{
			const idStr& name = files[i];
			if( name == "." || name == ".." )
			{
				continue;
			}

			idStr from = srcOS;
			from.AppendPath( name );
			if( Sys_IsFolder( from.c_str() ) == FOLDER_YES )
			{
				continue;
			}

			idStr to = dstOS;
			to.AppendPath( name );
			fileSystem->CopyFile( from.c_str(), to.c_str() );
			copied++;
		}
	}

	idStrList folders;
	if( Sys_ListFiles( srcOS.c_str(), "/", folders ) > 0 )
	{
		for( int i = 0; i < folders.Num(); i++ )
		{
			const idStr& name = folders[i];
			if( name == "." || name == ".." )
			{
				continue;
			}

			idStr from = srcOS;
			from.AppendPath( name );
			idStr to = dstOS;
			to.AppendPath( name );
			copied += CopyFolder( from, to );
		}
	}

	return copied;
}

static bool CreateProject( const char* name, int look, idStr& result )
{
	const char* basePath = cvarSystem->GetCVarString( "fs_basepath" );

	idStr templateOS = basePath;
	templateOS.AppendPath( "template" );

	idStr projectOS = basePath;
	projectOS.AppendPath( name );

	if( Sys_IsFolder( templateOS.c_str() ) != FOLDER_YES )
	{
		result = va( "The project template is missing (expected a folder named \"template\" next to \"%s\").", BASE_GAMEDIR );
		return false;
	}

	if( Sys_IsFolder( projectOS.c_str() ) == FOLDER_YES )
	{
		result = va( "A project called \"%s\" already exists. Choose another name.", name );
		return false;
	}

	fileSystem->CreateOSPath( projectOS.c_str() );
	const int copied = CopyFolder( templateOS, projectOS );

	const char* lookName = ( look == 0 ) ? "stylized" : "pbr";

	// project description, read by the editor
	idStr projectFile = projectOS;
	projectFile.AppendPath( "project.gt4" );
	idFile* f = fileSystem->OpenExplicitFileWrite( projectFile.c_str() );
	if( f == NULL )
	{
		result = va( "Could not write %s.", projectFile.c_str() );
		return false;
	}
	f->Printf( "// GrizzTech4 project\n" );
	f->Printf( "name \"%s\"\n", name );
	f->Printf( "look \"%s\"\n", lookName );
	f->Printf( "engine \"%s\"\n", ENGINE_VERSION );
	fileSystem->CloseFile( f );

	// settings applied whenever the project is opened
	idStr autoexec = projectOS;
	autoexec.AppendPath( "autoexec.cfg" );
	f = fileSystem->OpenExplicitFileWrite( autoexec.c_str() );
	if( f != NULL )
	{
		f->Printf( "// GrizzTech4 project settings (written by New Project)\n" );
		f->Printf( "seta r_materialPath \"%s\"\n", lookName );
		fileSystem->CloseFile( f );
	}

	common->Printf( "Editor: created project %s with the %s look (%d template files)\n", projectOS.c_str(), lookName, copied );
	result = projectOS;
	return true;
}

// small painted previews so the choice isn't made from words alone
static void DrawLookPreview( int look, float size )
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const ImVec2 c( p.x + size * 0.5f, p.y + size * 0.5f );
	const float r = size * 0.36f;

	draw->AddRectFilled( p, ImVec2( p.x + size, p.y + size ), IM_COL32( 40, 44, 52, 255 ), 6.0f );

	if( look == 0 )
	{
		// stylized: flat colour bands, a hard terminator and an ink outline
		draw->AddCircleFilled( c, r, IM_COL32( 196, 120, 72, 255 ), 48 );
		draw->AddCircleFilled( ImVec2( c.x - r * 0.18f, c.y - r * 0.18f ), r * 0.78f, IM_COL32( 226, 156, 96, 255 ), 48 );
		draw->AddCircleFilled( ImVec2( c.x - r * 0.38f, c.y - r * 0.38f ), r * 0.30f, IM_COL32( 250, 214, 160, 255 ), 32 );
		draw->AddCircle( c, r, IM_COL32( 30, 20, 16, 255 ), 48, 3.0f );
	}
	else
	{
		// PBR: smooth falloff and a tight specular highlight
		const int steps = 24;
		for( int i = 0; i < steps; i++ )
		{
			const float t = ( float )i / ( float )( steps - 1 );
			const float rr = r * ( 1.0f - t * 0.92f );
			const float off = r * 0.32f * t;
			const int v = ( int )( 50 + 150 * t );
			draw->AddCircleFilled( ImVec2( c.x - off, c.y - off ), rr, IM_COL32( v, ( int )( v * 0.92f ), ( int )( v * 0.86f ), 255 ), 48 );
		}
		draw->AddCircleFilled( ImVec2( c.x - r * 0.40f, c.y - r * 0.42f ), r * 0.08f, IM_COL32( 255, 255, 255, 230 ), 16 );
	}

	ImGui::Dummy( ImVec2( size, size ) );
}

static void DrawNewProjectDialog()
{
	if( openNewProjectPopup )
	{
		ImGui::OpenPopup( "New Project" );
		openNewProjectPopup = false;
		newProjectResult.Clear();
		newProjectSucceeded = false;
		newProjectLook = -1;
	}

	ImGui::SetNextWindowSize( ImVec2( 560, 0 ), ImGuiCond_Appearing );
	if( !ImGui::BeginPopupModal( "New Project", NULL, ImGuiWindowFlags_NoResize ) )
	{
		return;
	}

	if( newProjectSucceeded )
	{
		ImGui::TextWrapped( "Your project was created at:" );
		ImGui::TextWrapped( "%s", newProjectResult.c_str() );
		ImGui::Spacing();
		ImGui::TextWrapped( "Opening projects from inside the editor arrives in the next milestone. For now, start GrizzTech4 with this option to open it:" );
		const idStr launchArg = va( "+set fs_game %s", newProjectName );
		ImGui::TextColored( ImVec4( 0.55f, 0.8f, 1.0f, 1.0f ), "%s", launchArg.c_str() );
		if( ImGui::Button( "Copy option" ) )
		{
			ImGui::SetClipboardText( launchArg.c_str() );
		}
		ImGui::SameLine();
		if( ImGui::Button( "Close" ) )
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return;
	}

	ImGui::Text( "Project name" );
	ImGui::SetNextItemWidth( -1 );
	ImGui::InputText( "##name", newProjectName, sizeof( newProjectName ), ImGuiInputTextFlags_CharsNoBlank );
	if( !IsValidProjectName( newProjectName ) )
	{
		ImGui::TextColored( ImVec4( 1.0f, 0.6f, 0.3f, 1.0f ), "Use letters, numbers, - and _ only." );
	}

	ImGui::Spacing();
	ImGui::Text( "Which look should this project start with?" );
	Hint( "You can change it later in the project settings, and per material." );
	ImGui::Spacing();

	const float cardWidth = 250.0f;
	const char* titles[2] = { "Stylized", "PBR" };
	const char* blurbs[2] =
	{
		"Painted colour, banded light and inked edges. Forgiving for hand-made art.",
		"Physically based materials: roughness, metalness and realistic light."
	};

	for( int look = 0; look < 2; look++ )
	{
		if( look > 0 )
		{
			ImGui::SameLine();
		}

		ImGui::BeginGroup();
		ImGui::PushID( look );
		DrawLookPreview( look, 120.0f );
		if( ImGui::RadioButton( titles[look], newProjectLook == look ) )
		{
			newProjectLook = look;
		}
		ImGui::PushTextWrapPos( ImGui::GetCursorPosX() + cardWidth - 16.0f );
		ImGui::TextDisabled( "%s", blurbs[look] );
		ImGui::PopTextWrapPos();
		ImGui::PopID();
		ImGui::EndGroup();
	}

	ImGui::Spacing();
	if( newProjectResult.Length() > 0 )
	{
		ImGui::TextColored( ImVec4( 1.0f, 0.45f, 0.4f, 1.0f ), "%s", newProjectResult.c_str() );
	}

	ImGui::Separator();

	const bool canCreate = IsValidProjectName( newProjectName ) && newProjectLook >= 0;
	ImGui::BeginDisabled( !canCreate );
	if( ImGui::Button( "Create project" ) )
	{
		newProjectSucceeded = CreateProject( newProjectName, newProjectLook, newProjectResult );
	}
	ImGui::EndDisabled();
	if( !canCreate && newProjectLook < 0 )
	{
		ImGui::SameLine();
		ImGui::TextDisabled( "Pick a look to continue." );
	}

	ImGui::SameLine( ImGui::GetWindowWidth() - 80.0f );
	if( ImGui::Button( "Cancel" ) )
	{
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}


/*
===============================================================================

	Map name dialogs (New Map, Save As)

===============================================================================
*/

static bool IsValidMapName( const char* name )
{
	if( name == NULL || name[0] == '\0' || name[0] == '/' )
	{
		return false;
	}
	for( const char* c = name; *c; c++ )
	{
		const bool ok = ( *c >= 'a' && *c <= 'z' ) || ( *c >= 'A' && *c <= 'Z' ) || ( *c >= '0' && *c <= '9' ) || *c == '_' || *c == '-' || *c == '/';
		if( !ok )
		{
			return false;
		}
	}
	return strstr( name, ".." ) == NULL;
}

static void DrawMapNameDialog( const char* title, bool& openRequest, bool isNewMap )
{
	if( openRequest )
	{
		ImGui::OpenPopup( title );
		openRequest = false;
		idStr::Copynz( mapNameBuffer, isNewMap ? "mymap" : document.Name(), sizeof( mapNameBuffer ) );
	}

	ImGui::SetNextWindowSize( ImVec2( 420, 0 ), ImGuiCond_Appearing );
	if( !ImGui::BeginPopupModal( title, NULL, ImGuiWindowFlags_NoResize ) )
	{
		return;
	}

	ImGui::Text( "Map name" );
	Hint( "Saved in the project's maps folder. Use / for sub-folders, e.g. levels/harbour." );
	ImGui::SetNextItemWidth( -1 );
	if( ImGui::IsWindowAppearing() )
	{
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::InputText( "##mapname", mapNameBuffer, sizeof( mapNameBuffer ), ImGuiInputTextFlags_CharsNoBlank );

	const bool valid = IsValidMapName( mapNameBuffer );
	if( !valid )
	{
		ImGui::TextColored( ImVec4( 1.0f, 0.6f, 0.3f, 1.0f ), "Use letters, numbers, -, _ and / only." );
	}

	ImGui::Separator();
	ImGui::BeginDisabled( !valid );
	if( ImGui::Button( isNewMap ? "Create" : "Save" ) )
	{
		if( isNewMap )
		{
			if( document.IsDirty() )
			{
				document.Save();
			}
			document.New( mapNameBuffer );
			document.Save();
			view.camera.origin.Set( -384, 0, 160 );
			view.camera.yaw = 0.0f;
			view.camera.pitch = 15.0f;
			Status( va( "Created %s", mapNameBuffer ) );
		}
		else if( document.SaveAs( mapNameBuffer ) )
		{
			Status( va( "Saved as %s", mapNameBuffer ) );
		}
		mapListDirty = true;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if( ImGui::Button( "Cancel" ) )
	{
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

/*
===============================================================================

	Panels

===============================================================================
*/

static void BuildDefaultLayout( ImGuiID dockId )
{
	ImGui::DockBuilderRemoveNodeChildNodes( dockId );

	ImGuiID center = dockId;
	ImGuiID left = ImGui::DockBuilderSplitNode( center, ImGuiDir_Left, 0.18f, NULL, &center );
	ImGuiID right = ImGui::DockBuilderSplitNode( center, ImGuiDir_Right, 0.24f, NULL, &center );
	ImGuiID bottom = ImGui::DockBuilderSplitNode( center, ImGuiDir_Down, 0.26f, NULL, &center );

	ImGui::DockBuilderDockWindow( WINDOW_OUTLINER, left );
	ImGui::DockBuilderDockWindow( WINDOW_INSPECTOR, right );
	ImGui::DockBuilderDockWindow( WINDOW_CONTENT_BROWSER, bottom );
	ImGui::DockBuilderDockWindow( WINDOW_PROBLEMS, bottom );
	ImGui::DockBuilderFinish( dockId );
}

static idStr EntityLabel( const idMapEntity* ent, int index )
{
	const char* classname = ent->epairs.GetString( "classname", "?" );
	const char* name = ent->epairs.GetString( "name" );
	if( name[0] != '\0' )
	{
		return va( "%s  (%s)", name, classname );
	}
	return va( "%s %d", classname, index );
}

static void FocusSelection()
{
	idBounds bounds;
	if( document.SelectedPrimitive() != NULL )
	{
		PrimitiveBounds( document.SelectedPrimitive(), bounds );
	}
	else if( document.SelectedEntity() != NULL )
	{
		EntityBounds( document.SelectedEntity(), bounds );
	}
	view.camera.Focus( bounds );
}

static void DrawOutliner()
{
	if( !ImGui::Begin( WINDOW_OUTLINER, &showOutliner ) )
	{
		ImGui::End();
		return;
	}

	const idMapFile* map = document.Map();
	if( map == NULL )
	{
		Hint( "No level open. Use File > New Map or double-click a map in the Content Browser." );
		ImGui::End();
		return;
	}

	ImGui::SetNextItemWidth( -1 );
	ImGui::InputTextWithHint( "##filter", "Search", outlinerFilter, sizeof( outlinerFilter ) );
	ImGui::Separator();

	ImGui::BeginChild( "outliner_list" );

	// the level's own brushes
	const idMapEntity* world = map->GetEntity( 0 );
	if( outlinerFilter[0] == '\0' )
	{
		const bool open = ImGui::TreeNodeEx( "Level geometry", ImGuiTreeNodeFlags_SpanAvailWidth, "Level geometry  (%d brushes)", world->GetNumPrimitives() );
		if( open )
		{
			for( int p = 0; p < world->GetNumPrimitives(); p++ )
			{
				const bool isBrush = world->GetPrimitive( p )->GetType() == idMapPrimitive::TYPE_BRUSH;
				const bool selected = document.selection.entity == 0 && document.selection.primitive == p;
				if( ImGui::Selectable( va( "%s %d##prim%d", isBrush ? "Brush" : "Patch", p, p ), selected, ImGuiSelectableFlags_AllowDoubleClick ) )
				{
					document.selection.entity = 0;
					document.selection.primitive = p;
					if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
					{
						FocusSelection();
					}
				}
			}
			ImGui::TreePop();
		}
	}

	// everything else, in file order
	for( int i = 1; i < map->GetNumEntities(); i++ )
	{
		const idMapEntity* ent = map->GetEntity( i );
		const idStr label = EntityLabel( ent, i );
		if( outlinerFilter[0] != '\0' && label.Find( outlinerFilter, false ) < 0 )
		{
			continue;
		}
		const bool selected = document.selection.entity == i;
		if( ImGui::Selectable( va( "%s##ent%d", label.c_str(), i ), selected, ImGuiSelectableFlags_AllowDoubleClick ) )
		{
			document.selection.entity = i;
			document.selection.primitive = -1;
			if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
			{
				FocusSelection();
			}
		}
	}

	ImGui::EndChild();
	ImGui::End();
}

// records undo when a widget starts editing and drops the step if nothing changed
static void UndoBracket( const char* label )
{
	if( ImGui::IsItemActivated() )
	{
		document.BeginChange( label );
	}
	if( ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit() && document.IsChangeOpen() )
	{
		document.CancelChange();
	}
}

static bool IsVectorKey( const char* key )
{
	static const char* vectorKeys[] = { "origin", "light_radius", "light_center", "light_target", "light_up", "light_right", "light_start", "light_end", "editor_mins", "editor_maxs" };
	for( int i = 0; i < ( int )( sizeof( vectorKeys ) / sizeof( vectorKeys[0] ) ); i++ )
	{
		if( idStr::Icmp( key, vectorKeys[i] ) == 0 )
		{
			return true;
		}
	}
	return false;
}

static bool IsColorKey( const char* key, const idDeclEntityDef* def )
{
	if( idStr::Icmp( key, "_color" ) == 0 )
	{
		return true;
	}
	return def != NULL && def->dict.FindKey( va( "editor_color %s", key ) ) != NULL;
}

static void DrawEntityInspector( idMapEntity* ent )
{
	const char* classname = ent->epairs.GetString( "classname" );
	const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->FindType( DECL_ENTITYDEF, classname, false ) );

	ImGui::Text( "%s", classname );
	if( def != NULL && def->dict.GetString( "editor_usage" )[0] != '\0' )
	{
		Hint( def->dict.GetString( "editor_usage" ) );
	}
	else if( def == NULL && document.selection.entity != 0 )
	{
		ImGui::TextColored( ImVec4( 1.0f, 0.6f, 0.3f, 1.0f ), "No entity definition called \"%s\" exists.", classname );
	}
	if( ent->GetNumPrimitives() > 0 )
	{
		ImGui::TextDisabled( "%d brushes", ent->GetNumPrimitives() );
	}
	ImGui::Separator();

	int deleteIndex = -1;

	if( ImGui::BeginTable( "keys", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg ) )
	{
		ImGui::TableSetupColumn( "Key", ImGuiTableColumnFlags_WidthStretch, 0.4f );
		ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch, 0.6f );
		ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, 20.0f );

		for( int i = 0; i < ent->epairs.GetNumKeyVals(); i++ )
		{
			const idKeyValue* kv = ent->epairs.GetKeyVal( i );
			const idStr key = kv->GetKey();
			const idStr value = kv->GetValue();

			ImGui::PushID( i );
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted( key.c_str() );
			if( def != NULL && ImGui::IsItemHovered() )
			{
				const char* help = def->dict.GetString( va( "editor_var %s", key.c_str() ) );
				if( help[0] == '\0' )
				{
					help = def->dict.GetString( va( "editor_color %s", key.c_str() ) );
				}
				if( help[0] != '\0' )
				{
					ImGui::SetTooltip( "%s", help );
				}
			}

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth( -1 );

			const bool isWorld = document.selection.entity == 0;
			if( key.Icmp( "classname" ) == 0 && isWorld )
			{
				ImGui::TextDisabled( "%s", value.c_str() );
			}
			else if( IsColorKey( key, def ) )
			{
				idVec3 color = ent->epairs.GetVector( key );
				if( ImGui::ColorEdit3( "##v", color.ToFloatPtr(), ImGuiColorEditFlags_Float ) )
				{
					ent->epairs.Set( key, color.ToString( 3 ) );
					document.EndChange();
				}
				UndoBracket( va( "Edit %s", key.c_str() ) );
			}
			else if( IsVectorKey( key ) )
			{
				idVec3 v = ent->epairs.GetVector( key );
				if( ImGui::DragFloat3( "##v", v.ToFloatPtr(), 1.0f ) )
				{
					if( key.Icmp( "origin" ) == 0 )
					{
						// move the brushes with the origin so the entity stays in one piece
						document.TranslateSelection( v - ent->epairs.GetVector( "origin" ) );
						ent->epairs.Set( "origin", v.ToString( 2 ) );
					}
					else
					{
						ent->epairs.Set( key, v.ToString( 2 ) );
					}
					document.EndChange();
				}
				UndoBracket( va( "Edit %s", key.c_str() ) );
			}
			else
			{
				char buffer[1024];
				idStr::Copynz( buffer, value.c_str(), sizeof( buffer ) );
				if( ImGui::InputText( "##v", buffer, sizeof( buffer ) ) )
				{
					ent->epairs.Set( key, buffer );
					document.EndChange();
				}
				UndoBracket( va( "Edit %s", key.c_str() ) );
			}

			ImGui::TableNextColumn();
			const bool protectedKey = key.Icmp( "classname" ) == 0;
			if( !protectedKey && ImGui::SmallButton( "x" ) )
			{
				deleteIndex = i;
			}
			if( !protectedKey && ImGui::IsItemHovered() )
			{
				ImGui::SetTooltip( "Remove %s", key.c_str() );
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	if( deleteIndex >= 0 )
	{
		const idStr key = ent->epairs.GetKeyVal( deleteIndex )->GetKey();
		document.BeginChange( va( "Remove %s", key.c_str() ) );
		ent->epairs.Delete( key );
		document.EndChange();
	}

	// add a key
	ImGui::Spacing();
	ImGui::TextDisabled( "Add a property" );
	ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x * 0.4f );
	ImGui::InputTextWithHint( "##newkey", "key", newKeyName, sizeof( newKeyName ), ImGuiInputTextFlags_CharsNoBlank );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - 48.0f );
	ImGui::InputTextWithHint( "##newvalue", "value", newKeyValue, sizeof( newKeyValue ) );
	ImGui::SameLine();
	ImGui::BeginDisabled( newKeyName[0] == '\0' );
	if( ImGui::Button( "Add" ) )
	{
		document.BeginChange( va( "Add %s", newKeyName ) );
		ent->epairs.Set( newKeyName, newKeyValue );
		document.EndChange();
		newKeyName[0] = '\0';
		newKeyValue[0] = '\0';
	}
	ImGui::EndDisabled();

	// properties the definition documents but the entity doesn't set yet
	if( def != NULL )
	{
		bool header = false;
		for( int i = 0; i < def->dict.GetNumKeyVals(); i++ )
		{
			const idKeyValue* kv = def->dict.GetKeyVal( i );
			idStr key = kv->GetKey();
			if( !key.StripLeadingOnce( "editor_var " ) || ent->epairs.FindKey( key ) != NULL )
			{
				continue;
			}
			if( !header )
			{
				ImGui::Spacing();
				ImGui::TextDisabled( "Also available" );
				header = true;
			}
			ImGui::PushID( key.c_str() );
			if( ImGui::SmallButton( "+" ) )
			{
				document.BeginChange( va( "Add %s", key.c_str() ) );
				ent->epairs.Set( key, def->dict.GetString( key ) );
				document.EndChange();
			}
			ImGui::SameLine();
			ImGui::TextUnformatted( key.c_str() );
			if( ImGui::IsItemHovered() )
			{
				ImGui::SetTooltip( "%s", kv->GetValue().c_str() );
			}
			ImGui::PopID();
		}
	}
}

static void SetBrushMaterial( idMapPrimitive* prim, const char* material )
{
	if( prim->GetType() == idMapPrimitive::TYPE_BRUSH )
	{
		idMapBrush* brush = static_cast<idMapBrush*>( prim );
		for( int s = 0; s < brush->GetNumSides(); s++ )
		{
			brush->GetSide( s )->SetMaterial( material );
		}
	}
	else if( prim->GetType() == idMapPrimitive::TYPE_PATCH )
	{
		static_cast<idMapPatch*>( prim )->SetMaterial( material );
	}
}

static void DrawPrimitiveInspector( idMapPrimitive* prim )
{
	const bool isBrush = prim->GetType() == idMapPrimitive::TYPE_BRUSH;
	ImGui::Text( isBrush ? "Brush" : "Patch" );

	idBounds bounds;
	if( PrimitiveBounds( prim, bounds ) )
	{
		const idVec3 size = bounds[1] - bounds[0];
		ImGui::TextDisabled( "Size %.0f x %.0f x %.0f", size.x, size.y, size.z );
	}
	ImGui::Separator();

	// one material for the whole brush; per-face materials arrive with face selection
	const int key = document.selection.entity * 100000 + document.selection.primitive;
	if( brushMaterialFor != key || document.IsChangeOpen() == false )
	{
		if( brushMaterialFor != key )
		{
			const char* current = isBrush ? static_cast<idMapBrush*>( prim )->GetSide( 0 )->GetMaterial() : static_cast<idMapPatch*>( prim )->GetMaterial();
			idStr::Copynz( brushMaterialBuffer, current, sizeof( brushMaterialBuffer ) );
			brushMaterialFor = key;
		}
	}

	ImGui::Text( "Material" );
	ImGui::SetNextItemWidth( -60.0f );
	ImGui::InputText( "##material", brushMaterialBuffer, sizeof( brushMaterialBuffer ) );
	ImGui::SameLine();
	if( ImGui::Button( "Apply" ) )
	{
		document.BeginChange( "Set material" );
		SetBrushMaterial( prim, brushMaterialBuffer );
		document.EndChange();
	}
	Hint( "Or click a material in the Content Browser's Materials tab." );

	if( isBrush )
	{
		idMapBrush* brush = static_cast<idMapBrush*>( prim );
		if( ImGui::TreeNode( "Faces", "Faces (%d)", brush->GetNumSides() ) )
		{
			for( int s = 0; s < brush->GetNumSides(); s++ )
			{
				const idVec3 n = brush->GetSide( s )->GetPlane().Normal();
				ImGui::TextDisabled( "%d  (%.2f %.2f %.2f)", s, n.x, n.y, n.z );
				ImGui::SameLine();
				ImGui::TextUnformatted( brush->GetSide( s )->GetMaterial() );
			}
			ImGui::TreePop();
		}
	}
}

static void DrawInspector()
{
	if( !ImGui::Begin( WINDOW_INSPECTOR, &showInspector ) )
	{
		ImGui::End();
		return;
	}

	if( !document.SelectionIsValid() )
	{
		ImGui::TextDisabled( "Nothing selected" );
		ImGui::Separator();
		Hint( "Click something in the view or in the Outliner to see and change its properties." );
		if( document.IsOpen() && ImGui::Button( "Level settings" ) )
		{
			document.selection.entity = 0;
			document.selection.primitive = -1;
		}
		ImGui::End();
		return;
	}

	if( document.selection.IsPrimitive() )
	{
		DrawPrimitiveInspector( document.SelectedPrimitive() );
	}
	else
	{
		DrawEntityInspector( document.SelectedEntity() );
	}

	ImGui::End();
}

// where new things go: just in front of whatever the camera looks at
static idVec3 PlacementPoint()
{
	const idMat3 axis = view.camera.Axis();
	return SnapToGrid( view.camera.origin + axis[0] * 256.0f );
}

static void DrawContentBrowser()
{
	if( !ImGui::Begin( WINDOW_CONTENT_BROWSER, &showContentBrowser ) )
	{
		ImGui::End();
		return;
	}

	if( ImGui::BeginTabBar( "content_tabs" ) )
	{
		if( ImGui::BeginTabItem( "Maps" ) )
		{
			if( mapListDirty )
			{
				RefreshMapList();
			}
			if( ImGui::Button( "New map" ) )
			{
				openNewMapPopup = true;
			}
			ImGui::SameLine();
			if( ImGui::Button( "Refresh" ) )
			{
				RefreshMapList();
			}
			ImGui::SameLine();
			Hint( "Double-click a map to open it." );
			ImGui::Separator();

			ImGui::BeginChild( "maps" );
			for( int i = 0; i < mapList.Num(); i++ )
			{
				const bool isOpen = document.IsOpen() && mapList[i].Icmp( document.Name() ) == 0;
				if( ImGui::Selectable( va( "%s%s", mapList[i].c_str(), isOpen ? "   (open)" : "" ), selectedMap == i, ImGuiSelectableFlags_AllowDoubleClick ) )
				{
					selectedMap = i;
					if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) && !isOpen )
					{
						OpenMap( mapList[i].c_str() );
					}
				}
			}
			if( mapList.Num() == 0 )
			{
				Hint( "No maps in this project yet." );
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if( ImGui::BeginTabItem( "Materials" ) )
		{
			ImGui::SetNextItemWidth( 240.0f );
			ImGui::InputTextWithHint( "##matfilter", "Filter", materialFilter, sizeof( materialFilter ) );
			ImGui::SameLine();
			Hint( document.SelectedPrimitive() != NULL ? "Click a material to put it on the selected brush." : "Select a brush, then click a material to apply it." );
			ImGui::Separator();

			ImGui::BeginChild( "materials" );
			const int count = declManager->GetNumDecls( DECL_MATERIAL );
			for( int i = 0; i < count; i++ )
			{
				const idDecl* decl = declManager->DeclByIndex( DECL_MATERIAL, i, false );
				if( decl == NULL || decl->GetName()[0] == '_' )
				{
					continue;
				}
				if( materialFilter[0] != '\0' && idStr::FindText( decl->GetName(), materialFilter, false ) < 0 )
				{
					continue;
				}
				if( ImGui::Selectable( decl->GetName() ) && document.SelectedPrimitive() != NULL )
				{
					document.BeginChange( "Set material" );
					SetBrushMaterial( document.SelectedPrimitive(), decl->GetName() );
					document.EndChange();
					idStr::Copynz( brushMaterialBuffer, decl->GetName(), sizeof( brushMaterialBuffer ) );
				}
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if( ImGui::BeginTabItem( "Place" ) )
		{
			const bool canPlace = document.IsOpen();
			ImGui::BeginDisabled( !canPlace );
			if( ImGui::Button( "Box brush" ) )
			{
				const idVec3 p = PlacementPoint();
				const float half = Max( 64.0f, GridSize() * 8.0f );
				document.AddBoxBrush( p - idVec3( half, half, half ), p + idVec3( half, half, half ), "textures/dev/grid_wall" );
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			Hint( "New things appear just in front of the camera." );

			ImGui::SetNextItemWidth( 240.0f );
			ImGui::InputTextWithHint( "##entfilter", "Filter entity types", entityFilter, sizeof( entityFilter ) );
			ImGui::Separator();

			ImGui::BeginChild( "entities" );
			const int count = declManager->GetNumDecls( DECL_ENTITYDEF );
			for( int i = 0; i < count; i++ )
			{
				const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->DeclByIndex( DECL_ENTITYDEF, i, false ) );
				if( def == NULL )
				{
					continue;
				}
				// only types meant for levels: they document themselves for the editor
				const char* usage = def->dict.GetString( "editor_usage" );
				if( usage[0] == '\0' || idStr::Icmp( def->GetName(), "worldspawn" ) == 0 || idStr::Icmp( def->GetName(), "player" ) == 0 )
				{
					continue;
				}
				if( entityFilter[0] != '\0' && idStr::FindText( def->GetName(), entityFilter, false ) < 0 )
				{
					continue;
				}
				ImGui::PushID( i );
				ImGui::BeginDisabled( !canPlace );
				if( ImGui::SmallButton( "Place" ) )
				{
					document.AddEntity( def->GetName(), PlacementPoint() );
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::TextUnformatted( def->GetName() );
				if( ImGui::IsItemHovered() )
				{
					ImGui::SetTooltip( "%s", usage );
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
}

static void DrawProblems()
{
	if( !ImGui::Begin( WINDOW_PROBLEMS, &showProblems ) )
	{
		ImGui::End();
		return;
	}

	bool any = false;
	const idMapFile* map = document.Map();
	if( map != NULL )
	{
		bool hasStart = false;
		for( int i = 1; i < map->GetNumEntities(); i++ )
		{
			const idMapEntity* ent = map->GetEntity( i );
			const char* classname = ent->epairs.GetString( "classname" );
			if( idStr::Icmp( classname, "info_player_start" ) == 0 )
			{
				hasStart = true;
			}
			if( declManager->FindType( DECL_ENTITYDEF, classname, false ) == NULL )
			{
				any = true;
				ImGui::PushID( i );
				if( ImGui::SmallButton( "Show" ) )
				{
					document.selection.entity = i;
					document.selection.primitive = -1;
					FocusSelection();
				}
				ImGui::SameLine();
				ImGui::TextWrapped( "\"%s\" uses the type \"%s\", which doesn't exist. It won't appear in the game.", EntityLabel( ent, i ).c_str(), classname );
				ImGui::PopID();
			}
		}
		if( !hasStart )
		{
			any = true;
			ImGui::TextWrapped( "The level has no player start (info_player_start), so Play has nowhere to put you. Add one from the Content Browser's Place tab." );
		}
	}

	if( !any )
	{
		ImGui::TextDisabled( "No problems found." );
		Hint( "Leaks and compile errors are reported in the console (the ~ key) when you press Play." );
	}

	ImGui::End();
}

static void DrawAbout()
{
	if( !showAbout )
	{
		return;
	}

	ImGui::SetNextWindowSize( ImVec2( 460, 0 ), ImGuiCond_Appearing );
	if( ImGui::Begin( "About GrizzTech4", &showAbout, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse ) )
	{
		ImGui::Text( "%s", ENGINE_VERSION );
		ImGui::TextDisabled( "Based on %s and the Doom 3 BFG Edition GPL source.", UPSTREAM_VERSION );
		ImGui::Spacing();
		ImGui::TextWrapped( "Licensed under the GNU GPL v3 with id Software's additional terms." );
		ImGui::TextWrapped( "Material path: %s", r_materialPath.GetString() );
		ImGui::Separator();
		ImGui::Text( "Moving around" );
		Hint( "Hold the right mouse button to look around; WASD moves, Q and E go down and up, Shift is faster, and the wheel changes speed. Without the right button, the wheel moves forward and back." );
		ImGui::Text( "Editing" );
		Hint( "Click to select. Drag the arrows to move. Ctrl+Z undo, Ctrl+Y redo, Ctrl+D duplicate, Delete removes, F frames the selection, Ctrl+S saves." );
	}
	ImGui::End();
}

/*
===============================================================================

	Viewport: picking, the move handle and shortcuts

===============================================================================
*/

static idBounds SelectionBounds()
{
	idBounds bounds;
	bounds.Clear();
	if( document.SelectedPrimitive() != NULL )
	{
		PrimitiveBounds( document.SelectedPrimitive(), bounds );
	}
	else if( document.SelectedEntity() != NULL )
	{
		EntityBounds( document.SelectedEntity(), bounds );
	}
	return bounds;
}

static void DrawMoveHandle()
{
	// the level itself has no handle; its brushes do
	if( !document.SelectionIsValid() || ( document.selection.entity == 0 && !document.selection.IsPrimitive() ) )
	{
		if( gizmoDragging )
		{
			gizmoDragging = false;
			if( document.IsChangeOpen() )
			{
				document.CancelChange();
			}
		}
		return;
	}

	const idBounds bounds = SelectionBounds();
	if( bounds.IsCleared() )
	{
		return;
	}

	idVec3 pivot = bounds.GetCenter();
	const idMapEntity* ent = document.SelectedEntity();
	if( !document.selection.IsPrimitive() && ent->GetNumPrimitives() == 0 )
	{
		pivot = ent->epairs.GetVector( "origin" );
	}

	renderView_t rv;
	view.BuildRenderView( rv );
	viewDef_t viewDef;
	viewDef.renderView = rv;
	R_SetupViewMatrix( &viewDef );
	R_SetupProjectionMatrix( &viewDef, false );

	const ImVec2 size = ImGui::GetIO().DisplaySize;
	ImGuizmo::SetOrthographic( false );
	ImGuizmo::SetDrawlist( ImGui::GetBackgroundDrawList() );
	ImGuizmo::SetRect( 0, 0, size.x, size.y );
	ImGuizmo::SetID( 1 );

	idMat4 matrix = idMat4( mat3_identity, pivot ).Transpose();
	const float grid = GridSize();
	const float snap[3] = { grid, grid, grid };

	ImGuizmo::Manipulate( viewDef.worldSpace.modelViewMatrix, viewDef.unjitteredProjectionMatrix, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, matrix.ToFloatPtr(), NULL, editor_snap.GetBool() ? snap : NULL );

	if( ImGuizmo::IsUsing() )
	{
		if( !gizmoDragging )
		{
			gizmoDragging = true;
			document.BeginChange( "Move" );
		}
		const idVec3 moved = matrix.Transpose().GetTranslation();
		const idVec3 delta = moved - pivot;
		if( delta.LengthSqr() > 0.0001f )
		{
			document.TranslateSelection( delta );
			document.EndChange();
		}
	}
	else if( gizmoDragging )
	{
		gizmoDragging = false;
		if( document.IsChangeOpen() )
		{
			// grabbed but never moved
			document.CancelChange();
		}
	}
}

static void HandleShortcuts()
{
	ImGuiIO& io = ImGui::GetIO();
	if( io.WantTextInput || ImGui::IsMouseDown( ImGuiMouseButton_Right ) )
	{
		return;
	}

	if( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_Z, false ) )
	{
		if( io.KeyShift )
		{
			document.Redo();
		}
		else
		{
			document.Undo();
		}
	}
	else if( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_Y, false ) )
	{
		document.Redo();
	}
	else if( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_S, false ) )
	{
		if( document.Save() )
		{
			Status( "Saved" );
		}
		mapListDirty = true;
	}
	else if( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_D, false ) )
	{
		document.DuplicateSelection( idVec3( GridSize() * 4.0f, GridSize() * 4.0f, 0.0f ) );
	}
	else if( ImGui::IsKeyPressed( ImGuiKey_Delete, false ) )
	{
		document.DeleteSelection();
	}
	else if( ImGui::IsKeyPressed( ImGuiKey_F, false ) )
	{
		FocusSelection();
	}
	else if( ImGui::IsKeyPressed( ImGuiKey_Escape, false ) )
	{
		document.selection.Clear();
	}
}

static void HandleViewport()
{
	ImGuiIO& io = ImGui::GetIO();
	const bool hovered = !io.WantCaptureMouse;

	view.UpdateCamera( hovered );
	DrawMoveHandle();

	if( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() )
	{
		document.selection = view.Pick( document, io.MousePos.x, io.MousePos.y );
	}

	HandleShortcuts();
}

/*
===============================================================================

	Public interface

===============================================================================
*/

bool IsEditorMode()
{
	return editor.GetBool();
}

bool IsEditing()
{
	return IsEditorMode() && !IsPlaying();
}

void DrawWorld()
{
	view.Draw( document );
}

void DrawMainMenuBar()
{
	if( !IsEditorMode() )
	{
		return;
	}

	RegisterCommands();

	const bool playing = IsPlaying();

	if( !ImGui::BeginMainMenuBar() )
	{
		return;
	}

	if( ImGui::BeginMenu( "File", !playing ) )
	{
		if( ImGui::MenuItem( "New Map..." ) )
		{
			openNewMapPopup = true;
		}
		if( ImGui::BeginMenu( "Open Map" ) )
		{
			if( mapListDirty )
			{
				RefreshMapList();
			}
			for( int i = 0; i < mapList.Num(); i++ )
			{
				if( ImGui::MenuItem( mapList[i].c_str() ) )
				{
					OpenMap( mapList[i].c_str() );
				}
			}
			ImGui::EndMenu();
		}
		if( ImGui::MenuItem( "Save", "Ctrl+S", false, document.IsOpen() ) )
		{
			if( document.Save() )
			{
				Status( "Saved" );
			}
			mapListDirty = true;
		}
		if( ImGui::MenuItem( "Save As...", NULL, false, document.IsOpen() ) )
		{
			openSaveAsPopup = true;
		}
		ImGui::Separator();
		if( ImGui::MenuItem( "New Project..." ) )
		{
			openNewProjectPopup = true;
		}
		ImGui::Separator();
		if( ImGui::MenuItem( "Quit" ) )
		{
			if( document.IsDirty() )
			{
				document.Save();
			}
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
		}
		ImGui::EndMenu();
	}

	if( ImGui::BeginMenu( "Edit", !playing ) )
	{
		if( ImGui::MenuItem( va( "Undo %s", document.UndoLabel() ), "Ctrl+Z", false, document.CanUndo() ) )
		{
			document.Undo();
		}
		if( ImGui::MenuItem( va( "Redo %s", document.RedoLabel() ), "Ctrl+Y", false, document.CanRedo() ) )
		{
			document.Redo();
		}
		ImGui::Separator();
		const bool canEdit = document.SelectionIsValid() && !( document.selection.entity == 0 && !document.selection.IsPrimitive() );
		if( ImGui::MenuItem( "Duplicate", "Ctrl+D", false, canEdit ) )
		{
			document.DuplicateSelection( idVec3( GridSize() * 4.0f, GridSize() * 4.0f, 0.0f ) );
		}
		if( ImGui::MenuItem( "Delete", "Del", false, canEdit ) )
		{
			document.DeleteSelection();
		}
		if( ImGui::MenuItem( "Frame selection", "F", false, document.SelectionIsValid() ) )
		{
			FocusSelection();
		}
		ImGui::Separator();
		bool snap = editor_snap.GetBool();
		if( ImGui::MenuItem( "Snap to grid", NULL, &snap ) )
		{
			editor_snap.SetBool( snap );
		}
		if( ImGui::BeginMenu( "Grid size" ) )
		{
			const int sizes[] = { 1, 2, 4, 8, 16, 32, 64, 128 };
			for( int i = 0; i < 8; i++ )
			{
				if( ImGui::MenuItem( va( "%d", sizes[i] ), NULL, editor_gridSize.GetInteger() == sizes[i] ) )
				{
					editor_gridSize.SetInteger( sizes[i] );
				}
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}

	if( ImGui::BeginMenu( "View", !playing ) )
	{
		ImGui::MenuItem( WINDOW_OUTLINER, NULL, &showOutliner );
		ImGui::MenuItem( WINDOW_INSPECTOR, NULL, &showInspector );
		ImGui::MenuItem( WINDOW_CONTENT_BROWSER, NULL, &showContentBrowser );
		ImGui::MenuItem( WINDOW_PROBLEMS, NULL, &showProblems );
		ImGui::Separator();
		ImGui::MenuItem( "Brush outlines", NULL, &view.wireframe );
		ImGui::Separator();
		if( ImGui::MenuItem( "Reset layout" ) )
		{
			showOutliner = showInspector = showContentBrowser = showProblems = true;
			requestLayoutReset = true;
		}
		ImGui::EndMenu();
	}

	if( ImGui::BeginMenu( "Help" ) )
	{
		if( ImGui::MenuItem( "About GrizzTech4 and controls" ) )
		{
			showAbout = true;
		}
		ImGui::EndMenu();
	}

	// Play / Stop sits in the middle of the bar, like other editors
	const float buttonWidth = 90.0f;
	ImGui::SetCursorPosX( ( ImGui::GetWindowWidth() - buttonWidth ) * 0.5f );
	if( playing )
	{
		ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.65f, 0.2f, 0.2f, 1.0f ) );
		if( ImGui::Button( "Stop (F10)", ImVec2( buttonWidth, 0 ) ) )
		{
			Stop();
		}
		ImGui::PopStyleColor();
	}
	else
	{
		ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.2f, 0.5f, 0.25f, 1.0f ) );
		ImGui::BeginDisabled( !document.IsOpen() );
		if( ImGui::Button( "Play", ImVec2( buttonWidth, 0 ) ) )
		{
			PlayDocument();
		}
		ImGui::EndDisabled();
		ImGui::PopStyleColor();
	}

	// right side: status, open map and project
	const char* game = cvarSystem->GetCVarString( "fs_game" );
	idStr right = document.IsOpen() ? va( "%s%s", document.Name(), document.IsDirty() ? " *" : "" ) : "No level open";
	right += ( game != NULL && game[0] != '\0' ) ? va( "   |   Project: %s", game ) : "   |   Engine content";
	if( statusMessage.Length() > 0 && Sys_Milliseconds() - statusTime < 4000 )
	{
		right = statusMessage + "   |   " + right;
	}
	const float rightWidth = ImGui::CalcTextSize( right.c_str() ).x + 16.0f;
	ImGui::SetCursorPosX( ImGui::GetWindowWidth() - rightWidth );
	ImGui::TextDisabled( "%s", right.c_str() );

	ImGui::EndMainMenuBar();
}

void Draw( unsigned int mainDockSpaceId )
{
	if( !IsEditorMode() )
	{
		return;
	}

	RegisterCommands();

	if( IsPlaying() )
	{
		// panels step aside while playing; the menu bar keeps Stop visible
		return;
	}

	if( !startupDone )
	{
		startupDone = true;
		if( editor_startMap.GetString()[0] != '\0' )
		{
			OpenMap( editor_startMap.GetString() );
		}
	}

	if( mainDockSpaceId != 0 && ( !layoutBuilt || requestLayoutReset ) )
	{
		ImGuiDockNode* node = ImGui::DockBuilderGetNode( mainDockSpaceId );
		if( requestLayoutReset || node == NULL || !node->IsSplitNode() )
		{
			BuildDefaultLayout( mainDockSpaceId );
		}
		layoutBuilt = true;
		requestLayoutReset = false;
	}

	HandleViewport();

	if( showOutliner )
	{
		DrawOutliner();
	}
	if( showInspector )
	{
		DrawInspector();
	}
	if( showContentBrowser )
	{
		DrawContentBrowser();
	}
	if( showProblems )
	{
		DrawProblems();
	}

	DrawAbout();
	DrawNewProjectDialog();
	DrawMapNameDialog( "Save Map As", openSaveAsPopup, false );
	DrawMapNameDialog( "New Map", openNewMapPopup, true );
}

} // namespace GrizzEditor

#endif // GRIZZ_EDITOR
