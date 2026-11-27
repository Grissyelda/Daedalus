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
#include "EditorShapes.h"
#include "EditorBrushOps.h"

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

// the active viewport tool
enum gizmoMode_t
{
	GIZMO_MOVE,
	GIZMO_ROTATE,
	GIZMO_RESIZE,
	GIZMO_CLIP,			// cut brushes along a plane through clicked points
	GIZMO_VERTEX,		// drag brush corners
	GIZMO_COUNT
};
static gizmoMode_t	gizmoMode = GIZMO_MOVE;
static bool			gizmoDragging = false;
static unsigned int	dockspaceId = 0;
static idMat4		gizmoStart;			// the handle's matrix when the drag began
static idMat4		gizmoMatrix;		// and now

static ShapeSettings	shapeSettings;

// clip tool
static idList<idVec3>	clipPoints;
static idVec3		clipFirstNormal;
static int			clipKeep = 0;			// 0 both, 1 behind the plane, 2 in front

// corner tool
struct CornerRef
{
	int			entity;
	int			primitive;
	idVec3		position;
	bool		operator==( const CornerRef& other ) const
	{
		return entity == other.entity && primitive == other.primitive && position.Compare( other.position, 0.1f );
	}
};
static idList<CornerRef>	cornerSelection;
static bool			cornerDragging = false;
static idMat4		cornerStart;
static idMat4		cornerMatrix;
struct CornerBrushStart
{
	int			entity;
	int			primitive;
	idMapBrush* brush;
};
static idList<CornerBrushStart>	cornerStartBrushes;

// Open Project dialog
static bool			openProjectPopup = false;
static idStrList	projectList;
static int			faceRepeats[2] = { 1, 1 };
static idStr		statusMessage;
static int			statusTime = 0;

// New Project dialog
static char			newProjectName[64] = "MyGame";
static int			newProjectLook = -1;		// -1 = not chosen yet, 0 = stylized, 1 = PBR
static idStr		newProjectResult;
static bool			newProjectSucceeded = false;

static void ResizeSelection( const idBounds& bounds, const idVec3& size );
static void PlaceShape( shapeType_t type );
static void FitSelectedFaces();

static void SwitchProject( const char* name );
static void BeginCornerMove();
static void MoveCorners( const idVec3& delta );

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

static Selection SelectionFromArgs( const idCmdArgs& args )
{
	return Selection( atoi( args.Argv( 1 ) ), args.Argc() > 2 ? atoi( args.Argv( 2 ) ) : -1, args.Argc() > 3 ? atoi( args.Argv( 3 ) ) : -1 );
}

// lets tests and scripts drive the editor: editorSelect <entity> [primitive] [face]
static void EditorSelect_f( const idCmdArgs& args )
{
	document.selection.Clear();
	if( args.Argc() >= 2 )
	{
		document.selection.Set( SelectionFromArgs( args ) );
		document.ValidateSelection();
	}
}

static void EditorSelectAdd_f( const idCmdArgs& args )
{
	if( args.Argc() >= 2 )
	{
		document.selection.Add( SelectionFromArgs( args ) );
		document.ValidateSelection();
	}
}

static void EditorRotate_f( const idCmdArgs& args )
{
	idBounds bounds;
	if( args.Argc() != 2 || !document.CanTransformSelection() || !document.SelectionBounds( bounds ) )
	{
		common->Printf( "usage: editorRotate <degrees> (turns the selection about its centre)\n" );
		return;
	}
	const idVec3 pivot = bounds.GetCenter();
	const float angle = DEG2RAD( atof( args.Argv( 1 ) ) );
	idMat3 turn( cos( angle ), -sin( angle ), 0, sin( angle ), cos( angle ), 0, 0, 0, 1 );
	idVec3 turned( turn[0] * pivot, turn[1] * pivot, turn[2] * pivot );
	document.BeginChange( "Rotate" );
	document.TransformSelection( turn, pivot - turned );
	document.EndChange();
}

static void EditorResize_f( const idCmdArgs& args )
{
	idBounds bounds;
	if( args.Argc() != 4 || !document.CanTransformSelection() || !document.SelectionBounds( bounds ) )
	{
		common->Printf( "usage: editorResize <x> <y> <z> (new size of the selection)\n" );
		return;
	}
	document.BeginChange( "Resize" );
	ResizeSelection( bounds, idVec3( atof( args.Argv( 1 ) ), atof( args.Argv( 2 ) ), atof( args.Argv( 3 ) ) ) );
	document.EndChange();
}

static void EditorShape_f( const idCmdArgs& args )
{
	for( int i = 0; i < SHAPE_COUNT; i++ )
	{
		if( args.Argc() >= 2 && idStr::Icmp( args.Argv( 1 ), ShapeName( ( shapeType_t )i ) ) == 0 )
		{
			PlaceShape( ( shapeType_t )i );
			return;
		}
	}
	common->Printf( "usage: editorShape <box|room|ramp|cylinder|cone|stairs|arch>\n" );
}

// editorAddShape <name> <x1 y1 z1> <x2 y2 z2>: a shape filling exactly that box
static void EditorAddShape_f( const idCmdArgs& args )
{
	if( args.Argc() != 8 )
	{
		common->Printf( "usage: editorAddShape <box|room|ramp|cylinder|cone|stairs|arch> <x1> <y1> <z1> <x2> <y2> <z2>\n" );
		return;
	}
	for( int i = 0; i < SHAPE_COUNT; i++ )
	{
		if( idStr::Icmp( args.Argv( 1 ), ShapeName( ( shapeType_t )i ) ) != 0 )
		{
			continue;
		}
		idBounds box;
		box.Clear();
		box.AddPoint( idVec3( atof( args.Argv( 2 ) ), atof( args.Argv( 3 ) ), atof( args.Argv( 4 ) ) ) );
		box.AddPoint( idVec3( atof( args.Argv( 5 ) ), atof( args.Argv( 6 ) ), atof( args.Argv( 7 ) ) ) );
		idList<idMapBrush*> brushes;
		if( MakeShape( ( shapeType_t )i, shapeSettings, box, "textures/dev/grid_wall", brushes ) )
		{
			document.AddBrushes( brushes, va( "Add %s", ShapeName( ( shapeType_t )i ) ) );
		}
		return;
	}
	common->Printf( "unknown shape '%s'\n", args.Argv( 1 ) );
}

static void EditorMaterial_f( const idCmdArgs& args )
{
	if( args.Argc() != 2 )
	{
		common->Printf( "usage: editorMaterial <material> (on the selected brushes or faces)\n" );
		return;
	}
	document.SetMaterial( args.Argv( 1 ) );
}

static void EditorCarve_f( const idCmdArgs& args )
{
	common->Printf( "Editor: carve made %d pieces\n", document.SubtractSelection() );
}

// editorClip <nx> <ny> <nz> <dist> [keep]: cut the selection with the plane n . p = dist
static void EditorClip_f( const idCmdArgs& args )
{
	if( args.Argc() < 5 )
	{
		common->Printf( "usage: editorClip <nx> <ny> <nz> <dist> [keep: 0 both, 1 behind, 2 in front]\n" );
		return;
	}
	idVec3 normal( atof( args.Argv( 1 ) ), atof( args.Argv( 2 ) ), atof( args.Argv( 3 ) ) );
	normal.Normalize();
	const idPlane plane( normal, atof( args.Argv( 4 ) ) );
	common->Printf( "Editor: clip made %d pieces\n", document.ClipSelection( plane, args.Argc() > 5 ? atoi( args.Argv( 5 ) ) : 0 ) );
}

static void EditorGroup_f( const idCmdArgs& args )
{
	if( args.Argc() != 2 )
	{
		common->Printf( "usage: editorGroup <classname> (selected level brushes become that entity)\n" );
		return;
	}
	common->Printf( "Editor: made entity %d\n", document.GroupSelection( args.Argv( 1 ) ) );
}

static void EditorUngroup_f( const idCmdArgs& args )
{
	document.UngroupSelection();
}

