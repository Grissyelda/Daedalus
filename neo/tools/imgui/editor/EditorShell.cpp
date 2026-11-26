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

#include "../../imgui/BFGimgui.h"
#include "../../libs/imgui/imgui_internal.h"	// DockBuilder API

idCVar editor( "editor", "1", CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT, "start in the GrizzTech4 editor (0 = run as the game)" );
idCVar editor_playMap( "editor_playMap", "testmaps/gt4_test_room", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT, "map that Play loads when nothing is selected in the Content Browser" );

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

static bool			showOutliner = true;
static bool			showInspector = true;
static bool			showContentBrowser = true;
static bool			showProblems = true;
static bool			showAbout = false;
static bool			openNewProjectPopup = false;

static idStrList	mapList;
static bool			mapListDirty = true;
static int			selectedMap = -1;

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

static const char* MapToPlay()
{
	if( selectedMap >= 0 && selectedMap < mapList.Num() )
	{
		return mapList[ selectedMap ].c_str();
	}

	return editor_playMap.GetString();
}

static void Play( const char* mapName )
{
	if( mapName == NULL || mapName[0] == '\0' )
	{
		common->Warning( "Play: no map selected" );
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

static void Stop()
{
	if( IsPlaying() )
	{
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
	}
}

static void EditorPlay_f( const idCmdArgs& args )
{
	Play( args.Argc() > 1 ? args.Argv( 1 ) : MapToPlay() );
}

static void EditorStop_f( const idCmdArgs& args )
{
	Stop();
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

	cmdSystem->AddCommand( "editorPlay", EditorPlay_f, CMD_FL_SYSTEM, "plays a map in the editor window: editorPlay [map]" );
	cmdSystem->AddCommand( "editorStop", EditorStop_f, CMD_FL_SYSTEM, "stops playing and returns to the editor" );
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

static void DrawOutliner()
{
	if( !ImGui::Begin( WINDOW_OUTLINER, &showOutliner ) )
	{
		ImGui::End();
		return;
	}

	ImGui::TextDisabled( "Level contents" );
	ImGui::Separator();
	ImGui::TextWrapped( "Every entity, brush, group and prefab in the open level will be listed here, with search, show/hide and lock." );
	ImGui::Spacing();
	Hint( "Arrives with level editing (milestone 1)." );

	ImGui::End();
}

static void DrawInspector()
{
	if( !ImGui::Begin( WINDOW_INSPECTOR, &showInspector ) )
	{
		ImGui::End();
		return;
	}

	ImGui::TextDisabled( "Selection" );
	ImGui::Separator();
	ImGui::TextWrapped( "The selected object's properties will appear here as a form: sliders, colour pickers and asset pickers with previews." );
	ImGui::Spacing();
	Hint( "Arrives with level editing (milestone 1)." );

	ImGui::End();
}

static void DrawContentBrowser()
{
	if( !ImGui::Begin( WINDOW_CONTENT_BROWSER, &showContentBrowser ) )
	{
		ImGui::End();
		return;
	}

	if( mapListDirty )
	{
		RefreshMapList();
	}

	if( ImGui::Button( "Refresh" ) )
	{
		RefreshMapList();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled( mapList.Num() == 0 );
	if( ImGui::Button( "Play selected" ) )
	{
		Play( MapToPlay() );
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled( "Double-click a map to play it." );

	ImGui::Separator();
	ImGui::TextDisabled( "Maps" );

	if( mapList.Num() == 0 )
	{
		ImGui::TextWrapped( "No maps found in this project's maps folder." );
	}

	for( int i = 0; i < mapList.Num(); i++ )
	{
		if( ImGui::Selectable( mapList[i].c_str(), selectedMap == i, ImGuiSelectableFlags_AllowDoubleClick ) )
		{
			selectedMap = i;
			if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
			{
				Play( mapList[i].c_str() );
			}
		}
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

	ImGui::TextWrapped( "Leaks, missing assets, broken links and script errors will be listed here in plain language, each with a button that takes you to it." );
	ImGui::Spacing();
	Hint( "Until then, the console (the ~ key) shows warnings." );

	ImGui::End();
}

static void DrawAbout()
{
	if( !showAbout )
	{
		return;
	}

	ImGui::SetNextWindowSize( ImVec2( 420, 0 ), ImGuiCond_Appearing );
	if( ImGui::Begin( "About GrizzTech4", &showAbout, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse ) )
	{
		ImGui::Text( "%s", ENGINE_VERSION );
		ImGui::TextDisabled( "Based on %s and the Doom 3 BFG Edition GPL source.", UPSTREAM_VERSION );
		ImGui::Spacing();
		ImGui::TextWrapped( "Licensed under the GNU GPL v3 with id Software's additional terms." );
		ImGui::TextWrapped( "Material path: %s", r_materialPath.GetString() );
	}
	ImGui::End();
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
		if( ImGui::MenuItem( "New Project..." ) )
		{
			openNewProjectPopup = true;
		}
		ImGui::Separator();
		if( ImGui::MenuItem( "Quit" ) )
		{
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
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
		if( ImGui::MenuItem( "Reset layout" ) )
		{
			showOutliner = showInspector = showContentBrowser = showProblems = true;
			requestLayoutReset = true;
		}
		ImGui::EndMenu();
	}

	if( ImGui::BeginMenu( "Help" ) )
	{
		if( ImGui::MenuItem( "About GrizzTech4" ) )
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
		if( ImGui::Button( "Play", ImVec2( buttonWidth, 0 ) ) )
		{
			Play( MapToPlay() );
		}
		ImGui::PopStyleColor();
	}

	const char* game = cvarSystem->GetCVarString( "fs_game" );
	const idStr project = ( game != NULL && game[0] != '\0' ) ? va( "Project: %s", game ) : "No project open (engine content)";
	const float projectWidth = ImGui::CalcTextSize( project.c_str() ).x + 16.0f;
	ImGui::SetCursorPosX( ImGui::GetWindowWidth() - projectWidth );
	ImGui::TextDisabled( "%s", project.c_str() );

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
}

} // namespace GrizzEditor

#endif // GRIZZ_EDITOR