// editorMoveCorner <entity> <primitive> <x y z> <dx dy dz>
static void EditorMoveCorner_f( const idCmdArgs& args )
{
	if( args.Argc() != 9 )
	{
		common->Printf( "usage: editorMoveCorner <entity> <primitive> <x> <y> <z> <dx> <dy> <dz>\n" );
		return;
	}
	cornerSelection.Clear();
	CornerRef ref = { atoi( args.Argv( 1 ) ), atoi( args.Argv( 2 ) ), idVec3( atof( args.Argv( 3 ) ), atof( args.Argv( 4 ) ), atof( args.Argv( 5 ) ) ) };
	cornerSelection.Append( ref );
	document.BeginChange( "Move corners" );
	BeginCornerMove();
	MoveCorners( idVec3( atof( args.Argv( 6 ) ), atof( args.Argv( 7 ) ), atof( args.Argv( 8 ) ) ) );
	for( int i = 0; i < cornerStartBrushes.Num(); i++ )
	{
		delete cornerStartBrushes[i].brush;
	}
	cornerStartBrushes.Clear();
	cornerSelection.Clear();
	document.EndChange();
}

static void EditorOpenProject_f( const idCmdArgs& args )
{
	SwitchProject( args.Argc() > 1 ? args.Argv( 1 ) : "" );
}

static void EditorFitTexture_f( const idCmdArgs& args )
{
	FitSelectedFaces();
}

static void EditorMove_f( const idCmdArgs& args )
{
	if( args.Argc() != 4 || !document.CanTransformSelection() )
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
	cmdSystem->AddCommand( "editorSelectAdd", EditorSelectAdd_f, CMD_FL_SYSTEM, "adds to the selection: editorSelectAdd <entity> [primitive] [face]" );
	cmdSystem->AddCommand( "editorRotate", EditorRotate_f, CMD_FL_SYSTEM, "turns the selection: editorRotate <degrees>" );
	cmdSystem->AddCommand( "editorResize", EditorResize_f, CMD_FL_SYSTEM, "resizes the selection: editorResize <x> <y> <z>" );
	cmdSystem->AddCommand( "editorShape", EditorShape_f, CMD_FL_SYSTEM, "places a shape: editorShape <name>" );
	cmdSystem->AddCommand( "editorMaterial", EditorMaterial_f, CMD_FL_SYSTEM, "sets the material of the selection: editorMaterial <name>" );
	cmdSystem->AddCommand( "editorFitTexture", EditorFitTexture_f, CMD_FL_SYSTEM, "fits the texture to the selected faces" );
	cmdSystem->AddCommand( "editorAddShape", EditorAddShape_f, CMD_FL_SYSTEM, "adds a shape filling a box: editorAddShape <name> <x1 y1 z1> <x2 y2 z2>" );
	cmdSystem->AddCommand( "editorCarve", EditorCarve_f, CMD_FL_SYSTEM, "carves the selected brushes out of the brushes they overlap" );
	cmdSystem->AddCommand( "editorClip", EditorClip_f, CMD_FL_SYSTEM, "cuts the selection: editorClip <nx> <ny> <nz> <dist> [keep]" );
	cmdSystem->AddCommand( "editorGroup", EditorGroup_f, CMD_FL_SYSTEM, "selected level brushes become an entity: editorGroup <classname>" );
	cmdSystem->AddCommand( "editorUngroup", EditorUngroup_f, CMD_FL_SYSTEM, "a brush entity's brushes go back to the level" );
	cmdSystem->AddCommand( "editorMoveCorner", EditorMoveCorner_f, CMD_FL_SYSTEM, "moves a brush corner: editorMoveCorner <entity> <primitive> <x y z> <dx dy dz>" );
	cmdSystem->AddCommand( "editorOpenProject", EditorOpenProject_f, CMD_FL_SYSTEM, "restarts into a project: editorOpenProject [name]" );
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

// projects are folders next to the engine with a project.gt4 in them
static void RefreshProjectList()
{
	projectList.Clear();
	const char* basePath = cvarSystem->GetCVarString( "fs_basepath" );
	idStrList folders;
	Sys_ListFiles( basePath, "/", folders );
	for( int i = 0; i < folders.Num(); i++ )
	{
		if( folders[i] == "." || folders[i] == ".." )
		{
			continue;
		}
		idStr projectFile = basePath;
		projectFile.AppendPath( folders[i] );
		projectFile.AppendPath( "project.gt4" );
		idFile* f = fileSystem->OpenExplicitFileRead( projectFile.c_str() );
		if( f != NULL )
		{
			fileSystem->CloseFile( f );
			projectList.Append( folders[i] );
		}
	}
	projectList.SortWithTemplate( idSort_PathStr() );
}

// the engine reads its content folders once at startup, so switching restarts it
static void SwitchProject( const char* name )
{
	if( document.IsDirty() && document.IsOpen() )
	{
		document.Save();
	}
	const char* args[3] = { "+set", "fs_game", name };
	common->Printf( "Editor: restarting into project '%s'\n", name[0] ? name : "(engine content)" );
	Sys_ReLaunchWithArgs( args, 3 );
}

static void DrawOpenProjectDialog()
{
	if( openProjectPopup )
	{
		ImGui::OpenPopup( "Open Project" );
		openProjectPopup = false;
		RefreshProjectList();
	}

	ImGui::SetNextWindowSize( ImVec2( 460, 0 ), ImGuiCond_Appearing );
	if( !ImGui::BeginPopupModal( "Open Project", NULL, ImGuiWindowFlags_NoResize ) )
	{
		return;
	}

	const char* current = cvarSystem->GetCVarString( "fs_game" );
	Hint( "GrizzTech4 restarts to open a project. Unsaved changes to the open map are saved first." );
	ImGui::Separator();

	if( projectList.Num() == 0 )
	{
		ImGui::TextDisabled( "No projects yet. Use File > New Project to make one." );
	}
	for( int i = 0; i < projectList.Num(); i++ )
	{
		const bool isCurrent = projectList[i].Icmp( current ) == 0;
		ImGui::BeginDisabled( isCurrent );
		if( ImGui::Selectable( va( "%s%s", projectList[i].c_str(), isCurrent ? "   (open)" : "" ) ) )
		{
			SwitchProject( projectList[i].c_str() );
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
	}

	ImGui::Separator();
	if( current[0] != '\0' && ImGui::Button( "Engine content only" ) )
	{
		SwitchProject( "" );
		ImGui::CloseCurrentPopup();
	}
	if( current[0] != '\0' )
	{
		ImGui::SameLine();
	}
	if( ImGui::Button( "Cancel" ) )
	{
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
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
		ImGui::TextWrapped( "Open it now? GrizzTech4 restarts into the new project. You can also open it later from File > Open Project." );
		if( ImGui::Button( "Open it now" ) )
		{
			SwitchProject( newProjectName );
			ImGui::CloseCurrentPopup();
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
	if( document.SelectionBounds( bounds ) )
	{
		view.camera.Focus( bounds );
	}
}

// a click in a list: Ctrl adds or removes, otherwise it replaces the selection
static void ClickSelect( const Selection& sel )
{
	if( ImGui::GetIO().KeyCtrl )
	{
		document.selection.Toggle( sel );
	}
	else
	{
		document.selection.Set( sel );
	}
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
				const bool selected = document.selection.Contains( Selection( 0, p ) );
				if( ImGui::Selectable( va( "%s %d##prim%d", isBrush ? "Brush" : "Patch", p, p ), selected, ImGuiSelectableFlags_AllowDoubleClick ) )
				{
					ClickSelect( Selection( 0, p ) );
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
		const bool selected = document.selection.Contains( Selection( i ) );
		if( ImGui::Selectable( va( "%s##ent%d", label.c_str(), i ), selected, ImGuiSelectableFlags_AllowDoubleClick ) )
		{
			ClickSelect( Selection( i ) );
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

// the door's settings in plain terms; the raw keys are still in the table below
static void DrawDoorPanel( idMapEntity* ent, const idDeclEntityDef* def )
{
	idDict args;
	if( def != NULL )
	{
		args.Copy( def->dict );
	}
	args.Copy( ent->epairs );

	ImGui::Separator();
	ImGui::Text( "Door" );
	Hint( "The green arrow in the view shows where it slides to. It opens when the player comes near." );

	static const char* directions[] = { "Up", "Down", "East (+X)", "North (+Y)", "West (-X)", "South (-Y)" };
	static const float dirValues[] = { -1, -2, 0, 90, 180, 270 };
	const float current = args.GetFloat( "movedir", args.GetString( "angle", "0" ) );
	int dirIndex = 0;
	for( int i = 0; i < 6; i++ )
	{
		if( idMath::Fabs( current - dirValues[i] ) < 0.5f )
		{
			dirIndex = i;
		}
	}
	ImGui::SetNextItemWidth( 140.0f );
	if( ImGui::Combo( "Opens", &dirIndex, directions, 6 ) )
	{
		document.BeginChange( "Door direction" );
		ent->epairs.SetFloat( "movedir", dirValues[dirIndex] );
		ent->epairs.Delete( "angle" );
		document.EndChange();
	}

	float speed = args.GetFloat( "speed", "160" );
	ImGui::SetNextItemWidth( 140.0f );
	if( ImGui::SliderFloat( "Speed", &speed, 20.0f, 800.0f, "%.0f units/s" ) )
	{
		ent->epairs.SetFloat( "speed", idMath::Rint( speed ) );
		document.EndChange();
	}
	UndoBracket( "Door speed" );

	float wait = args.GetFloat( "wait", "3" );
	bool staysOpen = wait < 0.0f;
	if( ImGui::Checkbox( "Stays open", &staysOpen ) )
	{
		document.BeginChange( "Door wait" );
		ent->epairs.SetFloat( "wait", staysOpen ? -1.0f : 3.0f );
		document.EndChange();
	}
	if( !staysOpen )
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 120.0f );
		if( ImGui::SliderFloat( "##closes", &wait, 0.5f, 30.0f, "closes after %.1f s" ) )
		{
			ent->epairs.SetFloat( "wait", idMath::Rint( wait * 10.0f ) / 10.0f );
			document.EndChange();
		}
		UndoBracket( "Door wait" );
	}

	bool startOpen = args.GetBool( "start_open" );
	if( ImGui::Checkbox( "Starts open", &startOpen ) )
	{
		document.BeginChange( "Door start open" );
		ent->epairs.SetBool( "start_open", startOpen );
		document.EndChange();
	}
	ImGui::SameLine();
	bool locked = args.GetInt( "locked" ) != 0;
	if( ImGui::Checkbox( "Locked", &locked ) )
	{
		document.BeginChange( "Door locked" );
		ent->epairs.SetInt( "locked", locked ? 1 : 0 );
		document.EndChange();
	}
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
	else if( def == NULL && document.selection.Primary().entity != 0 )
	{
		ImGui::TextColored( ImVec4( 1.0f, 0.6f, 0.3f, 1.0f ), "No entity definition called \"%s\" exists.", classname );
	}
	if( ent->GetNumPrimitives() > 0 )
	{
		ImGui::TextDisabled( "%d brushes", ent->GetNumPrimitives() );
		if( document.selection.Primary().entity != 0 )
		{
			ImGui::SameLine();
			if( ImGui::SmallButton( "Return brushes to the level" ) )
			{
				document.UngroupSelection();
				ImGui::Separator();
				return;
			}
		}
	}
	if( idStr::Icmp( classname, "func_door" ) == 0 )
	{
		DrawDoorPanel( ent, def );
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

			const bool isWorld = document.selection.Primary().entity == 0;
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
					// a brush entity's model is named after it, so renaming moves the model name too
					if( key.Icmp( "name" ) == 0 && ent->GetNumPrimitives() > 0 && value.Icmp( ent->epairs.GetString( "model" ) ) == 0 )
					{
						ent->epairs.Set( "model", buffer );
					}
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

// Resizes about the bottom centre, so things stay on the floor they stand on.
// No undo step of its own: callers wrap it.
static void ResizeSelection( const idBounds& bounds, const idVec3& size )
{
	const idVec3 oldSize = bounds[1] - bounds[0];
	idVec3 scale;
	for( int i = 0; i < 3; i++ )
	{
		scale[i] = ( oldSize[i] > 0.01f && size[i] >= 1.0f ) ? size[i] / oldSize[i] : 1.0f;
	}
	if( scale.Compare( idVec3( 1, 1, 1 ), 0.0001f ) )
	{
		return;
	}

	const idVec3 anchor( ( bounds[0].x + bounds[1].x ) * 0.5f, ( bounds[0].y + bounds[1].y ) * 0.5f, bounds[0].z );
	const idMat3 linear( scale.x, 0, 0, 0, scale.y, 0, 0, 0, scale.z );
	const idVec3 translation( anchor.x - anchor.x * scale.x, anchor.y - anchor.y * scale.y, anchor.z - anchor.z * scale.z );
	document.TransformSelection( linear, translation );
}

static void DrawTransformFields()
{
	idBounds bounds;
	if( !document.CanTransformSelection() || !document.SelectionBounds( bounds ) )
	{
		return;
	}

	const float grid = editor_snap.GetBool() ? GridSize() : 1.0f;

	// position: the bottom centre, which is where things stand
	idVec3 position( ( bounds[0].x + bounds[1].x ) * 0.5f, ( bounds[0].y + bounds[1].y ) * 0.5f, bounds[0].z );
	const idVec3 oldPosition = position;
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled( "Position" );
	ImGui::SameLine( 70.0f );
	ImGui::SetNextItemWidth( -1 );
	// these drags are bracketed by UndoBracket: one undo step per drag
	if( ImGui::DragFloat3( "##position", position.ToFloatPtr(), grid * 0.25f, 0, 0, "%.0f" ) )
	{
		document.TranslateSelection( position - oldPosition );
		document.EndChange();
	}
	UndoBracket( "Move" );
	if( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "Position of the bottom centre" );
	}

	idVec3 size = bounds[1] - bounds[0];
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled( "Size" );
	ImGui::SameLine( 70.0f );
	ImGui::SetNextItemWidth( -1 );
	if( ImGui::DragFloat3( "##size", size.ToFloatPtr(), grid * 0.25f, 1.0f, 65536.0f, "%.0f" ) )
	{
		ResizeSelection( bounds, size );
		document.EndChange();
	}
	UndoBracket( "Resize" );
	if( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "Size (width, depth, height)" );
	}
}

static void DrawMaterialField( const char* current, int key )
{
	if( brushMaterialFor != key )
	{
		idStr::Copynz( brushMaterialBuffer, current, sizeof( brushMaterialBuffer ) );
		brushMaterialFor = key;
	}

	ImGui::Text( "Material" );
	ImGui::SetNextItemWidth( -60.0f );
	const bool entered = ImGui::InputText( "##material", brushMaterialBuffer, sizeof( brushMaterialBuffer ), ImGuiInputTextFlags_EnterReturnsTrue );
	ImGui::SameLine();
	if( ImGui::Button( "Apply" ) || entered )
	{
		document.SetMaterial( brushMaterialBuffer );
	}
	Hint( "Or click a material in the Content Browser's Materials tab." );
}

static void FitSelectedFaces()
{
	if( !document.selection.IsFaces() )
	{
		return;
	}
	document.BeginChange( "Fit texture" );
	for( int i = 0; i < document.selection.Num(); i++ )
	{
		const Selection& sel = document.selection[i];
		idMapBrush* brush = static_cast<idMapBrush*>( document.Map()->GetEntity( sel.entity )->GetPrimitive( sel.primitive ) );
		FitTexture( brush, sel.side, ( float )faceRepeats[0], ( float )faceRepeats[1] );
	}
	document.EndChange();
}

// applies an edit to every selected face; live edits (drags) get their undo step from UndoBracket
template< typename func_t >
static void EditFaces( const char* label, func_t edit, bool live = false )
{
	if( !live )
	{
		document.BeginChange( label );
	}
	for( int i = 0; i < document.selection.Num(); i++ )
	{
		const Selection& sel = document.selection[i];
		idMapBrush* brush = static_cast<idMapBrush*>( document.Map()->GetEntity( sel.entity )->GetPrimitive( sel.primitive ) );
		idMapBrushSide* side = brush->GetSide( sel.side );
		TextureAlign align = GetTextureAlign( side );
		edit( align );
		SetTextureAlign( side, align );
	}
	document.EndChange();
}

static void DrawFaceInspector()
{
	idMapBrushSide* face = document.SelectedFace();
	const int count = document.selection.Num();
	ImGui::Text( count > 1 ? "%d faces" : "Face", count );
	Hint( "Shift+click more faces to add them; click without Shift to go back to whole objects." );
	ImGui::Separator();

	const Selection primary = document.selection.Primary();
	DrawMaterialField( face->GetMaterial(), ( primary.entity * 1000 + primary.primitive ) * 100 + primary.side + 50000000 );
	ImGui::Separator();

	ImGui::Text( "Texture" );
	ImGui::Checkbox( "Texture lock", &document.textureLock );
	if( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "Keep textures in place on surfaces when you move, turn or resize brushes." );
	}

	const TextureAlign align = GetTextureAlign( face );
	const float grid = GridSize();

	idVec2 offset = align.offset;
	ImGui::SetNextItemWidth( -80.0f );
	if( ImGui::DragFloat2( "Offset", offset.ToFloatPtr(), 1.0f, 0, 0, "%.1f" ) )
	{
		const idVec2 delta = offset - align.offset;
		EditFaces( "Texture offset", [&]( TextureAlign & a )
		{
			a.offset += delta;
		}, true );
	}
	UndoBracket( "Texture offset" );

	idVec2 tile = align.tile;
	ImGui::SetNextItemWidth( -80.0f );
	if( ImGui::DragFloat2( "Tile size", tile.ToFloatPtr(), 1.0f, -4096.0f, 4096.0f, "%.1f" ) )
	{
		EditFaces( "Texture size", [&]( TextureAlign & a )
		{
			a.tile = tile;
		}, true );
	}
	UndoBracket( "Texture size" );
	if( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "Map units covered by one copy of the texture. Negative flips it." );
	}

	float rotation = align.rotation;
	ImGui::SetNextItemWidth( -80.0f );
	if( ImGui::DragFloat( "Rotation", &rotation, 1.0f, -360.0f, 360.0f, "%.1f deg" ) )
	{
		EditFaces( "Texture rotation", [&]( TextureAlign & a )
		{
			a.rotation = rotation;
		}, true );
	}
	UndoBracket( "Texture rotation" );

	// nudges by the grid, for lining up without dragging
	ImGui::TextDisabled( "Nudge" );
	ImGui::SameLine();
	if( ImGui::ArrowButton( "##left", ImGuiDir_Left ) )
	{
		EditFaces( "Texture offset", [&]( TextureAlign & a )
		{
			a.offset.x -= grid;
		} );
	}
	ImGui::SameLine();
	if( ImGui::ArrowButton( "##right", ImGuiDir_Right ) )
	{
		EditFaces( "Texture offset", [&]( TextureAlign & a )
		{
			a.offset.x += grid;
		} );
	}
	ImGui::SameLine();
	if( ImGui::ArrowButton( "##up", ImGuiDir_Up ) )
	{
		EditFaces( "Texture offset", [&]( TextureAlign & a )
		{
			a.offset.y -= grid;
		} );
	}
	ImGui::SameLine();
	if( ImGui::ArrowButton( "##down", ImGuiDir_Down ) )
	{
		EditFaces( "Texture offset", [&]( TextureAlign & a )
		{
			a.offset.y += grid;
		} );
	}
	ImGui::SameLine();
	if( ImGui::Button( "Turn 90" ) )
	{
		EditFaces( "Texture rotation", [&]( TextureAlign & a )
		{
			a.rotation = idMath::AngleNormalize180( a.rotation + 90.0f );
		} );
	}

	if( ImGui::Button( "Flip U" ) )
	{
		EditFaces( "Flip texture", [&]( TextureAlign & a )
		{
			a.tile.x = -a.tile.x;
		} );
	}
	ImGui::SameLine();
	if( ImGui::Button( "Flip V" ) )
	{
		EditFaces( "Flip texture", [&]( TextureAlign & a )
		{
			a.tile.y = -a.tile.y;
		} );
	}
	ImGui::SameLine();
	if( ImGui::Button( "Reset" ) )
	{
		EditFaces( "Reset texture", [&]( TextureAlign & a )
		{
			a.offset.Zero();
			a.tile.Set( 128.0f, 128.0f );
			a.rotation = 0.0f;
		} );
	}
	if( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "Straight, unshifted, one copy per 128 units" );
	}

	if( ImGui::Button( "Fit" ) )
	{
		FitSelectedFaces();
	}
	if( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "Stretch the texture to cover the face exactly the number of times set here" );
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 90.0f );
	ImGui::InputInt2( "times", faceRepeats );
	faceRepeats[0] = idMath::ClampInt( 1, 64, faceRepeats[0] );
	faceRepeats[1] = idMath::ClampInt( 1, 64, faceRepeats[1] );
}

// carve, and turning level brushes into doors and other brush entities
static void DrawBrushTools()
{
	idList<Selection> brushes;
	document.SelectedBrushes( brushes );
	if( brushes.Num() == 0 )
	{
		return;
	}

	ImGui::Separator();
	if( ImGui::Button( "Carve" ) )
	{
		const int pieces = document.SubtractSelection();
		Status( pieces > 0 ? va( "Carved out; %d pieces left around it.", pieces ) : "Nothing to carve: the selection doesn't overlap any other brush." );
		return;
	}
	if( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "Cut the selected brushes out of every brush they overlap, then remove them.\nUse it for doorways, windows and alcoves." );
	}
	ImGui::SameLine();
	if( ImGui::Button( "Clip..." ) )
	{
		gizmoMode = GIZMO_CLIP;
		clipPoints.Clear();
	}
	if( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "Cut the selected brushes in two along a line you click (C)" );
	}
	ImGui::SameLine();
	if( ImGui::Button( "Corners..." ) )
	{
		gizmoMode = GIZMO_VERTEX;
		cornerSelection.Clear();
	}
	if( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "Drag the corners of the selected brushes (V)" );
	}

	// only the level's own brushes can be turned into something
	bool allLevel = true;
	for( int i = 0; i < document.selection.Num(); i++ )
	{
		allLevel &= ( document.selection[i].entity == 0 );
	}
	if( !allLevel )
	{
		return;
	}

	ImGui::TextDisabled( "Turn into" );
	const int count = declManager->GetNumDecls( DECL_ENTITYDEF );
	int shown = 0;
	for( int i = 0; i < count; i++ )
	{
		const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->DeclByIndex( DECL_ENTITYDEF, i, true ) );
		if( def == NULL || !def->dict.GetBool( "editor_brushes" ) )
		{
			continue;
		}
		if( shown++ > 0 )
		{
			ImGui::SameLine();
		}
		if( ImGui::Button( def->dict.GetString( "editor_label", def->GetName() ) ) )
		{
			if( document.GroupSelection( def->GetName() ) >= 0 )
			{
				Status( va( "Made a %s.", def->dict.GetString( "editor_label", def->GetName() ) ) );
			}
		}
		if( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "%s", def->dict.GetString( "editor_usage" ) );
		}
	}
}

static void DrawPrimitiveInspector( idMapPrimitive* prim )
{
	const bool isBrush = prim->GetType() == idMapPrimitive::TYPE_BRUSH;
	const int count = document.selection.Num();
	if( count > 1 )
	{
		ImGui::Text( "%d selected", count );
	}
	else
	{
		ImGui::Text( isBrush ? "Brush" : "Patch" );
	}
	DrawTransformFields();
	DrawBrushTools();
	ImGui::Separator();

	const Selection primary = document.selection.Primary();
	const char* current = isBrush ? static_cast<idMapBrush*>( prim )->GetSide( 0 )->GetMaterial() : static_cast<idMapPatch*>( prim )->GetMaterial();
	DrawMaterialField( current, primary.entity * 100000 + primary.primitive );

	if( isBrush )
	{
		idMapBrush* brush = static_cast<idMapBrush*>( prim );
		Hint( "Shift+click a face in the view to change just that face." );
		if( ImGui::TreeNode( "Faces", "Faces (%d)", brush->GetNumSides() ) )
		{
			for( int s = 0; s < brush->GetNumSides(); s++ )
			{
				const idVec3 n = brush->GetSide( s )->GetPlane().Normal();
				if( ImGui::Selectable( va( "%d  (%.2f %.2f %.2f)  %s##face%d", s, n.x, n.y, n.z, brush->GetSide( s )->GetMaterial(), s ) ) )
				{
					document.selection.Set( Selection( primary.entity, primary.primitive, s ) );
				}
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
		Hint( "Click something in the view or in the Outliner to see and change its properties. Ctrl+click adds to the selection; Shift+click picks a single face." );
		if( document.IsOpen() && ImGui::Button( "Level settings" ) )
		{
			document.selection.Set( Selection( 0 ) );
		}
		ImGui::End();
		return;
	}

	if( document.selection.IsFaces() )
	{
		DrawFaceInspector();
	}
	else if( document.selection.Primary().IsPrimitive() )
	{
		DrawPrimitiveInspector( document.SelectedPrimitive() );
	}
	else
	{
		if( document.selection.Num() > 1 )
		{
			ImGui::Text( "%d selected", document.selection.Num() );
			DrawTransformFields();
			ImGui::Separator();
			ImGui::TextDisabled( "Showing the last one picked:" );
		}
		DrawEntityInspector( document.SelectedEntity() );
	}

	ImGui::End();
}

// where new things go: on whatever surface is in the middle of the view,
// or a little in front of the camera when it looks at nothing
static idVec3 PlacementPoint( idVec3* surfaceNormal = NULL )
{
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	idVec3 point, normal;
	if( view.Trace( document, size.x * 0.5f, size.y * 0.5f, point, normal ) && ( point - view.camera.origin ).LengthSqr() < Square( 4096.0f ) )
	{
		if( surfaceNormal != NULL )
		{
			*surfaceNormal = normal;
		}
		return SnapToGrid( point + normal );
	}

	if( surfaceNormal != NULL )
	{
		surfaceNormal->Set( 0, 0, 1 );
	}
	const idMat3 axis = view.camera.Axis();
	return SnapToGrid( view.camera.origin + axis[0] * 256.0f );
}

static void PlaceShape( shapeType_t type )
{
	if( !document.IsOpen() )
	{
		return;
	}

	// sit on floors, stand out from walls, hang from ceilings
	idVec3 normal;
	const idVec3 point = PlacementPoint( &normal );
	const idVec3 size = ShapeDefaultSize( type );
	idVec3 center = point;
	if( normal.z > 0.7f )
	{
		center.z = point.z + size.z * 0.5f;
	}
	else if( normal.z < -0.7f )
	{
		center.z = point.z - size.z * 0.5f;
	}
	else
	{
		center += normal * ( idMath::Fabs( normal.x ) * size.x + idMath::Fabs( normal.y ) * size.y ) * 0.5f;
	}
	center = SnapToGrid( center - size * 0.5f ) + size * 0.5f;

	idList<idMapBrush*> brushes;
	if( !MakeShape( type, shapeSettings, idBounds( center - size * 0.5f, center + size * 0.5f ), "textures/dev/grid_wall", brushes ) )
	{
		Status( va( "Couldn't make a %s with those settings.", ShapeName( type ) ) );
		return;
	}
	document.AddBrushes( brushes, va( "Add %s", ShapeName( type ) ) );
	Status( va( "Added a %s.", ShapeName( type ) ) );
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
			Hint( !document.selection.IsEmpty() ? "Click a material to put it on the selection." : "Select brushes or faces, then click a material to apply it." );
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
				if( ImGui::Selectable( decl->GetName() ) && !document.selection.IsEmpty() )
				{
					document.SetMaterial( decl->GetName() );
					idStr::Copynz( brushMaterialBuffer, decl->GetName(), sizeof( brushMaterialBuffer ) );
				}
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if( ImGui::BeginTabItem( "Place" ) )
		{
			const bool canPlace = document.IsOpen();
			Hint( "New things land on whatever is in the middle of the view." );

			ImGui::TextDisabled( "Shapes" );
			ImGui::BeginDisabled( !canPlace );
			for( int i = 0; i < SHAPE_COUNT; i++ )
			{
				if( i > 0 )
				{
					ImGui::SameLine();
				}
				if( ImGui::Button( ShapeName( ( shapeType_t )i ) ) )
				{
					PlaceShape( ( shapeType_t )i );
				}
				if( ImGui::IsItemHovered() )
				{
					ImGui::SetTooltip( "%s", ShapeHelp( ( shapeType_t )i ) );
				}
			}
			ImGui::EndDisabled();

			if( ImGui::TreeNode( "Shape settings" ) )
			{
				ImGui::SetNextItemWidth( 160.0f );
				ImGui::SliderInt( "Sides (cylinder, cone)", &shapeSettings.sides, 3, 32 );
				ImGui::SetNextItemWidth( 160.0f );
				ImGui::SliderInt( "Steps (stairs)", &shapeSettings.steps, 2, 32 );
				ImGui::SetNextItemWidth( 160.0f );
				ImGui::SliderInt( "Segments (arch)", &shapeSettings.segments, 2, 24 );
				ImGui::SetNextItemWidth( 160.0f );
				ImGui::DragFloat( "Thickness (arch, room)", &shapeSettings.thickness, 1.0f, 1.0f, 256.0f, "%.0f" );
				Hint( "Resize shapes afterwards in the Inspector or with the resize tool (R)." );
				ImGui::TreePop();
			}
			ImGui::Separator();
			ImGui::TextDisabled( "Things" );

			ImGui::SetNextItemWidth( 240.0f );
			ImGui::InputTextWithHint( "##entfilter", "Filter entity types", entityFilter, sizeof( entityFilter ) );
			ImGui::Separator();

			ImGui::BeginChild( "entities" );
			const int count = declManager->GetNumDecls( DECL_ENTITYDEF );
			for( int i = 0; i < count; i++ )
			{
				const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->DeclByIndex( DECL_ENTITYDEF, i, true ) );
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
			if( ent->GetNumPrimitives() > 0 && idStr::Icmp( ent->epairs.GetString( "model" ), ent->epairs.GetString( "name" ) ) != 0 )
			{
				any = true;
				ImGui::PushID( i + 100000 );
				if( ImGui::SmallButton( "Fix" ) )
				{
					document.BeginChange( "Fix model name" );
					map->GetEntity( i )->epairs.Set( "model", ent->epairs.GetString( "name" ) );
					document.EndChange();
				}
				ImGui::SameLine();
				ImGui::TextWrapped( "\"%s\" is made of brushes, but its 'model' doesn't match its name, so it won't appear in the game.", EntityLabel( ent, i ).c_str() );
				ImGui::PopID();
			}
			if( declManager->FindType( DECL_ENTITYDEF, classname, false ) == NULL )
			{
				any = true;
				ImGui::PushID( i );
				if( ImGui::SmallButton( "Show" ) )
				{
					document.selection.Set( Selection( i ) );
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
		Hint( "Click to select, Ctrl+click to add or remove, Shift+click to pick faces for texturing. W, E and R switch between moving, turning and resizing; C clips brushes and V drags their corners; [ and ] change the grid. Carve (in the Inspector) cuts doorways and windows out of walls. Ctrl+Z undo, Ctrl+Y redo, Ctrl+D duplicate, Delete removes, F frames the selection, Ctrl+S saves." );
	}
	ImGui::End();
}

/*
===============================================================================

	Viewport: picking, the move handle and shortcuts

===============================================================================
*/

static void EndGizmoDrag()
{
	if( !gizmoDragging )
	{
		return;
	}
	gizmoDragging = false;
	document.EndTransform();
	if( document.IsChangeOpen() )
	{
		const idMat4 moved = gizmoMatrix.Transpose() * gizmoStart.Transpose().Inverse();
		if( moved.Compare( mat4_identity, 0.0001f ) )
		{
			document.CancelChange();	// grabbed but never moved
		}
		else
		{
			document.EndChange();
		}
	}
}

static void DrawGizmo()
{
	idBounds bounds;
	if( !document.CanTransformSelection() || !document.SelectionBounds( bounds ) )
	{
		EndGizmoDrag();
		return;
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

	// the handle sits in the middle of the selection, or on a lone point entity's origin
	idVec3 pivot = bounds.GetCenter();
	const Selection primary = document.selection.Primary();
	if( document.selection.Num() == 1 && primary.primitive < 0 && document.SelectedEntity()->GetNumPrimitives() == 0 )
	{
		pivot = document.SelectedEntity()->epairs.GetVector( "origin" );
	}

	if( !gizmoDragging )
	{
		// ImGuizmo wants OpenGL's column-major layout, which is idMat4 transposed
		gizmoMatrix = idMat4( mat3_identity, pivot ).Transpose();
	}

	const bool snapping = editor_snap.GetBool();
	const float grid = GridSize();
	float snap[3] = { grid, grid, grid };
	ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
	const char* label = "Move";
	float localBounds[6];
	float boundsSnap[3] = { grid, grid, grid };
	bool useBounds = false;

	if( gizmoMode == GIZMO_ROTATE )
	{
		operation = ImGuizmo::ROTATE;
		label = "Rotate";
		snap[0] = snap[1] = snap[2] = 15.0f;
	}
	else if( gizmoMode == GIZMO_RESIZE )
	{
		operation = ImGuizmo::BOUNDS;
		label = "Resize";
		useBounds = true;
		const idVec3 start = gizmoDragging ? gizmoStart.Transpose().GetTranslation() : pivot;
		static idBounds dragBounds;
		if( !gizmoDragging )
		{
			dragBounds = bounds;
		}
		for( int i = 0; i < 3; i++ )
		{
			localBounds[i] = dragBounds[0][i] - start[i];
			localBounds[i + 3] = dragBounds[1][i] - start[i];
		}
	}

	ImGuizmo::Manipulate( viewDef.worldSpace.modelViewMatrix, viewDef.unjitteredProjectionMatrix, operation, ImGuizmo::WORLD, gizmoMatrix.ToFloatPtr(), NULL,
						  snapping ? snap : NULL, useBounds ? localBounds : NULL, ( useBounds && snapping ) ? boundsSnap : NULL );

	if( ImGuizmo::IsUsing() )
	{
		if( !gizmoDragging )
		{
			gizmoDragging = true;
			gizmoStart = idMat4( mat3_identity, pivot ).Transpose();
			document.BeginChange( label );
			document.BeginTransform();
		}

		// the whole change since the drag began: start -> now
		const idMat4 total = gizmoMatrix.Transpose() * gizmoStart.Transpose().Inverse();
		idMat3 linear;
		for( int r = 0; r < 3; r++ )
		{
			linear[r].Set( total[r][0], total[r][1], total[r][2] );
		}
		const idVec3 translation( total[0][3], total[1][3], total[2][3] );
		document.UpdateTransform( linear, translation );
	}
	else
	{
		EndGizmoDrag();
	}
}

/*
===============================================================================

	Clip tool: click two or three points on surfaces; the plane through them
	cuts the selected brushes. With two points the cut stands straight up from
	the surface you clicked first.

===============================================================================
*/

static bool ClipPlane( idPlane& plane )
{
	idVec3 normal;
	if( clipPoints.Num() == 2 )
	{
		normal = ( clipPoints[1] - clipPoints[0] ).Cross( clipFirstNormal );
	}
	else if( clipPoints.Num() == 3 )
	{
		normal = ( clipPoints[1] - clipPoints[0] ).Cross( clipPoints[2] - clipPoints[0] );
	}
	else
	{
		return false;
	}
	if( normal.Normalize() < 1e-4f )
	{
		return false;
	}
	plane.SetNormal( normal );
	plane[3] = -( normal * clipPoints[0] );
	return true;
}

static void DrawScreenLine( const idVec3& a, const idVec3& b, ImU32 color, float thickness )
{
	idVec2 sa, sb;
	if( view.WorldToScreen( a, sa ) && view.WorldToScreen( b, sb ) )
	{
		ImGui::GetBackgroundDrawList()->AddLine( ImVec2( sa.x, sa.y ), ImVec2( sb.x, sb.y ), color, thickness );
	}
}

static void DrawScreenBrush( const idMapBrush* brush, ImU32 color, float thickness )
{
	for( int s = 0; s < brush->GetNumSides(); s++ )
	{
		idWinding w;
		if( BrushSideWinding( brush, s, w ) )
		{
			for( int j = 0; j < w.GetNumPoints(); j++ )
			{
				DrawScreenLine( w[j].ToVec3(), w[( j + 1 ) % w.GetNumPoints()].ToVec3(), color, thickness );
			}
		}
	}
}

static void ApplyClip()
{
	idPlane plane;
	if( !ClipPlane( plane ) )
	{
		Status( "Click two or three points on surfaces to set where to cut." );
		return;
	}
	const int made = document.ClipSelection( plane, clipKeep );
	Status( made > 0 ? va( "Cut into %d pieces.", made ) : "The cut doesn't cross any selected brush." );
	clipPoints.Clear();
}

static void DrawClipTool()
{
	const ImU32 keepColor = IM_COL32( 255, 200, 40, 255 );
	const ImU32 dropColor = IM_COL32( 255, 70, 60, 200 );
	ImDrawList* draw = ImGui::GetBackgroundDrawList();

	idPlane plane;
	if( ClipPlane( plane ) )
	{
		idList<Selection> brushes;
		document.SelectedBrushes( brushes );
		for( int i = 0; i < brushes.Num(); i++ )
		{
			const idMapBrush* brush = static_cast<const idMapBrush*>( document.Map()->GetEntity( brushes[i].entity )->GetPrimitive( brushes[i].primitive ) );
			idMapBrush* front = NULL;
			idMapBrush* back = NULL;
			SplitBrush( brush, plane, NULL, &front, &back );
			if( back != NULL )
			{
				DrawScreenBrush( back, clipKeep == 2 ? dropColor : keepColor, 2.0f );
			}
			if( front != NULL )
			{
				DrawScreenBrush( front, clipKeep == 1 ? dropColor : IM_COL32( 80, 200, 255, 255 ), 2.0f );
			}
			delete front;
			delete back;
		}
	}

	for( int i = 0; i < clipPoints.Num(); i++ )
	{
		idVec2 sp;
		if( view.WorldToScreen( clipPoints[i], sp ) )
		{
			draw->AddCircleFilled( ImVec2( sp.x, sp.y ), 6.0f, IM_COL32( 255, 255, 255, 255 ) );
			draw->AddText( ImVec2( sp.x + 8.0f, sp.y - 8.0f ), IM_COL32( 255, 255, 255, 255 ), va( "%d", i + 1 ) );
		}
	}
}

static void ClipToolClick( float x, float y )
{
	idVec3 point, normal;
	if( !view.Trace( document, x, y, point, normal ) )
	{
		return;
	}
	if( clipPoints.Num() >= 3 )
	{
		clipPoints.Clear();
	}
	if( clipPoints.Num() == 0 )
	{
		clipFirstNormal = normal;
	}
	clipPoints.Append( editor_snap.GetBool() ? SnapToGrid( point ) : point );
}

/*
===============================================================================

	Corner tool: the corners of the selected brushes as dots; click (Ctrl+click
	for more) and drag the arrows. The brush is rebuilt around its corners, so
	it always stays convex.

===============================================================================
*/

static void EndCornerDrag( bool keep )
{
	if( !cornerDragging )
	{
		return;
	}
	cornerDragging = false;

	const idVec3 delta = cornerMatrix.Transpose().GetTranslation() - cornerStart.Transpose().GetTranslation();
	if( keep && delta.LengthSqr() > 0.0001f )
	{
		for( int i = 0; i < cornerSelection.Num(); i++ )
		{
			cornerSelection[i].position += delta;
		}
		document.EndChange();
	}
	else if( document.IsChangeOpen() )
	{
		document.CancelChange();
	}

	for( int i = 0; i < cornerStartBrushes.Num(); i++ )
	{
		delete cornerStartBrushes[i].brush;
	}
	cornerStartBrushes.Clear();
}

// moves the selected corners of the brushes saved at the start of the drag; no undo step
static void MoveCorners( const idVec3& delta )
{
	for( int b = 0; b < cornerStartBrushes.Num(); b++ )
	{
		const CornerBrushStart& start = cornerStartBrushes[b];
		idList<idVec3> points;
		BrushVertices( start.brush, points );
		for( int p = 0; p < points.Num(); p++ )
		{
			const CornerRef ref = { start.entity, start.primitive, points[p] };
			if( cornerSelection.FindIndex( ref ) >= 0 )
			{
				points[p] += delta;
			}
		}
		idMapBrush* hull = HullBrush( points, start.brush );
		if( hull != NULL )
		{
			document.SetBrushGeometry( start.entity, start.primitive, hull );
			delete hull;
		}
	}
}

static void BeginCornerMove()
{
	cornerStartBrushes.Clear();
	for( int i = 0; i < cornerSelection.Num(); i++ )
	{
		bool have = false;
		for( int b = 0; b < cornerStartBrushes.Num() && !have; b++ )
		{
			have = cornerStartBrushes[b].entity == cornerSelection[i].entity && cornerStartBrushes[b].primitive == cornerSelection[i].primitive;
		}
		if( have )
		{
			continue;
		}
		const idMapBrush* brush = static_cast<const idMapBrush*>( document.Map()->GetEntity( cornerSelection[i].entity )->GetPrimitive( cornerSelection[i].primitive ) );
		CornerBrushStart& start = cornerStartBrushes.Alloc();
		start.entity = cornerSelection[i].entity;
		start.primitive = cornerSelection[i].primitive;
		start.brush = CopyBrush( brush );
	}
}

static void ValidateCorners()
{
	const idMapFile* map = document.Map();
	for( int i = cornerSelection.Num() - 1; i >= 0; i-- )
	{
		const CornerRef& ref = cornerSelection[i];
		bool ok = map != NULL && ref.entity >= 0 && ref.entity < map->GetNumEntities() && ref.primitive >= 0 && ref.primitive < map->GetEntity( ref.entity )->GetNumPrimitives();
		if( ok )
		{
			ok = map->GetEntity( ref.entity )->GetPrimitive( ref.primitive )->GetType() == idMapPrimitive::TYPE_BRUSH;
		}
		if( ok )
		{
			idList<idVec3> points;
			BrushVertices( static_cast<const idMapBrush*>( map->GetEntity( ref.entity )->GetPrimitive( ref.primitive ) ), points );
			ok = false;
			for( int p = 0; p < points.Num() && !ok; p++ )
			{
				ok = points[p].Compare( ref.position, 0.1f );
			}
		}
		if( !ok )
		{
			cornerSelection.RemoveIndex( i );
		}
	}
}

static void DrawCornerTool()
{
	if( !cornerDragging )
	{
		ValidateCorners();
	}

	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	idList<Selection> brushes;
	document.SelectedBrushes( brushes );
	for( int i = 0; i < brushes.Num(); i++ )
	{
		idList<idVec3> points;
		BrushVertices( static_cast<const idMapBrush*>( document.Map()->GetEntity( brushes[i].entity )->GetPrimitive( brushes[i].primitive ) ), points );
		for( int p = 0; p < points.Num(); p++ )
		{
			idVec2 sp;
			if( !view.WorldToScreen( points[p], sp ) )
			{
				continue;
			}
			const CornerRef ref = { brushes[i].entity, brushes[i].primitive, points[p] };
			if( cornerSelection.FindIndex( ref ) >= 0 )
			{
				draw->AddCircleFilled( ImVec2( sp.x, sp.y ), 6.0f, IM_COL32( 255, 200, 40, 255 ) );
			}
			else
			{
				draw->AddCircleFilled( ImVec2( sp.x, sp.y ), 4.5f, IM_COL32( 20, 20, 20, 200 ) );
				draw->AddCircle( ImVec2( sp.x, sp.y ), 5.0f, IM_COL32( 255, 255, 255, 255 ), 12, 1.5f );
			}
		}
	}

	if( cornerSelection.Num() == 0 )
	{
		EndCornerDrag( false );
		return;
	}

	// a move handle in the middle of the picked corners
	idVec3 pivot = vec3_origin;
	for( int i = 0; i < cornerSelection.Num(); i++ )
	{
		pivot += cornerSelection[i].position;
	}
	pivot /= ( float )cornerSelection.Num();

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
	ImGuizmo::SetID( 2 );

	if( !cornerDragging )
	{
		cornerMatrix = idMat4( mat3_identity, pivot ).Transpose();
	}
	const float grid = GridSize();
	const float snap[3] = { grid, grid, grid };
	ImGuizmo::Manipulate( viewDef.worldSpace.modelViewMatrix, viewDef.unjitteredProjectionMatrix, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, cornerMatrix.ToFloatPtr(), NULL, editor_snap.GetBool() ? snap : NULL );

	if( ImGuizmo::IsUsing() )
	{
		if( !cornerDragging )
		{
			cornerDragging = true;
			cornerStart = idMat4( mat3_identity, pivot ).Transpose();
			document.BeginChange( "Move corners" );
			BeginCornerMove();
		}
		MoveCorners( cornerMatrix.Transpose().GetTranslation() - cornerStart.Transpose().GetTranslation() );
	}
	else
	{
		EndCornerDrag( true );
	}
}

// a click near a corner picks it; true when one was there
static bool CornerToolClick( float x, float y, bool add )
{
	idList<Selection> brushes;
	document.SelectedBrushes( brushes );
	float bestDist = 10.0f;
	CornerRef best = { -1, -1, vec3_origin };
	for( int i = 0; i < brushes.Num(); i++ )
	{
		idList<idVec3> points;
		BrushVertices( static_cast<const idMapBrush*>( document.Map()->GetEntity( brushes[i].entity )->GetPrimitive( brushes[i].primitive ) ), points );
		for( int p = 0; p < points.Num(); p++ )
		{
			idVec2 sp;
			if( view.WorldToScreen( points[p], sp ) )
			{
				const float dist = ( sp - idVec2( x, y ) ).Length();
				if( dist < bestDist )
				{
					bestDist = dist;
					best.entity = brushes[i].entity;
					best.primitive = brushes[i].primitive;
					best.position = points[p];
				}
			}
		}
	}
	if( best.entity < 0 )
	{
		return false;
	}

	if( !add )
	{
		cornerSelection.Clear();
	}
	const int index = cornerSelection.FindIndex( best );
	if( index >= 0 )
	{
		cornerSelection.RemoveIndex( index );
	}
	else
	{
		cornerSelection.Append( best );
	}
	return true;
}

// the strip of tool buttons across the top of the 3D view
static void DrawViewportToolbar()
{
	ImGuiDockNode* central = ( dockspaceId != 0 ) ? ImGui::DockBuilderGetCentralNode( dockspaceId ) : NULL;
	if( central == NULL )
	{
		return;
	}

	ImGui::SetNextWindowPos( ImVec2( central->Pos.x + 8.0f, central->Pos.y + 8.0f ) );
	ImGui::SetNextWindowBgAlpha( 0.75f );
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
								   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
	if( ImGui::Begin( "##viewport_toolbar", NULL, flags ) )
	{
		const char* modes[] = { "Move (W)", "Rotate (E)", "Resize (R)", "Clip (C)", "Corners (V)" };
		const char* tips[] =
		{
			"Drag the arrows to move the selection",
			"Drag the rings to turn the selection; snaps to 15 degrees",
			"Drag the dots on the box edges to resize",
			"Cut the selected brushes: click two or three points on surfaces, then press Enter",
			"Drag the corners of the selected brushes: click a dot (Ctrl+click for more), then drag the arrows"
		};
		for( int i = 0; i < GIZMO_COUNT; i++ )
		{
			if( i > 0 )
			{
				ImGui::SameLine();
			}
			const bool active = ( gizmoMode == i );
			if( active )
			{
				ImGui::PushStyleColor( ImGuiCol_Button, ImGui::GetStyleColorVec4( ImGuiCol_ButtonActive ) );
			}
			if( ImGui::Button( modes[i] ) )
			{
				gizmoMode = ( gizmoMode_t )i;
			}
			if( active )
			{
				ImGui::PopStyleColor();
			}
			if( ImGui::IsItemHovered() )
			{
				ImGui::SetTooltip( "%s", tips[i] );
			}
		}

		ImGui::SameLine();
		ImGui::TextDisabled( "|" );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 64.0f );
		static const int gridSizes[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256 };
		if( ImGui::BeginCombo( "##grid", va( "Grid %d", editor_gridSize.GetInteger() ) ) )
		{
			for( int i = 0; i < 9; i++ )
			{
				if( ImGui::Selectable( va( "%d", gridSizes[i] ), editor_gridSize.GetInteger() == gridSizes[i] ) )
				{
					editor_gridSize.SetInteger( gridSizes[i] );
				}
			}
			ImGui::EndCombo();
		}
		if( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "Grid size. [ and ] make it smaller and bigger." );
		}

		ImGui::SameLine();
		bool snap = editor_snap.GetBool();
		if( ImGui::Checkbox( "Snap", &snap ) )
		{
			editor_snap.SetBool( snap );
		}
		ImGui::SameLine();
		ImGui::Checkbox( "Texture lock", &document.textureLock );
		if( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "Keep textures in place on surfaces when you move, turn or resize brushes" );
		}
		ImGui::SameLine();
		ImGui::Checkbox( "Wireframe", &view.wireframe );

		// what the clip and corner tools need from you right now
		if( gizmoMode == GIZMO_CLIP )
		{
			idList<Selection> brushes;
			document.SelectedBrushes( brushes );
			if( brushes.Num() == 0 )
			{
				ImGui::TextColored( ImVec4( 1.0f, 0.75f, 0.3f, 1.0f ), "Select the brushes to cut first (Esc, then click one), then come back to Clip." );
			}
			else
			{
				ImGui::Text( "Clip: %d of 2-3 points placed.", clipPoints.Num() );
				ImGui::SameLine();
				static const char* keepNames[] = { "Keep both", "Keep yellow", "Keep blue" };
				ImGui::SetNextItemWidth( 110.0f );
				ImGui::Combo( "##keep", &clipKeep, keepNames, 3 );
				if( ImGui::IsItemHovered() )
				{
					ImGui::SetTooltip( "Which side of the cut stays (Tab cycles)" );
				}
				ImGui::SameLine();
				ImGui::BeginDisabled( clipPoints.Num() < 2 );
				if( ImGui::Button( "Cut (Enter)" ) )
				{
					ApplyClip();
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				if( ImGui::Button( "Start over (Esc)" ) )
				{
					clipPoints.Clear();
				}
			}
		}
		else if( gizmoMode == GIZMO_VERTEX )
		{
			if( cornerSelection.Num() == 0 )
			{
				ImGui::TextDisabled( "Corners: click a dot to pick it, Ctrl+click for more, then drag the arrows." );
			}
			else
			{
				ImGui::Text( "Corners: %d picked. Drag the arrows; the brush reshapes around them.", cornerSelection.Num() );
			}
		}
	}
	ImGui::End();
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
		if( gizmoMode == GIZMO_CLIP && clipPoints.Num() > 0 )
		{
			clipPoints.Clear();
		}
		else if( gizmoMode == GIZMO_VERTEX && cornerSelection.Num() > 0 )
		{
			cornerSelection.Clear();
		}
		else
		{
			document.selection.Clear();
		}
	}
	else if( gizmoMode == GIZMO_CLIP && ImGui::IsKeyPressed( ImGuiKey_Enter, false ) )
	{
		ApplyClip();
	}
	else if( gizmoMode == GIZMO_CLIP && ImGui::IsKeyPressed( ImGuiKey_Tab, false ) )
	{
		clipKeep = ( clipKeep + 1 ) % 3;
	}
	else if( gizmoMode == GIZMO_CLIP && ImGui::IsKeyPressed( ImGuiKey_Backspace, false ) && clipPoints.Num() > 0 )
	{
		clipPoints.RemoveIndex( clipPoints.Num() - 1 );
	}
	else if( !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_C, false ) )
	{
		gizmoMode = GIZMO_CLIP;
		clipPoints.Clear();
	}
	else if( !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_V, false ) )
	{
		gizmoMode = GIZMO_VERTEX;
		cornerSelection.Clear();
	}
	else if( !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_W, false ) )
	{
		gizmoMode = GIZMO_MOVE;
	}
	else if( !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_E, false ) )
	{
		gizmoMode = GIZMO_ROTATE;
	}
	else if( !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_R, false ) )
	{
		gizmoMode = GIZMO_RESIZE;
	}
	else if( ImGui::IsKeyPressed( ImGuiKey_LeftBracket, false ) )
	{
		editor_gridSize.SetInteger( Max( 1, editor_gridSize.GetInteger() / 2 ) );
		Status( va( "Grid %d", editor_gridSize.GetInteger() ) );
	}
	else if( ImGui::IsKeyPressed( ImGuiKey_RightBracket, false ) )
	{
		editor_gridSize.SetInteger( Min( 256, editor_gridSize.GetInteger() * 2 ) );
		Status( va( "Grid %d", editor_gridSize.GetInteger() ) );
	}
}

static void HandleViewport()
{
	ImGuiIO& io = ImGui::GetIO();
	const bool hovered = !io.WantCaptureMouse;

	view.UpdateCamera( hovered );
	if( gizmoMode == GIZMO_CLIP )
	{
		EndGizmoDrag();
		DrawClipTool();
	}
	else if( gizmoMode == GIZMO_VERTEX )
	{
		EndGizmoDrag();
		DrawCornerTool();
	}
	else
	{
		EndCornerDrag( true );
		DrawGizmo();
	}
	DrawViewportToolbar();

	// click selects, Ctrl+click adds or removes, Shift+click picks faces
	if( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() )
	{
		idList<Selection> brushes;
		document.SelectedBrushes( brushes );
		if( gizmoMode == GIZMO_CLIP && brushes.Num() > 0 )
		{
			ClipToolClick( io.MousePos.x, io.MousePos.y );
		}
		else if( gizmoMode == GIZMO_VERTEX && CornerToolClick( io.MousePos.x, io.MousePos.y, io.KeyCtrl ) )
		{
			// picked a corner
		}
		else if( io.KeyShift )
		{
			const Selection face = view.Pick( document, io.MousePos.x, io.MousePos.y, true );
			if( face.IsFace() )
			{
				if( document.selection.IsFaces() )
				{
					document.selection.Toggle( face );
				}
				else
				{
					document.selection.Set( face );
				}
			}
		}
		else
		{
			const Selection hit = view.Pick( document, io.MousePos.x, io.MousePos.y );
			if( io.KeyCtrl )
			{
				document.selection.Toggle( hit );
			}
			else if( hit.IsEmpty() )
			{
				document.selection.Clear();
			}
			else
			{
				document.selection.Set( hit );
			}
		}
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
		if( ImGui::MenuItem( "Open Project..." ) )
		{
			openProjectPopup = true;
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
		const bool canEdit = document.CanTransformSelection();
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
		if( ImGui::MenuItem( "Select none", "Esc", false, !document.selection.IsEmpty() ) )
		{
			document.selection.Clear();
		}
		ImGui::Separator();
		if( ImGui::MenuItem( "Move tool", "W", gizmoMode == GIZMO_MOVE ) )
		{
			gizmoMode = GIZMO_MOVE;
		}
		if( ImGui::MenuItem( "Rotate tool", "E", gizmoMode == GIZMO_ROTATE ) )
		{
			gizmoMode = GIZMO_ROTATE;
		}
		if( ImGui::MenuItem( "Resize tool", "R", gizmoMode == GIZMO_RESIZE ) )
		{
			gizmoMode = GIZMO_RESIZE;
		}
		if( ImGui::MenuItem( "Clip tool", "C", gizmoMode == GIZMO_CLIP ) )
		{
			gizmoMode = GIZMO_CLIP;
			clipPoints.Clear();
		}
		if( ImGui::MenuItem( "Corner tool", "V", gizmoMode == GIZMO_VERTEX ) )
		{
			gizmoMode = GIZMO_VERTEX;
			cornerSelection.Clear();
		}
		{
			idList<Selection> brushes;
			document.SelectedBrushes( brushes );
			if( ImGui::MenuItem( "Carve selected brushes", NULL, false, brushes.Num() > 0 ) )
			{
				document.SubtractSelection();
			}
		}
		ImGui::MenuItem( "Texture lock", NULL, &document.textureLock );
		ImGui::Separator();
		bool snap = editor_snap.GetBool();
		if( ImGui::MenuItem( "Snap to grid", NULL, &snap ) )
		{
			editor_snap.SetBool( snap );
		}
		if( ImGui::BeginMenu( "Grid size" ) )
		{
			const int sizes[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256 };
			for( int i = 0; i < 9; i++ )
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

	dockspaceId = mainDockSpaceId;
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
	DrawOpenProjectDialog();
	DrawMapNameDialog( "Save Map As", openSaveAsPopup, false );
	DrawMapNameDialog( "New Map", openNewMapPopup, true );
}

} // namespace GrizzEditor

#endif // GRIZZ_EDITOR
