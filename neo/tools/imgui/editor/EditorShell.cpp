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
#include "EditorTheme.h"

#include "../../imgui/BFGimgui.h"
#include "../../libs/imgui/imgui_internal.h"	// DockBuilder API
#include "../../libs/imgui/ImGuizmo.h"
#include "renderer/RenderCommon.h"

idCVar editor( "editor", "1", CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT, "start in the GrizzTech4 editor (0 = run as the game)" );
idCVar editor_startMap( "editor_startMap", "testmaps/gt4_test_room", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT, "map the editor opens at startup" );
idCVar editor_gridSize( "editor_gridSize", "8", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_NOCHEAT, "editor grid size in units" );
idCVar editor_showWelcome( "editor_showWelcome", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT, "show the Getting started window when the editor opens" );
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
	GIZMO_BRUSH,		// draw new brushes by dragging
	GIZMO_PAINT,		// click surfaces to put the current material on them
	GIZMO_COUNT
};

// the tabs over the tool dock: each shows the tools for one kind of work
enum editMode_t
{
	MODE_BUILD,
	MODE_PAINT,
	MODE_LOGIC,
	MODE_LIGHT,
	MODE_KITS,
	MODE_COUNT
};
static editMode_t	editMode = MODE_BUILD;
static bool			paintPicking = false;		// the next paint click picks a material up instead
static gizmoMode_t	gizmoMode = GIZMO_MOVE;
static bool			gizmoDragging = false;
static unsigned int	dockspaceId = 0;
static idMat4		gizmoStart;			// the handle's matrix when the drag began
static idMat4		gizmoMatrix;		// and now

static ShapeSettings	shapeSettings;

// what new brushes are made of and look like
static int			drawShape = SHAPE_BOX;
static idStr		currentMaterial = "textures/dev/grid_wall";
static float		drawHeight = 128.0f;

// brush tool: drag a footprint, then set the height
enum drawStage_t
{
	DRAW_NONE,
	DRAW_FOOTPRINT,
	DRAW_HEIGHT
};
static drawStage_t	drawStage = DRAW_NONE;
static idVec3		drawStart;
static idVec3		drawEnd;
static float		drawBaseZ = 0.0f;
static float		drawAnchorY = 0.0f;
static float		drawAnchorHeight = 128.0f;
static float		drawUnitsPerPixel = 1.0f;
static idVec2		drawPressPos;

// a shape or thing being dragged out of the tool dock into the view
static int			draggingShape = -1;
static idStr		draggingThing;			// entity class, or "light:<preset>"

// top-down plan view
static bool			planView = false;
static idVec2		planCenter( 0, 0 );
static float		planZoom = 0.5f;		// pixels per unit
static bool			planPanning = false;
static bool			planMoving = false;
static idVec2		planMoveStart;
static idVec2		planLastMouse;

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
static void TurnSelection( float degrees );
static void FitSelectedFaces();

static void SwitchProject( const char* name );
static void CenterPlanOnCamera();
static void BeginCornerMove();
static void MoveCorners( const idVec3& delta );

// titles carry an icon; the part after ### is the window's ID, which stays the same
static const char* WINDOW_OUTLINER			= ICON_OUTLINER "  Outliner###Outliner";
static const char* WINDOW_INSPECTOR			= ICON_INSPECTOR "  Inspector###Inspector";
static const char* WINDOW_CONTENT_BROWSER	= ICON_CONTENT "  Content###Content Browser";
static const char* WINDOW_PROBLEMS			= ICON_PROBLEMS "  Problems###Problems";

// quiet, wrapped hint text
static void Hint( const char* text )
{
	UI::Hint( text );
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

	common->Printf( "Editor: playing %s (Esc or F10 stops)\n", mapName );
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
	TurnSelection( atof( args.Argv( 1 ) ) );
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

static void SetTool( gizmoMode_t mode );
static void SetMode( editMode_t mode );
static void DragOut( int shape, const char* classname );
static void PlaceThing( const char* classname );

// editorTool <brush|move|rotate|resize|clip|corners|paint> [top|3d]
static void EditorTool_f( const idCmdArgs& args )
{
	static const char* names[GIZMO_COUNT] = { "move", "rotate", "resize", "clip", "corners", "brush", "paint" };
	for( int i = 0; i < GIZMO_COUNT && args.Argc() > 1; i++ )
	{
		if( idStr::Icmp( args.Argv( 1 ), names[i] ) == 0 )
		{
			SetTool( ( gizmoMode_t )i );
		}
	}
	if( args.Argc() > 2 )
	{
		planView = idStr::Icmp( args.Argv( 2 ), "top" ) == 0;
		if( planView )
		{
			CenterPlanOnCamera();
		}
	}
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
	cmdSystem->AddCommand( "editorTool", EditorTool_f, CMD_FL_SYSTEM, "picks a tool and view: editorTool <brush|move|rotate|resize|clip|corners> [top|3d]" );
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
	ImGui::DockBuilderDockWindow( WINDOW_PROBLEMS, bottom );
	ImGui::DockBuilderDockWindow( WINDOW_CONTENT_BROWSER, bottom );
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

static const char* ThingIcon( const idDeclEntityDef* def );
static const char* ThingLabel( const idDeclEntityDef* def );

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

// a row's type, right-aligned and quiet
static void RowType( const char* text )
{
	const float width = ImGui::CalcTextSize( text ).x;
	ImGui::SameLine( Max( ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - width - 4.0f * UI::Scale() ) );
	ImGui::TextDisabled( "%s", text );
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
		Hint( "No level open. Use File > New Map, or double-click a map in Content." );
		ImGui::End();
		return;
	}

	UI::SearchField( "##filter", outlinerFilter, sizeof( outlinerFilter ), "Search the level" );
	ImGui::Spacing();

	ImGui::BeginChild( "outliner_list" );

	// the level's own brushes
	const idMapEntity* world = map->GetEntity( 0 );
	if( outlinerFilter[0] == '\0' )
	{
		const bool open = ImGui::TreeNodeEx( "Level geometry", ImGuiTreeNodeFlags_SpanAvailWidth, ICON_BRUSH "  Level geometry" );
		RowType( va( "%d", world->GetNumPrimitives() ) );
		if( open )
		{
			for( int p = 0; p < world->GetNumPrimitives(); p++ )
			{
				const bool isBrush = world->GetPrimitive( p )->GetType() == idMapPrimitive::TYPE_BRUSH;
				const bool selected = document.selection.Contains( Selection( 0, p ) );
				if( ImGui::Selectable( va( "%s  %s %d##prim%d", isBrush ? ICON_BRUSH : ICON_PATCH, isBrush ? "Brush" : "Patch", p, p ), selected, ImGuiSelectableFlags_AllowDoubleClick ) )
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
		const char* classname = ent->epairs.GetString( "classname", "?" );
		const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->FindType( DECL_ENTITYDEF, classname, false ) );
		const char* name = ent->epairs.GetString( "name", classname );
		const bool selected = document.selection.Contains( Selection( i ) );
		if( ImGui::Selectable( va( "%s  %s##ent%d", def != NULL ? ThingIcon( def ) : ICON_WARNING, name, i ), selected, ImGuiSelectableFlags_AllowDoubleClick ) )
		{
			ClickSelect( Selection( i ) );
			if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
			{
				FocusSelection();
			}
		}
		RowType( def != NULL ? ThingLabel( def ) : classname );
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

static const char* ThingIcon( const idDeclEntityDef* def );
static const char* ThingLabel( const idDeclEntityDef* def );
static idStr ShortMaterialName( const char* name );

// the door's settings in plain terms; the raw keys are under Advanced
static void DrawDoorCard( idMapEntity* ent, const idDeclEntityDef* def )
{
	if( UI::BeginCard( "door", ICON_DOOR, "Door", "The green arrow in the view shows where the door slides to. It opens when the player comes near, unless it's locked." ) )
	{
		idDict args;
		if( def != NULL )
		{
			args.Copy( def->dict );
		}
		args.Copy( ent->epairs );

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
		UI::FieldLabel( "Opens" );
		if( ImGui::Combo( "##opens", &dirIndex, directions, 6 ) )
		{
			document.BeginChange( "Door direction" );
			ent->epairs.SetFloat( "movedir", dirValues[dirIndex] );
			ent->epairs.Delete( "angle" );
			document.EndChange();
		}

		float speed = args.GetFloat( "speed", "160" );
		UI::FieldLabel( "Speed" );
		if( ImGui::SliderFloat( "##speed", &speed, 20.0f, 800.0f, "%.0f units/s" ) )
		{
			ent->epairs.SetFloat( "speed", idMath::Rint( speed ) );
			document.EndChange();
		}
		UndoBracket( "Door speed" );

		float wait = args.GetFloat( "wait", "3" );
		bool staysOpen = wait < 0.0f;
		if( !staysOpen )
		{
			UI::FieldLabel( "Closes after" );
			if( ImGui::SliderFloat( "##closes", &wait, 0.5f, 30.0f, "%.1f s" ) )
			{
				ent->epairs.SetFloat( "wait", idMath::Rint( wait * 10.0f ) / 10.0f );
				document.EndChange();
			}
			UndoBracket( "Door wait" );
		}
		if( ImGui::Checkbox( "Stays open", &staysOpen ) )
		{
			document.BeginChange( "Door wait" );
			ent->epairs.SetFloat( "wait", staysOpen ? -1.0f : 3.0f );
			document.EndChange();
		}
		ImGui::SameLine();
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
	UI::EndCard();
}

// colour, reach and shadows of a point light
static void DrawLightCard( idMapEntity* ent )
{
	if( UI::BeginCard( "light", ICON_LIGHT, "Light", "How the light looks. The reach is how far it lights in each direction." ) )
	{
		idVec3 color = ent->epairs.GetVector( "_color", "1 1 1" );
		UI::FieldLabel( "Colour" );
		if( ImGui::ColorEdit3( "##color", color.ToFloatPtr(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel ) )
		{
			ent->epairs.Set( "_color", color.ToString( 3 ) );
			document.EndChange();
		}
		UndoBracket( "Light colour" );

		if( ent->epairs.FindKey( "light_target" ) == NULL )
		{
			idVec3 radius = ent->epairs.GetVector( "light_radius", "300 300 300" );
			float reach = Max( radius.x, Max( radius.y, radius.z ) );
			const bool even = idMath::Fabs( radius.x - radius.y ) < 0.5f && idMath::Fabs( radius.y - radius.z ) < 0.5f;
			UI::FieldLabel( "Reach" );
			if( even )
			{
				if( ImGui::SliderFloat( "##reach", &reach, 16.0f, 2048.0f, "%.0f units", ImGuiSliderFlags_Logarithmic ) )
				{
					reach = idMath::Rint( reach );
					ent->epairs.Set( "light_radius", idVec3( reach, reach, reach ).ToString( 0 ) );
					document.EndChange();
				}
				UndoBracket( "Light reach" );
			}
			else
			{
				if( ImGui::DragFloat3( "##reach3", radius.ToFloatPtr(), 2.0f, 1.0f, 8192.0f, "%.0f" ) )
				{
					ent->epairs.Set( "light_radius", radius.ToString( 0 ) );
					document.EndChange();
				}
				UndoBracket( "Light reach" );
				UI::Tip( "Reach along x, y and z" );
			}
		}
		else
		{
			UI::Hint( "A projected light (it has a target); its shape is under Advanced." );
		}

		bool shadows = !ent->epairs.GetBool( "noshadows" );
		if( ImGui::Checkbox( "Casts shadows", &shadows ) )
		{
			document.BeginChange( "Light shadows" );
			if( shadows )
			{
				ent->epairs.Delete( "noshadows" );
			}
			else
			{
				ent->epairs.SetBool( "noshadows", true );
			}
			document.EndChange();
		}
	}
	UI::EndCard();
}

// every key and value, for when the friendly cards don't cover something
static void DrawAdvancedCard( idMapEntity* ent, const idDeclEntityDef* def )
{
	if( UI::BeginCard( "advanced", ICON_ADVANCED, "Advanced", "Every setting this thing has, as the raw keys and values the game reads.", false ) )
	{
		int deleteIndex = -1;
		const bool isWorld = document.selection.Primary().entity == 0;

		if( ImGui::BeginTable( "keys", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg ) )
		{
			ImGui::TableSetupColumn( "Key", ImGuiTableColumnFlags_WidthStretch, 0.4f );
			ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch, 0.6f );
			ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() );

			for( int i = 0; i < ent->epairs.GetNumKeyVals(); i++ )
			{
				const idKeyValue* kv = ent->epairs.GetKeyVal( i );
				const idStr key = kv->GetKey();
				const idStr value = kv->GetValue();

				ImGui::PushID( i );
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled( "%s", key.c_str() );
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
				ImGui::SetNextItemWidth( -FLT_MIN );

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
				if( key.Icmp( "classname" ) != 0 && UI::IconButton( "remove", ICON_CLOSE, va( "Remove %s", key.c_str() ) ) )
				{
					deleteIndex = i;
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
		UI::SectionLabel( "Add a setting" );
		const float buttonWidth = ImGui::GetFrameHeight();
		ImGui::SetNextItemWidth( ( ImGui::GetContentRegionAvail().x - buttonWidth ) * 0.4f );
		ImGui::InputTextWithHint( "##newkey", "key", newKeyName, sizeof( newKeyName ), ImGuiInputTextFlags_CharsNoBlank );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x );
		ImGui::InputTextWithHint( "##newvalue", "value", newKeyValue, sizeof( newKeyValue ) );
		ImGui::SameLine();
		if( UI::IconButton( "add", ICON_PLUS, "Add this setting", false, newKeyName[0] != '\0' ) )
		{
			document.BeginChange( va( "Add %s", newKeyName ) );
			ent->epairs.Set( newKeyName, newKeyValue );
			document.EndChange();
			newKeyName[0] = '\0';
			newKeyValue[0] = '\0';
		}

		// settings the definition documents but the entity doesn't set yet
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
					UI::SectionLabel( "Also available" );
					header = true;
				}
				ImGui::PushID( key.c_str() );
				if( UI::IconButton( "addvar", ICON_PLUS, va( "Add %s: %s", key.c_str(), kv->GetValue().c_str() ) ) )
				{
					document.BeginChange( va( "Add %s", key.c_str() ) );
					ent->epairs.Set( key, def->dict.GetString( key ) );
					document.EndChange();
				}
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted( key.c_str() );
				UI::Tip( kv->GetValue().c_str() );
				ImGui::PopID();
			}
		}
	}
	UI::EndCard();
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

// position and size of the selection, and which way a lone point entity faces
static void DrawPlacementCard()
{
	idBounds bounds;
	if( !document.CanTransformSelection() || !document.SelectionBounds( bounds ) )
	{
		return;
	}

	if( UI::BeginCard( "placement", ICON_MOVE, "Placement", "Where it is and how big. Position is the bottom centre, where things stand. Drag a number or click it to type." ) )
	{
		const float grid = editor_snap.GetBool() ? GridSize() : 1.0f;

		idVec3 position( ( bounds[0].x + bounds[1].x ) * 0.5f, ( bounds[0].y + bounds[1].y ) * 0.5f, bounds[0].z );
		const idVec3 oldPosition = position;
		UI::FieldLabel( "Position" );
		// these drags are bracketed by UndoBracket: one undo step per drag
		if( ImGui::DragFloat3( "##position", position.ToFloatPtr(), grid * 0.25f, 0, 0, "%.0f" ) )
		{
			document.TranslateSelection( position - oldPosition );
			document.EndChange();
		}
		UndoBracket( "Move" );

		const bool pointEntity = document.selection.Num() == 1 && document.selection.Primary().primitive < 0 &&
								 document.selection.Primary().entity != 0 && document.SelectedEntity()->GetNumPrimitives() == 0;
		if( !pointEntity )
		{
			idVec3 size = bounds[1] - bounds[0];
			UI::FieldLabel( "Size" );
			if( ImGui::DragFloat3( "##size", size.ToFloatPtr(), grid * 0.25f, 1.0f, 65536.0f, "%.0f" ) )
			{
				ResizeSelection( bounds, size );
				document.EndChange();
			}
			UndoBracket( "Resize" );
			UI::Tip( "Width, depth and height" );
		}
		else
		{
			// which way it faces, as a compass angle
			idMapEntity* ent = document.SelectedEntity();
			idMat3 axis;
			EntityAxis( ent, axis );
			float yaw = RAD2DEG( atan2( axis[0].y, axis[0].x ) );
			UI::FieldLabel( "Facing" );
			ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - ( ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x ) * 2.0f );
			if( ImGui::DragFloat( "##facing", &yaw, 1.0f, -360.0f, 360.0f, "%.0f deg" ) )
			{
				yaw = idMath::AngleNormalize360( idMath::Rint( yaw ) );
				ent->epairs.Delete( "rotation" );
				ent->epairs.SetFloat( "angle", yaw );
				document.EndChange();
			}
			UndoBracket( "Facing" );
			UI::Tip( "0 faces east (+X), 90 north (+Y). The arrow in the view shows it." );
			ImGui::SameLine();
			if( UI::IconButton( "turnl", ICON_TURN_LEFT, "Turn 90 degrees left (,)" ) )
			{
				TurnSelection( 90.0f );
			}
			ImGui::SameLine();
			if( UI::IconButton( "turnr", ICON_TURN_RIGHT, "Turn 90 degrees right (.)" ) )
			{
				TurnSelection( -90.0f );
			}
		}
	}
	UI::EndCard();
}

static void DrawMaterialField( const char* current, int key )
{
	if( brushMaterialFor != key )
	{
		idStr::Copynz( brushMaterialBuffer, current, sizeof( brushMaterialBuffer ) );
		brushMaterialFor = key;
	}

	ImGui::PushFont( UI::Font( UI::FONT_BOLD ) );
	ImGui::TextUnformatted( ShortMaterialName( current ).c_str() );
	ImGui::PopFont();
	ImGui::TextDisabled( "%s", current );

	const float buttonWidth = ImGui::GetFrameHeight();
	ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - ( buttonWidth + ImGui::GetStyle().ItemSpacing.x ) * 2.0f );
	const bool entered = ImGui::InputTextWithHint( "##material", "material name", brushMaterialBuffer, sizeof( brushMaterialBuffer ), ImGuiInputTextFlags_EnterReturnsTrue );
	ImGui::SameLine();
	if( UI::IconButton( "apply", ICON_OK, "Use the material typed here (Enter)" ) || entered )
	{
		document.SetMaterial( brushMaterialBuffer );
	}
	ImGui::SameLine();
	if( UI::IconButton( "usecurrent", ICON_PAINT, va( "Use the paint material (%s)", currentMaterial.c_str() ) ) )
	{
		document.SetMaterial( currentMaterial );
		idStr::Copynz( brushMaterialBuffer, currentMaterial.c_str(), sizeof( brushMaterialBuffer ) );
	}
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
	const Selection primary = document.selection.Primary();

	if( UI::BeginCard( "facematerial", ICON_PAINT, "Material", "What the selected faces are made of. The Paint tab (2) paints faces by clicking them." ) )
	{
		DrawMaterialField( face->GetMaterial(), ( primary.entity * 1000 + primary.primitive ) * 100 + primary.side + 50000000 );
	}
	UI::EndCard();

	if( UI::BeginCard( "texture", ICON_FIT, "Texture", "How the material's texture lies on the face: shift it, change how big it repeats, or turn it." ) )
	{
		const TextureAlign align = GetTextureAlign( face );
		const float grid = GridSize();

		idVec2 offset = align.offset;
		UI::FieldLabel( "Offset" );
		if( ImGui::DragFloat2( "##offset", offset.ToFloatPtr(), 1.0f, 0, 0, "%.1f" ) )
		{
			const idVec2 delta = offset - align.offset;
			EditFaces( "Texture offset", [&]( TextureAlign & a )
			{
				a.offset += delta;
			}, true );
		}
		UndoBracket( "Texture offset" );

		idVec2 tile = align.tile;
		UI::FieldLabel( "Tile size" );
		if( ImGui::DragFloat2( "##tile", tile.ToFloatPtr(), 1.0f, -4096.0f, 4096.0f, "%.1f" ) )
		{
			EditFaces( "Texture size", [&]( TextureAlign & a )
			{
				a.tile = tile;
			}, true );
		}
		UndoBracket( "Texture size" );
		UI::Tip( "Map units covered by one copy of the texture. Negative flips it." );

		float rotation = align.rotation;
		UI::FieldLabel( "Rotation" );
		if( ImGui::DragFloat( "##rotation", &rotation, 1.0f, -360.0f, 360.0f, "%.1f deg" ) )
		{
			EditFaces( "Texture rotation", [&]( TextureAlign & a )
			{
				a.rotation = rotation;
			}, true );
		}
		UndoBracket( "Texture rotation" );

		// nudges by the grid, for lining up without dragging
		UI::FieldLabel( "Nudge" );
		ImGui::Dummy( ImVec2( 0, 0 ) );
		ImGui::SameLine( 0.0f, 0.0f );
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
		if( UI::IconButton( "turn90", ICON_TURN_RIGHT, "Turn the texture 90 degrees" ) )
		{
			EditFaces( "Texture rotation", [&]( TextureAlign & a )
			{
				a.rotation = idMath::AngleNormalize180( a.rotation + 90.0f );
			} );
		}

		ImGui::Spacing();
		if( ImGui::Button( "Flip across" ) )
		{
			EditFaces( "Flip texture", [&]( TextureAlign & a )
			{
				a.tile.x = -a.tile.x;
			} );
		}
		ImGui::SameLine();
		if( ImGui::Button( "Flip up-down" ) )
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
		UI::Tip( "Straight, unshifted, one copy per 128 units" );

		if( ImGui::Button( ICON_FIT "  Fit" ) )
		{
			FitSelectedFaces();
		}
		UI::Tip( "Stretch the texture to cover the face exactly this many times across and up" );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x );
		ImGui::InputInt2( "##times", faceRepeats );
		faceRepeats[0] = idMath::ClampInt( 1, 64, faceRepeats[0] );
		faceRepeats[1] = idMath::ClampInt( 1, 64, faceRepeats[1] );

		ImGui::Checkbox( "Texture lock", &document.textureLock );
		UI::Tip( "Keep textures in place on surfaces when you move, turn or resize brushes." );
	}
	UI::EndCard();
}

// carve, cut, corners, and turning level brushes into doors and other brush things
static void DrawShapeCard()
{
	idList<Selection> brushes;
	document.SelectedBrushes( brushes );
	if( brushes.Num() == 0 )
	{
		return;
	}

	if( UI::BeginCard( "shape", ICON_CORNERS, "Shape", "Ways to reshape brushes. Carve cuts the selection out of whatever it overlaps: put a box through a wall and carve it for a doorway." ) )
	{
		if( ImGui::Button( ICON_CARVE "  Carve" ) )
		{
			const int pieces = document.SubtractSelection();
			Status( pieces > 0 ? va( "Carved out; %d pieces left around it.", pieces ) : "Nothing to carve: the selection doesn't overlap any other brush." );
			UI::EndCard();
			return;
		}
		UI::Tip( "Cut the selected brushes out of every brush they overlap, then remove them. For doorways, windows and alcoves." );
		ImGui::SameLine();
		if( ImGui::Button( ICON_CLIP "  Cut" ) )
		{
			SetTool( GIZMO_CLIP );
		}
		UI::Tip( "Cut the selected brushes in two along a line you click (C)" );
		ImGui::SameLine();
		if( ImGui::Button( ICON_CORNERS "  Corners" ) )
		{
			SetTool( GIZMO_VERTEX );
		}
		UI::Tip( "Drag the corners of the selected brushes (V)" );

		// only the level's own brushes can be turned into something
		bool allLevel = true;
		for( int i = 0; i < document.selection.Num(); i++ )
		{
			allLevel &= ( document.selection[i].entity == 0 );
		}
		if( allLevel )
		{
			ImGui::Spacing();
			UI::SectionLabel( "Turn into" );
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
				if( ImGui::Button( va( "%s  %s", ThingIcon( def ), ThingLabel( def ) ) ) )
				{
					if( document.GroupSelection( def->GetName() ) >= 0 )
					{
						Status( va( "Made a %s.", ThingLabel( def ) ) );
					}
				}
				UI::Tip( def->dict.GetString( "editor_usage" ) );
			}
		}
	}
	UI::EndCard();
}

static void DrawPrimitiveInspector( idMapPrimitive* prim )
{
	const bool isBrush = prim->GetType() == idMapPrimitive::TYPE_BRUSH;
	DrawPlacementCard();
	DrawShapeCard();

	if( UI::BeginCard( "look", ICON_PAINT, "Material", "What the brush is made of. Shift+click a single face in the view to change just that face; the Paint tab (2) paints by clicking." ) )
	{
		const Selection primary = document.selection.Primary();
		const char* current = isBrush ? static_cast<idMapBrush*>( prim )->GetSide( 0 )->GetMaterial() : static_cast<idMapPatch*>( prim )->GetMaterial();
		DrawMaterialField( current, primary.entity * 100000 + primary.primitive );

		if( isBrush )
		{
			idMapBrush* brush = static_cast<idMapBrush*>( prim );
			if( ImGui::TreeNode( "Faces", "Faces (%d)", brush->GetNumSides() ) )
			{
				for( int s = 0; s < brush->GetNumSides(); s++ )
				{
					if( ImGui::Selectable( va( "%d   %s##face%d", s, ShortMaterialName( brush->GetSide( s )->GetMaterial() ).c_str(), s ) ) )
					{
						document.selection.Set( Selection( primary.entity, primary.primitive, s ) );
					}
					UI::Tip( brush->GetSide( s )->GetMaterial() );
				}
				ImGui::TreePop();
			}
		}
	}
	UI::EndCard();
}

static void DrawEntityInspector( idMapEntity* ent )
{
	const char* classname = ent->epairs.GetString( "classname" );
	const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->FindType( DECL_ENTITYDEF, classname, false ) );
	const bool isWorld = document.selection.Primary().entity == 0;

	if( def == NULL && !isWorld )
	{
		ImGui::TextColored( UI::Warn(), ICON_WARNING "  No type called \"%s\" exists, so this won't appear in the game.", classname );
	}

	if( !isWorld )
	{
		DrawPlacementCard();
	}
	if( idStr::Icmp( classname, "func_door" ) == 0 )
	{
		DrawDoorCard( ent, def );
	}
	if( idStr::Icmp( classname, "light" ) == 0 )
	{
		DrawLightCard( ent );
	}
	if( ent->GetNumPrimitives() > 0 && !isWorld )
	{
		if( UI::BeginCard( "brushes", ICON_BRUSH, "Brushes", "This thing is made of brushes. Giving them back turns them into plain level geometry again." ) )
		{
			ImGui::AlignTextToFramePadding();
			ImGui::Text( "%d brushes", ent->GetNumPrimitives() );
			ImGui::SameLine();
			if( ImGui::Button( ICON_UNGROUP "  Back to plain brushes" ) )
			{
				document.UngroupSelection();
				UI::EndCard();
				return;
			}
		}
		UI::EndCard();
	}
	DrawAdvancedCard( ent, def );
}

// the selection's name and what it is, above the cards
static void DrawInspectorHeader( const char* icon, const char* title, const char* subtitle, const char* usage )
{
	ImGui::PushFont( UI::Font( UI::FONT_TITLE ) );
	ImGui::TextColored( UI::Accent(), "%s", icon );
	ImGui::SameLine();
	ImGui::TextUnformatted( title );
	ImGui::PopFont();
	if( subtitle != NULL && subtitle[0] != '\0' )
	{
		ImGui::TextDisabled( "%s", subtitle );
	}
	if( usage != NULL && usage[0] != '\0' )
	{
		UI::Hint( usage );
	}
	ImGui::Spacing();
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
		// an empty state that says what to do
		const float width = ImGui::GetContentRegionAvail().x;
		ImGui::Dummy( ImVec2( 0.0f, 24.0f * UI::Scale() ) );
		ImFont* iconFont = UI::Font( UI::FONT_ICON );
		const float iconWidth = iconFont->CalcTextSizeA( iconFont->FontSize, FLT_MAX, 0.0f, ICON_SELECT ).x;
		ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( width - iconWidth ) * 0.5f );
		ImGui::PushFont( iconFont );
		ImGui::TextDisabled( ICON_SELECT );
		ImGui::PopFont();
		ImGui::PushFont( UI::Font( UI::FONT_BOLD ) );
		const float textWidth = ImGui::CalcTextSize( "Nothing selected" ).x;
		ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( width - textWidth ) * 0.5f );
		ImGui::TextUnformatted( "Nothing selected" );
		ImGui::PopFont();
		ImGui::Spacing();
		UI::Hint( "Click something in the view or the Outliner to see and change it. Ctrl+click adds to the selection; Shift+click picks a single face." );
		ImGui::Spacing();
		if( document.IsOpen() && ImGui::Button( ICON_SETTINGS "  Level settings", ImVec2( -FLT_MIN, 0 ) ) )
		{
			document.selection.Set( Selection( 0 ) );
		}
		ImGui::End();
		return;
	}

	const int count = document.selection.Num();
	if( document.selection.IsFaces() )
	{
		DrawInspectorHeader( ICON_PAINT, count > 1 ? va( "%d faces", count ) : "Face", "Shift+click more faces to add them", NULL );
		DrawFaceInspector();
	}
	else if( document.selection.Primary().IsPrimitive() )
	{
		const bool isBrush = document.SelectedPrimitive()->GetType() == idMapPrimitive::TYPE_BRUSH;
		DrawInspectorHeader( isBrush ? ICON_BRUSH : ICON_PATCH, count > 1 ? va( "%d selected", count ) : ( isBrush ? "Brush" : "Patch" ),
							 document.selection.Primary().entity == 0 ? "Part of the level" : "Part of a brush thing", NULL );
		DrawPrimitiveInspector( document.SelectedPrimitive() );
	}
	else
	{
		idMapEntity* ent = document.SelectedEntity();
		const char* classname = ent->epairs.GetString( "classname" );
		const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->FindType( DECL_ENTITYDEF, classname, false ) );
		if( document.selection.Primary().entity == 0 )
		{
			DrawInspectorHeader( ICON_SETTINGS, "Level settings", "worldspawn", "Settings for the whole level. The level's own brushes are listed under Level geometry in the Outliner." );
		}
		else
		{
			const char* name = ent->epairs.GetString( "name", classname );
			const char* typeLabel = def != NULL ? ThingLabel( def ) : classname;
			idStr subtitle = typeLabel;
			if( count > 1 )
			{
				subtitle = va( "%s  -  %d selected, showing the last one picked", typeLabel, count );
			}
			DrawInspectorHeader( def != NULL ? ThingIcon( def ) : ICON_ENTITY, name, subtitle.c_str(), def != NULL ? def->dict.GetString( "editor_usage" ) : NULL );
		}
		DrawEntityInspector( ent );
	}

	ImGui::End();
}

// the point where a screen ray crosses the horizontal plane at height z
static bool RayToHeight( float x, float y, float z, idVec3& point )
{
	idVec3 start, dir;
	view.ScreenRay( x, y, start, dir );
	if( idMath::Fabs( dir.z ) < 1e-4f )
	{
		return false;
	}
	const float t = ( z - start.z ) / dir.z;
	if( t <= 0.0f || t > 65536.0f )
	{
		return false;
	}
	point = start + dir * t;
	return true;
}

// where something dropped at a screen position goes: on the surface under it, else on the
// floor grid, else a little in front of the camera
static idVec3 PlacementPointAt( float x, float y, idVec3* surfaceNormal = NULL )
{
	idVec3 point, normal;
	if( view.Trace( document, x, y, point, normal ) && ( point - view.camera.origin ).LengthSqr() < Square( 8192.0f ) )
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
	if( RayToHeight( x, y, view.gridZ, point ) && ( point - view.camera.origin ).LengthSqr() < Square( 8192.0f ) )
	{
		return SnapToGrid( point );
	}
	idVec3 start, dir;
	view.ScreenRay( x, y, start, dir );
	return SnapToGrid( start + dir * 256.0f );
}

static idVec3 PlacementPoint( idVec3* surfaceNormal = NULL )
{
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	return PlacementPointAt( size.x * 0.5f, size.y * 0.5f, surfaceNormal );
}

static bool MakeAndAddShape( shapeType_t type, const idBounds& box )
{
	idList<idMapBrush*> brushes;
	if( !MakeShape( type, shapeSettings, box, currentMaterial, brushes ) )
	{
		Status( va( "Couldn't make a %s that size with these settings.", ShapeName( type ) ) );
		return false;
	}
	document.AddBrushes( brushes, va( "Add %s", ShapeName( type ) ) );
	return true;
}

// a shape at its usual size, sitting on floors, standing out from walls, hanging from ceilings
static void PlaceShapeAt( shapeType_t type, float x, float y )
{
	if( !document.IsOpen() )
	{
		return;
	}

	idVec3 normal;
	const idVec3 point = PlacementPointAt( x, y, &normal );
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

	if( MakeAndAddShape( type, idBounds( center - size * 0.5f, center + size * 0.5f ) ) )
	{
		Status( va( "Added a %s.", ShapeName( type ) ) );
	}
}

static void PlaceShape( shapeType_t type )
{
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	PlaceShapeAt( type, size.x * 0.5f, size.y * 0.5f );
}

// turns the selection about its centre, around the vertical
static void TurnSelection( float degrees )
{
	idBounds bounds;
	if( !document.CanTransformSelection() || !document.SelectionBounds( bounds ) )
	{
		return;
	}
	idVec3 pivot = bounds.GetCenter();
	if( document.selection.Num() == 1 && document.selection.Primary().primitive < 0 && document.SelectedEntity()->GetNumPrimitives() == 0 )
	{
		pivot = document.SelectedEntity()->epairs.GetVector( "origin" );
	}
	const float angle = DEG2RAD( degrees );
	const idMat3 turn( cos( angle ), -sin( angle ), 0, sin( angle ), cos( angle ), 0, 0, 0, 1 );
	const idVec3 turned( turn[0] * pivot, turn[1] * pivot, turn[2] * pivot );
	document.BeginChange( "Turn" );
	document.TransformSelection( turn, pivot - turned );
	document.EndChange();
}

// lays tiles out in rows that fit the panel
static void TileFlow( int index, float tile )
{
	if( index == 0 )
	{
		return;
	}
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float lineEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
	if( lineEnd + spacing + tile <= ImGui::GetWindowContentRegionMax().x )
	{
		ImGui::SameLine();
	}
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
		if( ImGui::BeginTabItem( ICON_MAP_FILE "  Levels" ) )
		{
			if( mapListDirty )
			{
				RefreshMapList();
			}
			if( ImGui::Button( ICON_NEW "  New level" ) )
			{
				openNewMapPopup = true;
			}
			ImGui::SameLine();
			if( UI::IconButton( "refresh", ICON_UNDO, "Look for new levels" ) )
			{
				RefreshMapList();
			}
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			Hint( "Double-click a level to open it." );

			ImGui::BeginChild( "maps" );
			for( int i = 0; i < mapList.Num(); i++ )
			{
				const bool isOpen = document.IsOpen() && mapList[i].Icmp( document.Name() ) == 0;
				if( ImGui::Selectable( va( ICON_MAP_FILE "  %s##map%d", mapList[i].c_str(), i ), selectedMap == i || isOpen, ImGuiSelectableFlags_AllowDoubleClick ) )
				{
					selectedMap = i;
					if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) && !isOpen )
					{
						OpenMap( mapList[i].c_str() );
					}
				}
				if( isOpen )
				{
					RowType( "open" );
				}
			}
			if( mapList.Num() == 0 )
			{
				Hint( "No levels in this project yet." );
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if( ImGui::BeginTabItem( ICON_MATERIAL "  Materials" ) )
		{
			UI::SearchField( "##matfilter", materialFilter, sizeof( materialFilter ), "Find a material", 260.0f * UI::Scale() );
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled( ICON_PAINT "  %s", ShortMaterialName( currentMaterial ).c_str() );
			UI::Tip( va( "Painting with %s. Clicking a material here makes it the paint material, and puts it on anything selected.", currentMaterial.c_str() ) );

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
				if( ImGui::Selectable( va( ICON_MATERIAL "  %s", decl->GetName() ), currentMaterial.Icmp( decl->GetName() ) == 0 ) )
				{
					// new brushes use it from now on; selected ones get it straight away
					currentMaterial = decl->GetName();
					if( !document.selection.IsEmpty() )
					{
						document.SetMaterial( decl->GetName() );
						idStr::Copynz( brushMaterialBuffer, decl->GetName(), sizeof( brushMaterialBuffer ) );
					}
					else
					{
						Status( va( "Painting with %s.", ShortMaterialName( decl->GetName() ).c_str() ) );
					}
				}
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if( ImGui::BeginTabItem( ICON_MODE_KITS "  Place" ) )
		{
			const bool canPlace = document.IsOpen();
			Hint( "Click to add one in the middle of the view, or drag it into the view." );
			ImGui::Spacing();

			const float tile = UI::TileSize();
			UI::SectionLabel( "Shapes" );
			static const char* shapeIcons[SHAPE_COUNT] = { ICON_SHAPE_BOX, ICON_SHAPE_ROOM, ICON_SHAPE_RAMP, ICON_SHAPE_CYLINDER, ICON_SHAPE_CONE, ICON_SHAPE_STAIRS, ICON_SHAPE_ARCH };
			for( int i = 0; i < SHAPE_COUNT; i++ )
			{
				TileFlow( i, tile );
				if( UI::ToolTile( ShapeName( ( shapeType_t )i ), shapeIcons[i], ShapeName( ( shapeType_t )i ), false, draggingShape < 0 ? ShapeHelp( ( shapeType_t )i ) : NULL, canPlace, tile ) )
				{
					PlaceShape( ( shapeType_t )i );
				}
				DragOut( i, NULL );
			}

			if( ImGui::TreeNode( "Shape settings" ) )
			{
				UI::FieldLabel( "Sides", 120.0f * UI::Scale() );
				ImGui::SliderInt( "##sides", &shapeSettings.sides, 3, 32 );
				UI::Tip( "Cylinders and cones" );
				UI::FieldLabel( "Steps", 120.0f * UI::Scale() );
				ImGui::SliderInt( "##steps", &shapeSettings.steps, 2, 32 );
				UI::Tip( "Stairs" );
				UI::FieldLabel( "Segments", 120.0f * UI::Scale() );
				ImGui::SliderInt( "##segments", &shapeSettings.segments, 2, 24 );
				UI::Tip( "Arches" );
				UI::FieldLabel( "Thickness", 120.0f * UI::Scale() );
				ImGui::DragFloat( "##thickness", &shapeSettings.thickness, 1.0f, 1.0f, 256.0f, "%.0f" );
				UI::Tip( "Arches and rooms" );
				ImGui::TreePop();
			}

			ImGui::Spacing();
			UI::SectionLabel( "Things" );
			UI::SearchField( "##entfilter", entityFilter, sizeof( entityFilter ), "Find a type" );
			ImGui::BeginChild( "entities" );
			const int count = declManager->GetNumDecls( DECL_ENTITYDEF );
			int shown = 0;
			for( int i = 0; i < count; i++ )
			{
				const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->DeclByIndex( DECL_ENTITYDEF, i, true ) );
				if( def == NULL )
				{
					continue;
				}
				// only types meant for levels: they document themselves for the editor
				const char* usage = def->dict.GetString( "editor_usage" );
				if( usage[0] == '\0' || def->dict.GetBool( "editor_brushes" ) || idStr::Icmp( def->GetName(), "worldspawn" ) == 0 || idStr::Icmp( def->GetName(), "player" ) == 0 )
				{
					continue;
				}
				if( entityFilter[0] != '\0' && idStr::FindText( def->GetName(), entityFilter, false ) < 0 && idStr::FindText( ThingLabel( def ), entityFilter, false ) < 0 )
				{
					continue;
				}
				TileFlow( shown++, tile );
				if( UI::ToolTile( def->GetName(), ThingIcon( def ), ThingLabel( def ), false, va( "%s\n%s", def->GetName(), usage ), canPlace, tile ) )
				{
					PlaceThing( def->GetName() );
				}
				DragOut( -1, def->GetName() );
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
}

static void ProblemRow( const char* text )
{
	ImGui::TextColored( UI::Warn(), ICON_WARNING );
	ImGui::SameLine();
	ImGui::TextWrapped( "%s", text );
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
				if( ImGui::SmallButton( ICON_OK " Fix" ) )
				{
					document.BeginChange( "Fix model name" );
					map->GetEntity( i )->epairs.Set( "model", ent->epairs.GetString( "name" ) );
					document.EndChange();
				}
				ImGui::SameLine();
				ProblemRow( va( "\"%s\" is made of brushes, but its 'model' doesn't match its name, so it won't appear in the game.", EntityLabel( ent, i ).c_str() ) );
				ImGui::PopID();
			}
			if( declManager->FindType( DECL_ENTITYDEF, classname, false ) == NULL )
			{
				any = true;
				ImGui::PushID( i );
				if( ImGui::SmallButton( ICON_FOCUS " Show" ) )
				{
					document.selection.Set( Selection( i ) );
					FocusSelection();
				}
				ImGui::SameLine();
				ProblemRow( va( "\"%s\" uses the type \"%s\", which doesn't exist. It won't appear in the game.", EntityLabel( ent, i ).c_str(), classname ) );
				ImGui::PopID();
			}
		}
		if( !hasStart )
		{
			any = true;
			ImGui::PushID( "start" );
			if( ImGui::SmallButton( ICON_PLUS " Add" ) )
			{
				PlaceThing( "info_player_start" );
			}
			ImGui::SameLine();
			ProblemRow( "The level has no player start, so Play has nowhere to put you." );
			ImGui::PopID();
		}
	}

	if( !any )
	{
		ImGui::TextColored( UI::Good(), ICON_OK );
		ImGui::SameLine();
		ImGui::TextUnformatted( "No problems found." );
		Hint( "Leaks and compile errors are reported in the console (the ~ key) when you press Play." );
	}

	ImGui::End();
}

static bool showWelcome = false;
static bool welcomeChecked = false;

// a short first lesson, shown the first time and from Help > Getting started
static void DrawWelcome()
{
	if( !welcomeChecked )
	{
		welcomeChecked = true;
		showWelcome = editor_showWelcome.GetBool();
	}
	if( !showWelcome )
	{
		return;
	}

	ImGui::SetNextWindowSize( ImVec2( 560.0f * UI::Scale(), 0 ), ImGuiCond_Appearing );
	ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 20.0f * UI::Scale(), 18.0f * UI::Scale() ) );
	const bool open = ImGui::Begin( "Getting started", &showWelcome, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar );
	ImGui::PopStyleVar();
	if( open )
	{
		ImGui::PushFont( UI::Font( UI::FONT_TITLE ) );
		ImGui::TextColored( UI::Accent(), ICON_SPARKLES );
		ImGui::SameLine();
		ImGui::TextUnformatted( "Welcome to GrizzTech4" );
		ImGui::PopFont();
		Hint( "A level in five steps. Everything here is also under Help." );
		ImGui::Spacing();

		struct step_t
		{
			const char* icon;
			const char* title;
			const char* text;
		};
		static const step_t steps[] =
		{
			{ ICON_EYE,			"Look around",		"Hold the right mouse button and move the mouse to look; while holding it, WASD flies and Q/E go down and up. Middle button pans, Alt + left button orbits, the wheel moves forward and back." },
			{ ICON_MODE_BUILD,	"Build rooms",		"The dock at the bottom has the tools. In Build, pick Draw (B) and drag a footprint on the floor, let go, move the mouse for height, click. Or drag a shape from the dock into the view. T flips to a top-down plan." },
			{ ICON_ROTATE,		"Shape them",		"Click to select. Move (W), Turn (E) and Resize (R) put handles on it; the bar above the dock turns by 90, carves doorways and more. Hold Space for a wheel of every tool." },
			{ ICON_PAINT,		"Paint and light",	"The Paint tab (2) paints surfaces by clicking them. The Light tab (4) adds lights; the Logic tab (3) makes doors and the player start." },
			{ ICON_PLAY,		"Play",				"Press Play at the top. Esc brings you back." },
		};
		for( int i = 0; i < ( int )( sizeof( steps ) / sizeof( steps[0] ) ); i++ )
		{
			if( UI::BeginCard( va( "step%d", i ), steps[i].icon, va( "%d.  %s", i + 1, steps[i].title ) ) )
			{
				ImGui::TextUnformatted( steps[i].text );
			}
			UI::EndCard();
		}

		ImGui::Spacing();
		bool again = editor_showWelcome.GetBool();
		if( ImGui::Checkbox( "Show this when the editor opens", &again ) )
		{
			editor_showWelcome.SetBool( again );
		}
		const float buttonWidth = 120.0f * UI::Scale();
		ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - buttonWidth );
		if( UI::ColorButton( "Let's go", UI::Accent(), ImVec2( buttonWidth, 0 ) ) )
		{
			showWelcome = false;
		}
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
		Hint( "Hold the right mouse button to look around; WASD moves, Q and E go down and up, Shift is faster, and the wheel changes speed. Hold the middle button to pan, Alt + left button to orbit, and roll the wheel to move forward and back. T switches to the top view." );
		ImGui::Text( "Panels" );
		Hint( "Drag a panel's tab to move it: onto another panel to stack them, to a side to split, or out of the window onto another monitor. View > Reset layout puts everything back." );
		ImGui::Text( "Editing" );
		Hint( "Click to select, Ctrl+click to add or remove, Shift+click to pick faces. W, E and R move, turn and resize; B draws brushes, C cuts them, V drags their corners, P paints and I picks a material up. Hold Space for the tool wheel. 1 to 5 switch the dock's tabs (Build, Paint, Logic, Light, Kits). [ and ] change the grid. Carve cuts doorways and windows out of walls. Ctrl+Z undo, Ctrl+Y redo, Ctrl+D duplicate, Delete removes, F frames the selection, Ctrl+S saves." );
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

/*
===============================================================================

	Brush tool: press on a floor (or the grid) and drag out a footprint;
	release, move the mouse up or down to set the height, and click to finish.
	A click without a drag selects, as with the other tools.

===============================================================================
*/

static idBounds DrawBounds()
{
	idBounds box;
	box.Clear();
	box.AddPoint( idVec3( drawStart.x, drawStart.y, drawBaseZ ) );
	box.AddPoint( idVec3( drawEnd.x, drawEnd.y, drawBaseZ + drawHeight ) );
	return box;
}

static void DrawScreenBox( const idBounds& box, ImU32 color, float thickness )
{
	idVec3 corners[8];
	for( int i = 0; i < 8; i++ )
	{
		corners[i].Set( box[( i >> 0 ) & 1].x, box[( i >> 1 ) & 1].y, box[( i >> 2 ) & 1].z );
	}
	static const int edges[12][2] = { {0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7} };
	for( int i = 0; i < 12; i++ )
	{
		DrawScreenLine( corners[edges[i][0]], corners[edges[i][1]], color, thickness );
	}
}

static void CancelBrushDraw()
{
	drawStage = DRAW_NONE;
}

static void FinishBrushDraw()
{
	const idBounds box = DrawBounds();
	drawStage = DRAW_NONE;
	if( MakeAndAddShape( ( shapeType_t )drawShape, box ) )
	{
		const idVec3 size = box[1] - box[0];
		Status( va( "Added a %s, %.0f x %.0f x %.0f.", ShapeName( ( shapeType_t )drawShape ), size.x, size.y, size.z ) );
	}
}

// selects what's under the pointer the usual way: Ctrl adds, Shift picks faces
static void ClickSelectInView( float x, float y )
{
	ImGuiIO& io = ImGui::GetIO();
	if( io.KeyShift )
	{
		const Selection face = view.Pick( document, x, y, true );
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
		return;
	}

	const Selection hit = view.Pick( document, x, y );
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

// true while the tool is using the mouse, so nothing else should act on it
static bool HandleBrushTool( bool hovered )
{
	ImGuiIO& io = ImGui::GetIO();
	const float grid = GridSize();

	switch( drawStage )
	{
		case DRAW_NONE:
		{
			if( !hovered || !ImGui::IsMouseClicked( ImGuiMouseButton_Left ) || io.KeyAlt || !document.IsOpen() )
			{
				return false;
			}

			// start on the surface under the pointer if it's a floor, else on the grid
			idVec3 point, normal;
			drawBaseZ = view.gridZ;
			if( view.Trace( document, io.MousePos.x, io.MousePos.y, point, normal ) && normal.z > 0.7f )
			{
				drawBaseZ = SnapToGrid( point ).z;
			}
			if( !RayToHeight( io.MousePos.x, io.MousePos.y, drawBaseZ, point ) )
			{
				return false;
			}
			drawStart = SnapToGrid( point );
			drawEnd = drawStart;
			drawPressPos.Set( io.MousePos.x, io.MousePos.y );
			drawStage = DRAW_FOOTPRINT;
			return true;
		}

		case DRAW_FOOTPRINT:
		{
			idVec3 point;
			if( RayToHeight( io.MousePos.x, io.MousePos.y, drawBaseZ, point ) )
			{
				drawEnd = SnapToGrid( point );
			}

			if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
			{
				const bool dragged = ( idVec2( io.MousePos.x, io.MousePos.y ) - drawPressPos ).Length() > 4.0f;
				if( !dragged || idMath::Fabs( drawEnd.x - drawStart.x ) < grid * 0.5f || idMath::Fabs( drawEnd.y - drawStart.y ) < grid * 0.5f )
				{
					// a click, or too thin to be a brush: select instead
					drawStage = DRAW_NONE;
					if( !dragged )
					{
						ClickSelectInView( drawPressPos.x, drawPressPos.y );
					}
					return true;
				}

				// now the height: about one grid step per few pixels at this distance
				drawStage = DRAW_HEIGHT;
				drawAnchorY = io.MousePos.y;
				drawAnchorHeight = drawHeight;
				renderView_t rv;
				view.BuildRenderView( rv );
				const float distance = Max( 64.0f, ( DrawBounds().GetCenter() - view.camera.origin ).Length() );
				drawUnitsPerPixel = 2.0f * distance * tanf( DEG2RAD( rv.fov_y * 0.5f ) ) / Max( 1.0f, ImGui::GetIO().DisplaySize.y );
			}
			return true;
		}

		case DRAW_HEIGHT:
		{
			float height = drawAnchorHeight + ( drawAnchorY - io.MousePos.y ) * drawUnitsPerPixel;
			if( editor_snap.GetBool() )
			{
				height = idMath::Rint( height / grid ) * grid;
			}
			drawHeight = Max( grid, height );

			if( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) || ImGui::IsKeyPressed( ImGuiKey_Enter, false ) )
			{
				FinishBrushDraw();
			}
			return true;
		}
	}
	return false;
}

static void DrawBrushToolPreview()
{
	if( drawStage == DRAW_NONE )
	{
		return;
	}
	const idBounds box = DrawBounds();
	DrawScreenBox( box, IM_COL32( 80, 220, 255, 255 ), 2.0f );

	const idVec3 size = box[1] - box[0];
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const char* text = ( drawStage == DRAW_FOOTPRINT ) ? va( "%.0f x %.0f", size.x, size.y ) : va( "%.0f x %.0f x %.0f   click to finish, Esc cancels", size.x, size.y, size.z );
	ImGui::GetBackgroundDrawList()->AddText( ImVec2( mouse.x + 16.0f, mouse.y + 12.0f ), IM_COL32( 255, 255, 255, 255 ), text );
}

/*
===============================================================================

	Plan view: the level from above, flat. Drag things to move them; with the
	Brush tool, drag on empty space to draw. Wheel zooms, right or middle
	drag pans.

===============================================================================
*/

static bool PlanViewRect( ImVec2& mins, ImVec2& maxs )
{
	ImGuiDockNode* central = ( dockspaceId != 0 ) ? ImGui::DockBuilderGetCentralNode( dockspaceId ) : NULL;
	if( central == NULL )
	{
		return false;
	}
	mins = central->Pos;
	maxs = ImVec2( central->Pos.x + central->Size.x, central->Pos.y + central->Size.y );
	return true;
}

static ImVec2 PlanToScreen( const idVec2& p, const ImVec2& mins, const ImVec2& maxs )
{
	const ImVec2 center( ( mins.x + maxs.x ) * 0.5f, ( mins.y + maxs.y ) * 0.5f );
	return ImVec2( center.x + ( p.x - planCenter.x ) * planZoom, center.y - ( p.y - planCenter.y ) * planZoom );
}

static idVec2 ScreenToPlan( const ImVec2& s, const ImVec2& mins, const ImVec2& maxs )
{
	const ImVec2 center( ( mins.x + maxs.x ) * 0.5f, ( mins.y + maxs.y ) * 0.5f );
	return idVec2( planCenter.x + ( s.x - center.x ) / planZoom, planCenter.y - ( s.y - center.y ) / planZoom );
}

// the outline of a brush seen from above: the convex hull of its corners
static void PlanOutline( const idMapPrimitive* prim, idList<idVec2>& hull, float& top )
{
	hull.Clear();
	idList<idVec2> points;
	top = -idMath::INFINITUM;
	if( prim->GetType() == idMapPrimitive::TYPE_BRUSH )
	{
		idList<idVec3> corners;
		BrushVertices( static_cast<const idMapBrush*>( prim ), corners );
		for( int i = 0; i < corners.Num(); i++ )
		{
			points.Append( corners[i].ToVec2() );
			top = Max( top, corners[i].z );
		}
	}
	else
	{
		idBounds b;
		if( PrimitiveBounds( prim, b ) )
		{
			points.Append( idVec2( b[0].x, b[0].y ) );
			points.Append( idVec2( b[1].x, b[0].y ) );
			points.Append( idVec2( b[1].x, b[1].y ) );
			points.Append( idVec2( b[0].x, b[1].y ) );
			top = b[1].z;
		}
	}
	if( points.Num() < 3 )
	{
		return;
	}

	// gift wrapping; brushes have few corners
	int start = 0;
	for( int i = 1; i < points.Num(); i++ )
	{
		if( points[i].x < points[start].x || ( points[i].x == points[start].x && points[i].y < points[start].y ) )
		{
			start = i;
		}
	}
	int current = start;
	for( int guard = 0; guard < 128; guard++ )
	{
		hull.Append( points[current] );
		int next = ( current + 1 ) % points.Num();
		for( int i = 0; i < points.Num(); i++ )
		{
			const idVec2 a = points[next] - points[current];
			const idVec2 b = points[i] - points[current];
			const float cross = a.x * b.y - a.y * b.x;
			if( cross < -0.001f || ( idMath::Fabs( cross ) <= 0.001f && b.LengthSqr() > a.LengthSqr() ) )
			{
				next = i;
			}
		}
		current = next;
		if( current == start )
		{
			break;
		}
	}
}

static bool PointInHull( const idList<idVec2>& hull, const idVec2& p )
{
	if( hull.Num() < 3 )
	{
		return false;
	}
	for( int i = 0; i < hull.Num(); i++ )
	{
		const idVec2 a = hull[i];
		const idVec2 b = hull[( i + 1 ) % hull.Num()];
		// the hull runs counterclockwise, so inside is on the left of every edge
		const float cross = ( b.x - a.x ) * ( p.y - a.y ) - ( b.y - a.y ) * ( p.x - a.x );
		if( cross < -0.001f )
		{
			return false;
		}
	}
	return true;
}

static float HullArea( const idList<idVec2>& hull )
{
	float area = 0.0f;
	for( int i = 0; i < hull.Num(); i++ )
	{
		const idVec2& a = hull[i];
		const idVec2& b = hull[( i + 1 ) % hull.Num()];
		area += a.x * b.y - b.x * a.y;
	}
	return idMath::Fabs( area ) * 0.5f;
}

// the thing under a plan position: point entities first, then the smallest brush there,
// so clicking inside a room picks what's in it rather than the ceiling over it
static Selection PlanPick( const idVec2& p )
{
	Selection best;
	const idMapFile* map = document.Map();
	if( map == NULL )
	{
		return best;
	}

	float bestArea = idMath::INFINITUM;
	for( int e = 0; e < map->GetNumEntities(); e++ )
	{
		const idMapEntity* ent = map->GetEntity( e );
		if( ent->GetNumPrimitives() == 0 )
		{
			idBounds b;
			EntityBounds( ent, b );
			if( p.x >= b[0].x && p.x <= b[1].x && p.y >= b[0].y && p.y <= b[1].y )
			{
				return Selection( e );
			}
			continue;
		}
		for( int i = 0; i < ent->GetNumPrimitives(); i++ )
		{
			idList<idVec2> hull;
			float top;
			PlanOutline( ent->GetPrimitive( i ), hull, top );
			const float area = HullArea( hull );
			if( area < bestArea && PointInHull( hull, p ) )
			{
				bestArea = area;
				best = Selection( e, ( e == 0 ) ? i : -1 );
			}
		}
	}
	return best;
}

static void CenterPlanOnCamera()
{
	planCenter = view.camera.origin.ToVec2();
}

static void DrawPlanView( bool hovered )
{
	ImVec2 mins, maxs;
	if( !PlanViewRect( mins, maxs ) )
	{
		return;
	}
	ImGuiIO& io = ImGui::GetIO();
	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	draw->AddRectFilled( mins, maxs, IM_COL32( 24, 26, 30, 255 ) );
	draw->PushClipRect( mins, maxs, true );

	// grid, thinned out when zoomed out
	float step = GridSize();
	while( step * planZoom < 8.0f )
	{
		step *= 2.0f;
	}
	const float major = step * 8.0f;
	const idVec2 w0 = ScreenToPlan( ImVec2( mins.x, maxs.y ), mins, maxs );
	const idVec2 w1 = ScreenToPlan( ImVec2( maxs.x, mins.y ), mins, maxs );
	for( float x = idMath::Floor( w0.x / step ) * step; x <= w1.x; x += step )
	{
		const bool isMajor = idMath::Fabs( x - idMath::Rint( x / major ) * major ) < 0.01f;
		const float sx = PlanToScreen( idVec2( x, 0 ), mins, maxs ).x;
		draw->AddLine( ImVec2( sx, mins.y ), ImVec2( sx, maxs.y ), idMath::Fabs( x ) < 0.01f ? IM_COL32( 120, 60, 60, 255 ) : ( isMajor ? IM_COL32( 64, 68, 78, 255 ) : IM_COL32( 40, 43, 50, 255 ) ) );
	}
	for( float y = idMath::Floor( w0.y / step ) * step; y <= w1.y; y += step )
	{
		const bool isMajor = idMath::Fabs( y - idMath::Rint( y / major ) * major ) < 0.01f;
		const float sy = PlanToScreen( idVec2( 0, y ), mins, maxs ).y;
		draw->AddLine( ImVec2( mins.x, sy ), ImVec2( maxs.x, sy ), idMath::Fabs( y ) < 0.01f ? IM_COL32( 60, 120, 60, 255 ) : ( isMajor ? IM_COL32( 64, 68, 78, 255 ) : IM_COL32( 40, 43, 50, 255 ) ) );
	}

	// brushes, lowest first so higher ones draw on top
	const idMapFile* map = document.Map();
	if( map != NULL )
	{
		struct Item
		{
			int			entity;
			int			primitive;
			float		top;
		};
		idList<Item> items;
		for( int e = 0; e < map->GetNumEntities(); e++ )
		{
			const idMapEntity* ent = map->GetEntity( e );
			for( int i = 0; i < ent->GetNumPrimitives(); i++ )
			{
				idList<idVec2> hull;
				Item item = { e, i, 0.0f };
				PlanOutline( ent->GetPrimitive( i ), hull, item.top );
				items.Append( item );
			}
		}
		for( int i = 1; i < items.Num(); i++ )
		{
			for( int j = i; j > 0 && items[j - 1].top > items[j].top; j-- )
			{
				const Item swap = items[j - 1];
				items[j - 1] = items[j];
				items[j] = swap;
			}
		}

		for( int i = 0; i < items.Num(); i++ )
		{
			const Item& item = items[i];
			idList<idVec2> hull;
			float top;
			PlanOutline( map->GetEntity( item.entity )->GetPrimitive( item.primitive ), hull, top );
			if( hull.Num() < 3 )
			{
				continue;
			}
			const bool selected = document.selection.Contains( Selection( item.entity, item.primitive ) ) || ( item.entity != 0 && document.selection.Contains( Selection( item.entity ) ) );
			idList<ImVec2> pts;
			for( int k = 0; k < hull.Num(); k++ )
			{
				pts.Append( PlanToScreen( hull[k], mins, maxs ) );
			}
			const ImU32 fill = item.entity == 0 ? IM_COL32( 120, 130, 150, 60 ) : IM_COL32( 60, 160, 220, 70 );
			draw->AddConvexPolyFilled( pts.Ptr(), pts.Num(), selected ? IM_COL32( 255, 200, 40, 70 ) : fill );
			draw->AddPolyline( pts.Ptr(), pts.Num(), selected ? IM_COL32( 255, 200, 40, 255 ) : IM_COL32( 200, 205, 215, 255 ), ImDrawFlags_Closed, selected ? 2.0f : 1.0f );
		}

		// point entities, with the way they face
		for( int e = 1; e < map->GetNumEntities(); e++ )
		{
			const idMapEntity* ent = map->GetEntity( e );
			if( ent->GetNumPrimitives() > 0 )
			{
				continue;
			}
			idBounds b;
			EntityBounds( ent, b );
			idVec3 color( 0.5f, 0.5f, 1.0f );
			const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->FindType( DECL_ENTITYDEF, ent->epairs.GetString( "classname" ), false ) );
			if( def != NULL )
			{
				def->dict.GetVector( "editor_color", "0.5 0.5 1", color );
			}
			const bool selected = document.selection.Contains( Selection( e ) );
			const ImU32 col = selected ? IM_COL32( 255, 200, 40, 255 ) : IM_COL32( ( int )( color.x * 255 ), ( int )( color.y * 255 ), ( int )( color.z * 255 ), 255 );
			const ImVec2 a = PlanToScreen( idVec2( b[0].x, b[1].y ), mins, maxs );
			const ImVec2 c = PlanToScreen( idVec2( b[1].x, b[0].y ), mins, maxs );
			draw->AddRect( a, ImVec2( Max( c.x, a.x + 6.0f ), Max( c.y, a.y + 6.0f ) ), col, 0.0f, 0, selected ? 2.0f : 1.0f );

			idMat3 axis;
			if( EntityAxis( ent, axis ) || idStr::Icmp( ent->epairs.GetString( "classname" ), "info_player_start" ) == 0 )
			{
				const idVec2 o = b.GetCenter().ToVec2();
				const float len = Max( 24.0f, ( b[1].x - b[0].x ) );
				const ImVec2 so = PlanToScreen( o, mins, maxs );
				const ImVec2 st = PlanToScreen( o + axis[0].ToVec2() * len, mins, maxs );
				draw->AddLine( so, st, col, 2.0f );
				draw->AddCircleFilled( st, 3.0f, col );
			}
		}
	}

	// where the 3D camera is and which way it looks
	{
		const ImVec2 c = PlanToScreen( view.camera.origin.ToVec2(), mins, maxs );
		const float yaw = DEG2RAD( view.camera.yaw );
		const ImVec2 f( cosf( yaw ) * 14.0f, -sinf( yaw ) * 14.0f );
		const ImVec2 l( -sinf( yaw ) * 7.0f, -cosf( yaw ) * 7.0f );
		draw->AddTriangleFilled( ImVec2( c.x + f.x, c.y + f.y ), ImVec2( c.x + l.x, c.y + l.y ), ImVec2( c.x - l.x, c.y - l.y ), IM_COL32( 255, 255, 255, 160 ) );
	}

	// a box being drawn
	if( drawStage != DRAW_NONE )
	{
		const ImVec2 a = PlanToScreen( drawStart.ToVec2(), mins, maxs );
		const ImVec2 b = PlanToScreen( drawEnd.ToVec2(), mins, maxs );
		draw->AddRect( ImVec2( Min( a.x, b.x ), Min( a.y, b.y ) ), ImVec2( Max( a.x, b.x ), Max( a.y, b.y ) ), IM_COL32( 80, 220, 255, 255 ), 0.0f, 0, 2.0f );
		draw->AddText( ImVec2( io.MousePos.x + 16.0f, io.MousePos.y + 12.0f ), IM_COL32( 255, 255, 255, 255 ),
					   va( "%.0f x %.0f, %.0f high", idMath::Fabs( drawEnd.x - drawStart.x ), idMath::Fabs( drawEnd.y - drawStart.y ), drawHeight ) );
	}

	draw->PopClipRect();

	// ---- input ----
	const ImVec2 mouse = io.MousePos;
	const idVec2 world = ScreenToPlan( mouse, mins, maxs );

	// zoom around the pointer
	if( hovered && io.MouseWheel != 0.0f )
	{
		const float factor = io.MouseWheel > 0.0f ? 1.25f : 0.8f;
		planZoom = idMath::ClampFloat( 0.01f, 16.0f, planZoom * factor );
		const idVec2 after = ScreenToPlan( mouse, mins, maxs );
		planCenter += world - after;
	}

	// pan with the right or middle button
	if( hovered && ( ImGui::IsMouseClicked( ImGuiMouseButton_Right ) || ImGui::IsMouseClicked( ImGuiMouseButton_Middle ) ) )
	{
		planPanning = true;
		planLastMouse.Set( mouse.x, mouse.y );
	}
	if( planPanning )
	{
		if( !ImGui::IsMouseDown( ImGuiMouseButton_Right ) && !ImGui::IsMouseDown( ImGuiMouseButton_Middle ) )
		{
			planPanning = false;
		}
		else
		{
			planCenter.x -= ( mouse.x - planLastMouse.x ) / planZoom;
			planCenter.y += ( mouse.y - planLastMouse.y ) / planZoom;
			planLastMouse.Set( mouse.x, mouse.y );
			return;
		}
	}

	const float grid = GridSize();
	auto snap = [&]( float v )
	{
		return editor_snap.GetBool() ? idMath::Rint( v / grid ) * grid : v;
	};

	// moving the selection
	if( planMoving )
	{
		const idVec3 delta( snap( world.x - planMoveStart.x ), snap( world.y - planMoveStart.y ), 0.0f );
		document.UpdateTransform( mat3_identity, delta );
		if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
		{
			planMoving = false;
			document.EndTransform();
			if( delta.LengthSqr() > 0.0f )
			{
				document.EndChange();
			}
			else
			{
				document.CancelChange();
			}
		}
		return;
	}

	// drawing a box from above: height from the toolbar, standing on the grid
	if( drawStage == DRAW_FOOTPRINT )
	{
		drawEnd.Set( snap( world.x ), snap( world.y ), drawBaseZ );
		if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
		{
			if( idMath::Fabs( drawEnd.x - drawStart.x ) >= grid * 0.5f && idMath::Fabs( drawEnd.y - drawStart.y ) >= grid * 0.5f )
			{
				FinishBrushDraw();
			}
			else
			{
				CancelBrushDraw();
			}
		}
		return;
	}

	if( !hovered || !ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
	{
		return;
	}

	const Selection hit = PlanPick( world );
	if( !hit.IsEmpty() && !io.KeyCtrl )
	{
		// grab: select it if it isn't already, then drag to move
		if( !document.selection.Contains( hit ) )
		{
			document.selection.Set( hit );
		}
		if( document.CanTransformSelection() )
		{
			planMoving = true;
			planMoveStart = world;
			document.BeginChange( "Move" );
			document.BeginTransform();
		}
		return;
	}
	if( io.KeyCtrl )
	{
		document.selection.Toggle( hit );
		return;
	}

	// empty space
	if( gizmoMode == GIZMO_BRUSH && document.IsOpen() )
	{
		drawBaseZ = view.gridZ;
		drawStart.Set( snap( world.x ), snap( world.y ), drawBaseZ );
		drawEnd = drawStart;
		drawStage = DRAW_FOOTPRINT;
	}
	else
	{
		document.selection.Clear();
	}
}

/*
===============================================================================

	Shapes dragged from the toolbar into the view

===============================================================================
*/


/*
===============================================================================

	Viewport overlay: view bar, mode tabs and tool dock, context bar, tool wheel

===============================================================================
*/

struct toolInfo_t
{
	gizmoMode_t		mode;
	const char* 	icon;
	const char* 	label;
	const char* 	key;
	const char* 	tip;
};

static const toolInfo_t toolInfo[] =
{
	{ GIZMO_MOVE,	ICON_MOVE,		"Move",		"W", "Select things and drag the arrows to move them" },
	{ GIZMO_ROTATE,	ICON_ROTATE,	"Turn",		"E", "Drag the rings to turn the selection. It snaps to 15 degrees; , and . turn by 90" },
	{ GIZMO_RESIZE,	ICON_RESIZE,	"Resize",	"R", "Drag the dots on the box edges to resize" },
	{ GIZMO_BRUSH,	ICON_DRAW,		"Draw",		"B", "Draw new brushes: drag a footprint on the floor, let go, move the mouse for the height, click" },
	{ GIZMO_CLIP,	ICON_CLIP,		"Cut",		"C", "Cut the selected brushes in two: click two or three points on them, then press Enter" },
	{ GIZMO_VERTEX,	ICON_CORNERS,	"Corners",	"V", "Drag the corners of the selected brushes to reshape them" },
	{ GIZMO_PAINT,	ICON_PAINT,		"Paint",	"P", "Click surfaces to paint them with the current material. Ctrl+click paints a whole brush" },
};
static const int NUM_TOOLS = sizeof( toolInfo ) / sizeof( toolInfo[0] );

static const toolInfo_t& ToolInfo( gizmoMode_t mode )
{
	for( int i = 0; i < NUM_TOOLS; i++ )
	{
		if( toolInfo[i].mode == mode )
		{
			return toolInfo[i];
		}
	}
	return toolInfo[0];
}

struct modeInfo_t
{
	const char* 	icon;
	const char* 	label;
	const char* 	tip;
};

static const modeInfo_t modeInfo[MODE_COUNT] =
{
	{ ICON_MODE_BUILD,	"Build",	"Draw, shape, cut and arrange the level's brushes (1)" },
	{ ICON_MODE_PAINT,	"Paint",	"Put materials on surfaces (2)" },
	{ ICON_MODE_LOGIC,	"Logic",	"Make things do things: doors, the player start, and later rule cards (3)" },
	{ ICON_MODE_LIGHT,	"Light",	"Add and set up lights (4)" },
	{ ICON_MODE_KITS,	"Kits",		"Ready-made pieces to drop into the level (5)" },
};

static void SetTool( gizmoMode_t mode )
{
	if( mode == GIZMO_CLIP && gizmoMode != GIZMO_CLIP )
	{
		clipPoints.Clear();
	}
	if( mode == GIZMO_VERTEX && gizmoMode != GIZMO_VERTEX )
	{
		cornerSelection.Clear();
	}
	gizmoMode = mode;
	paintPicking = false;
	if( mode == GIZMO_PAINT )
	{
		editMode = MODE_PAINT;
	}
	else if( editMode == MODE_PAINT )
	{
		editMode = MODE_BUILD;
	}
}

static void SetMode( editMode_t mode )
{
	editMode = mode;
	if( mode == MODE_PAINT )
	{
		SetTool( GIZMO_PAINT );
	}
	else if( gizmoMode == GIZMO_PAINT )
	{
		gizmoMode = GIZMO_MOVE;
		paintPicking = false;
	}
}

static void TogglePlanView()
{
	planView = !planView;
	if( planView )
	{
		CenterPlanOnCamera();
	}
}

// a short, readable name for a material: its last path part
static idStr ShortMaterialName( const char* name )
{
	idStr s = name;
	const int slash = s.Last( '/' );
	if( slash >= 0 )
	{
		s = s.Right( s.Length() - slash - 1 );
	}
	return s;
}

// paints the face under the cursor (Ctrl: the whole brush), or picks its material up
static void PaintClick( float x, float y, bool wholeBrush )
{
	const Selection face = view.Pick( document, x, y, true );
	if( !face.IsFace() )
	{
		return;
	}
	idMapPrimitive* prim = document.Map()->GetEntity( face.entity )->GetPrimitive( face.primitive );
	if( prim->GetType() != idMapPrimitive::TYPE_BRUSH )
	{
		return;
	}
	idMapBrush* brush = static_cast<idMapBrush*>( prim );

	if( paintPicking )
	{
		currentMaterial = brush->GetSide( face.side )->GetMaterial();
		paintPicking = false;
		Status( va( "Painting with %s.", currentMaterial.c_str() ) );
		return;
	}

	if( !wholeBrush && idStr::Icmp( brush->GetSide( face.side )->GetMaterial(), currentMaterial ) == 0 )
	{
		return;
	}
	document.BeginChange( "Paint" );
	for( int s = 0; s < brush->GetNumSides(); s++ )
	{
		if( wholeBrush || s == face.side )
		{
			brush->GetSide( s )->SetMaterial( currentMaterial );
		}
	}
	document.EndChange();
}

/*
-------------------------------------------------------------------------------
	floating panels over the view share these flags
-------------------------------------------------------------------------------
*/

static const ImGuiWindowFlags OVERLAY_FLAGS = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

static bool BeginOverlay( const char* id, const ImVec2& pos, const ImVec2& pivot, float rounding )
{
	ImGui::SetNextWindowPos( pos, ImGuiCond_Always, pivot );
	ImGui::SetNextWindowBgAlpha( 0.94f );
	ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 4.0f * UI::Scale(), 6.0f * UI::Scale() ) );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, rounding );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 8.0f * UI::Scale(), 8.0f * UI::Scale() ) );
	const bool open = ImGui::Begin( id, NULL, OVERLAY_FLAGS );
	ImGui::PopStyleVar( 2 );		// item spacing stays until EndOverlay
	return open;
}

static void EndOverlay()
{
	ImGui::End();
	ImGui::PopStyleVar();
}

/*
-------------------------------------------------------------------------------
	view bar: top left of the view
-------------------------------------------------------------------------------
*/

static void DrawViewBar( const ImGuiDockNode* central )
{
	const float pad = 10.0f * UI::Scale();
	if( BeginOverlay( "##view_bar", ImVec2( central->Pos.x + pad, central->Pos.y + pad ), ImVec2( 0, 0 ), 10.0f * UI::Scale() ) )
	{
		if( UI::IconButton( "3d", ICON_VIEW_3D, "3D view (T switches)", !planView ) && planView )
		{
			TogglePlanView();
		}
		ImGui::SameLine();
		if( UI::IconButton( "top", ICON_TOP_VIEW, "Top view: lay out rooms from above (T switches)", planView ) && !planView )
		{
			TogglePlanView();
		}
		UI::VerticalDivider();

		static const int gridSizes[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256 };
		ImGui::SetNextItemWidth( 84.0f * UI::Scale() );
		if( ImGui::BeginCombo( "##grid", va( ICON_GRID "  %d", editor_gridSize.GetInteger() ), ImGuiComboFlags_HeightLarge ) )
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
		UI::Tip( "Grid size. [ and ] make it smaller and bigger." );
		ImGui::SameLine();
		bool snap = editor_snap.GetBool();
		if( UI::IconToggle( "snap", ICON_SNAP, NULL, &snap, "Snap to the grid" ) )
		{
			editor_snap.SetBool( snap );
		}
		ImGui::SameLine();
		UI::IconToggle( "texlock", ICON_LOCK, ICON_UNLOCK, &document.textureLock, "Texture lock: textures stay put on surfaces when you move, turn or resize brushes" );
		UI::VerticalDivider();
		UI::IconToggle( "outlines", ICON_WIREFRAME, NULL, &view.wireframe, "Brush outlines" );
		ImGui::SameLine();
		UI::IconToggle( "floor", ICON_GRID, NULL, &view.showGrid, "Floor grid" );
	}
	EndOverlay();
}

/*
-------------------------------------------------------------------------------
	tool dock: mode tabs over a row of big tool tiles, bottom centre of the view
-------------------------------------------------------------------------------
*/

static float dockTile = 62.0f;

static void TileDivider()
{
	ImGui::SameLine( 0.0f, 8.0f * UI::Scale() );
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddLine( ImVec2( pos.x, pos.y + dockTile * 0.18f ), ImVec2( pos.x, pos.y + dockTile * 0.82f ), ImGui::GetColorU32( ImGuiCol_Separator ), 1.0f );
	ImGui::Dummy( ImVec2( 1.0f, dockTile ) );
	ImGui::SameLine( 0.0f, 8.0f * UI::Scale() );
}

// starts dragging a piece out of the dock when the tile just pressed is pulled
static void DragOut( int shape, const char* classname )
{
	if( ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Left, 6.0f ) )
	{
		draggingShape = shape;
		draggingThing = classname != NULL ? classname : "";
	}
}

static bool ToolTileFor( const toolInfo_t& tool )
{
	const bool active = ( gizmoMode == tool.mode ) && !( tool.mode == GIZMO_PAINT && paintPicking );
	if( UI::ToolTile( tool.label, tool.icon, tool.label, active, va( "%s (%s)\n%s", tool.label, tool.key, tool.tip ), true, dockTile ) )
	{
		SetTool( tool.mode );
		return true;
	}
	return false;
}

// things that go in the level as entities: what the Kits and Logic tabs offer
static const char* ThingIcon( const idDeclEntityDef* def )
{
	const char* name = def->GetName();
	if( idStr::FindText( name, "light" ) >= 0 )
	{
		return ICON_LIGHT;
	}
	if( idStr::FindText( name, "player" ) >= 0 )
	{
		return ICON_PLAYER;
	}
	if( idStr::FindText( name, "door" ) >= 0 )
	{
		return ICON_DOOR;
	}
	if( def->dict.GetBool( "editor_brushes" ) )
	{
		return ICON_BRUSH;
	}
	return ICON_ENTITY;
}

static const char* ThingLabel( const idDeclEntityDef* def )
{
	const char* label = def->dict.GetString( "editor_label" );
	if( label[0] != '\0' )
	{
		return label;
	}
	if( idStr::Icmp( def->GetName(), "info_player_start" ) == 0 )
	{
		return "Player start";
	}
	if( idStr::Icmp( def->GetName(), "light" ) == 0 )
	{
		return "Light";
	}
	return def->GetName();
}

static void PlaceThing( const char* classname )
{
	if( document.IsOpen() )
	{
		const idVec3 lift = idStr::Icmp( classname, "light" ) == 0 ? idVec3( 0, 0, 64 ) : vec3_origin;
		document.AddEntity( classname, PlacementPoint() + lift );
	}
}

struct lightPreset_t
{
	const char* 	label;
	const char* 	tip;
	float			radius;
	idVec3			color;
};

static const lightPreset_t lightPresets[] =
{
	{ "Small",	"A small light: a lamp or a candle's glow",	160.0f,	idVec3( 1.0f, 1.0f, 1.0f ) },
	{ "Room",	"Lights a normal room",						320.0f,	idVec3( 1.0f, 1.0f, 1.0f ) },
	{ "Big",	"Lights a hall or a yard",					640.0f,	idVec3( 1.0f, 1.0f, 1.0f ) },
	{ "Warm",	"A room light with a warm, yellow tint",	320.0f,	idVec3( 1.0f, 0.84f, 0.62f ) },
	{ "Cool",	"A room light with a cool, blue tint",		320.0f,	idVec3( 0.70f, 0.82f, 1.0f ) },
};

static void PlaceLight( const lightPreset_t& preset, const idVec3& point )
{
	if( !document.IsOpen() )
	{
		return;
	}
	idDict keys;
	keys.Set( "light_radius", idVec3( preset.radius, preset.radius, preset.radius ).ToString( 0 ) );
	keys.Set( "_color", preset.color.ToString( 2 ) );
	document.AddEntity( "light", point + idVec3( 0, 0, 64 ), &keys );
}

static void DrawBuildTiles()
{
	for( int i = 0; i < NUM_TOOLS; i++ )
	{
		if( toolInfo[i].mode == GIZMO_PAINT )
		{
			continue;
		}
		if( i > 0 )
		{
			ImGui::SameLine();
		}
		ToolTileFor( toolInfo[i] );
	}
	TileDivider();
	static const char* shapeIcons[SHAPE_COUNT] = { ICON_SHAPE_BOX, ICON_SHAPE_ROOM, ICON_SHAPE_RAMP, ICON_SHAPE_CYLINDER, ICON_SHAPE_CONE, ICON_SHAPE_STAIRS, ICON_SHAPE_ARCH };
	for( int i = 0; i < SHAPE_COUNT; i++ )
	{
		if( i > 0 )
		{
			ImGui::SameLine();
		}
		const bool active = ( drawShape == i && gizmoMode == GIZMO_BRUSH );
		const char* name = ShapeName( ( shapeType_t )i );
		if( UI::ToolTile( name, shapeIcons[i], name, active, draggingShape < 0 ? va( "%s\n\nClick, then drag in the view to draw one any size. Or drag this into the view to drop one.", ShapeHelp( ( shapeType_t )i ) ) : NULL, document.IsOpen(), dockTile ) )
		{
			drawShape = i;
			SetTool( GIZMO_BRUSH );
		}
		DragOut( i, NULL );
	}
}

static void DrawPaintTiles()
{
	ToolTileFor( ToolInfo( GIZMO_PAINT ) );
	ImGui::SameLine();
	if( UI::ToolTile( "pick", ICON_PICK, "Pick", paintPicking, "Pick up a material: click a surface to paint with what's on it (I)", true, dockTile ) )
	{
		SetTool( GIZMO_PAINT );
		paintPicking = true;
	}
	ImGui::SameLine();
	const idStr shortName = ShortMaterialName( currentMaterial );
	if( UI::ToolTile( "material", ICON_MATERIAL, shortName.c_str(), false, va( "Painting with %s\nClick to choose another", currentMaterial.c_str() ), true, dockTile ) )
	{
		ImGui::OpenPopup( "##material_picker" );
	}
	if( ImGui::BeginPopup( "##material_picker" ) )
	{
		UI::SearchField( "##pickfilter", materialFilter, sizeof( materialFilter ), "Find a material", 320.0f * UI::Scale() );
		ImGui::BeginChild( "##picklist", ImVec2( 320.0f * UI::Scale(), 300.0f * UI::Scale() ) );
		const int count = declManager->GetNumDecls( DECL_MATERIAL );
		for( int i = 0; i < count; i++ )
		{
			const idDecl* decl = declManager->DeclByIndex( DECL_MATERIAL, i, false );
			if( decl == NULL || decl->GetName()[0] == '_' || ( materialFilter[0] != '\0' && idStr::FindText( decl->GetName(), materialFilter, false ) < 0 ) )
			{
				continue;
			}
			if( ImGui::Selectable( decl->GetName(), currentMaterial.Icmp( decl->GetName() ) == 0 ) )
			{
				currentMaterial = decl->GetName();
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndChild();
		ImGui::EndPopup();
	}
	TileDivider();
	if( UI::ToolTile( "apply", ICON_PAINT, "Apply", false, "Put the current material on everything selected (whole brushes, or just the selected faces)", !document.selection.IsEmpty(), dockTile ) )
	{
		document.SetMaterial( currentMaterial );
	}
	ImGui::SameLine();
	if( UI::ToolTile( "fit", ICON_FIT, "Fit", false, "Stretch the texture to cover each selected face once (Shift+click faces to select them)", document.selection.IsFaces(), dockTile ) )
	{
		FitSelectedFaces();
	}
	ImGui::SameLine();
	if( UI::ToolTile( "texlock", document.textureLock ? ICON_LOCK : ICON_UNLOCK, "Tex lock", document.textureLock, "Texture lock: textures stay put on surfaces when you move, turn or resize brushes", true, dockTile ) )
	{
		document.textureLock = !document.textureLock;
	}
}

static void DrawLogicTiles()
{
	// level brushes can be turned into things that move or act
	idList<Selection> brushes;
	document.SelectedBrushes( brushes );
	bool allLevel = brushes.Num() > 0;
	for( int i = 0; i < document.selection.Num(); i++ )
	{
		allLevel &= ( document.selection[i].entity == 0 );
	}

	int shown = 0;
	const int count = declManager->GetNumDecls( DECL_ENTITYDEF );
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
		const char* label = ThingLabel( def );
		if( UI::ToolTile( def->GetName(), ThingIcon( def ), label, false,
						  va( "Turn the selected brushes into a %s.\n%s%s", label, def->dict.GetString( "editor_usage" ), allLevel ? "" : "\n\nSelect some of the level's brushes first." ), allLevel, dockTile ) )
		{
			if( document.GroupSelection( def->GetName() ) >= 0 )
			{
				Status( va( "Made a %s.", label ) );
			}
		}
	}

	const bool brushEntity = document.selection.Num() == 1 && document.selection.Primary().entity > 0 && !document.selection.Primary().IsPrimitive() &&
							 document.SelectedEntity() != NULL && document.SelectedEntity()->GetNumPrimitives() > 0;
	ImGui::SameLine();
	if( UI::ToolTile( "ungroup", ICON_UNGROUP, "Undo it", false, "Turn the selected door (or other brush thing) back into plain level brushes", brushEntity, dockTile ) )
	{
		document.UngroupSelection();
	}

	TileDivider();
	if( UI::ToolTile( "start", ICON_PLAYER, "Start", false, "Add the place where the player starts. Click to add it in the middle of the view, or drag it in.", document.IsOpen(), dockTile ) )
	{
		PlaceThing( "info_player_start" );
	}
	DragOut( -1, "info_player_start" );
	ImGui::SameLine();
	UI::ToolTile( "rules", ICON_CARDS, "Rules", false, "Rule cards: \"When the player walks in, Do open the door\". Coming in Phase 3.", false, dockTile );
}

static void DrawLightTiles()
{
	for( int i = 0; i < ( int )( sizeof( lightPresets ) / sizeof( lightPresets[0] ) ); i++ )
	{
		if( i > 0 )
		{
			ImGui::SameLine();
		}
		const lightPreset_t& preset = lightPresets[i];
		if( UI::ToolTile( preset.label, ICON_LIGHT, preset.label, false, va( "%s.\nClick to add one in the middle of the view, or drag it in. Change its colour and reach in the Inspector.", preset.tip ), document.IsOpen(), dockTile ) )
		{
			PlaceLight( preset, PlacementPoint() );
		}
		DragOut( -1, va( "light:%d", i ) );
	}
}

static void DrawKitTiles()
{
	int shown = 0;
	const int count = declManager->GetNumDecls( DECL_ENTITYDEF );
	for( int i = 0; i < count; i++ )
	{
		const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->DeclByIndex( DECL_ENTITYDEF, i, true ) );
		if( def == NULL || def->dict.GetString( "editor_usage" )[0] == '\0' || def->dict.GetBool( "editor_brushes" ) ||
				idStr::Icmp( def->GetName(), "worldspawn" ) == 0 || idStr::Icmp( def->GetName(), "player" ) == 0 )
		{
			continue;
		}
		if( shown++ > 0 )
		{
			ImGui::SameLine();
		}
		if( UI::ToolTile( def->GetName(), ThingIcon( def ), ThingLabel( def ), false, va( "%s\n\nClick to add one in the middle of the view, or drag it in.", def->dict.GetString( "editor_usage" ) ), document.IsOpen(), dockTile ) )
		{
			PlaceThing( def->GetName() );
		}
		DragOut( -1, def->GetName() );
	}
	TileDivider();
	UI::ToolTile( "mine", ICON_MODE_KITS, "My pieces", false, "Save your own builds, behaviour included, as pieces to reuse. Coming in Phase 2.", false, dockTile );
}

static ImVec2 dockTopCenter( 0, 0 );

static int CountDefs( bool brushThings )
{
	int n = 0;
	const int count = declManager->GetNumDecls( DECL_ENTITYDEF );
	for( int i = 0; i < count; i++ )
	{
		const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->DeclByIndex( DECL_ENTITYDEF, i, true ) );
		if( def == NULL )
		{
			continue;
		}
		if( brushThings )
		{
			n += def->dict.GetBool( "editor_brushes" ) ? 1 : 0;
		}
		else if( def->dict.GetString( "editor_usage" )[0] != '\0' && !def->dict.GetBool( "editor_brushes" ) &&
				 idStr::Icmp( def->GetName(), "worldspawn" ) != 0 && idStr::Icmp( def->GetName(), "player" ) != 0 )
		{
			n++;
		}
	}
	return n;
}

// how wide a row of tiles is
static float TileRowWidth( int tiles, int dividers )
{
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	return tiles * dockTile + Max( 0, tiles - 1 ) * spacing + dividers * ( 17.0f * UI::Scale() - spacing );
}

static float ModeRowWidth( editMode_t mode )
{
	switch( mode )
	{
		case MODE_BUILD:
			return TileRowWidth( 6 + SHAPE_COUNT, 1 );
		case MODE_PAINT:
			return TileRowWidth( 6, 1 );
		case MODE_LOGIC:
			return TileRowWidth( CountDefs( true ) + 3, 1 );
		case MODE_LIGHT:
			return TileRowWidth( 5, 0 );
		default:
			return TileRowWidth( CountDefs( false ) + 1, 1 );
	}
}

static void DrawToolDock( const ImGuiDockNode* central )
{
	// tiles shrink to fit narrow views; Build has the most: 13, a divider, the spacing and the padding
	const float s = UI::Scale();
	const float avail = central->Size.x - 24.0f * s;
	dockTile = idMath::ClampFloat( 36.0f * s, UI::TileSize(), ( avail - 12.0f * 4.0f * s - 17.0f * s - 16.0f * s ) / 13.0f );

	const ImVec2 pos( central->Pos.x + central->Size.x * 0.5f, central->Pos.y + central->Size.y - 12.0f * UI::Scale() );
	// the dock keeps the width of its widest tab, so the tabs don't jump about
	float widest = 0.0f;
	for( int i = 0; i < MODE_COUNT; i++ )
	{
		widest = Max( widest, ModeRowWidth( ( editMode_t )i ) );
	}
	ImGui::SetNextWindowSizeConstraints( ImVec2( widest + 16.0f * UI::Scale(), 0.0f ), ImVec2( FLT_MAX, FLT_MAX ) );
	if( BeginOverlay( "##tool_dock", pos, ImVec2( 0.5f, 1.0f ), 14.0f * UI::Scale() ) )
	{
		// mode tabs, centred over the tiles
		float tabsWidth = 0.0f;
		for( int i = 0; i < MODE_COUNT; i++ )
		{
			tabsWidth += ImGui::CalcTextSize( va( "%s  %s", modeInfo[i].icon, modeInfo[i].label ) ).x + 28.0f * UI::Scale() + ( i > 0 ? ImGui::GetStyle().ItemSpacing.x : 0.0f );
		}
		const float windowWidth = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
		if( windowWidth > tabsWidth )
		{
			ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( windowWidth - tabsWidth ) * 0.5f );
		}
		for( int i = 0; i < MODE_COUNT; i++ )
		{
			if( i > 0 )
			{
				ImGui::SameLine();
			}
			if( UI::ModeTab( modeInfo[i].icon, modeInfo[i].label, editMode == i, modeInfo[i].tip ) )
			{
				SetMode( ( editMode_t )i );
			}
		}

		// the row of tiles, centred
		const float rowWidth = ModeRowWidth( editMode );
		if( windowWidth > rowWidth )
		{
			ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( windowWidth - rowWidth ) * 0.5f );
		}
		switch( editMode )
		{
			case MODE_BUILD:
				DrawBuildTiles();
				break;
			case MODE_PAINT:
				DrawPaintTiles();
				break;
			case MODE_LOGIC:
				DrawLogicTiles();
				break;
			case MODE_LIGHT:
				DrawLightTiles();
				break;
			default:
				DrawKitTiles();
				break;
		}
		dockTopCenter = ImVec2( ImGui::GetWindowPos().x + ImGui::GetWindowSize().x * 0.5f, ImGui::GetWindowPos().y );
	}
	EndOverlay();
}

/*
-------------------------------------------------------------------------------
	context bar: what the tool needs from you, and quick actions on the selection
-------------------------------------------------------------------------------
*/

static void DrawSelectionActions()
{
	const bool canTransform = document.CanTransformSelection();
	if( !canTransform )
	{
		return;
	}
	ImGui::AlignTextToFramePadding();
	const int count = document.selection.Num();
	ImGui::TextDisabled( count == 1 ? "1 selected" : "%d selected", count );
	ImGui::SameLine();
	if( UI::IconButton( "turnl", ICON_TURN_LEFT, "Turn 90 degrees left (,)" ) )
	{
		TurnSelection( 90.0f );
	}
	ImGui::SameLine();
	if( UI::IconButton( "turnr", ICON_TURN_RIGHT, "Turn 90 degrees right (.)" ) )
	{
		TurnSelection( -90.0f );
	}
	idList<Selection> brushes;
	document.SelectedBrushes( brushes );
	ImGui::SameLine();
	if( UI::IconButton( "carve", ICON_CARVE, "Carve: cut the selected brushes out of everything they overlap, then remove them. For doorways and windows.", false, brushes.Num() > 0 ) )
	{
		const int pieces = document.SubtractSelection();
		Status( pieces > 0 ? va( "Carved out; %d pieces left around it.", pieces ) : "Nothing to carve: the selection doesn't overlap any other brush." );
	}
	ImGui::SameLine();
	if( UI::IconButton( "dup", ICON_DUPLICATE, "Duplicate (Ctrl+D)" ) )
	{
		document.DuplicateSelection( idVec3( GridSize() * 4.0f, GridSize() * 4.0f, 0.0f ) );
	}
	ImGui::SameLine();
	if( UI::IconButton( "focus", ICON_FOCUS, "Frame the selection (F)" ) )
	{
		FocusSelection();
	}
	ImGui::SameLine();
	if( UI::IconButton( "delete", ICON_DELETE, "Delete (Del)" ) )
	{
		document.DeleteSelection();
	}
}

static void DrawContextBar()
{
	if( dockTopCenter.y <= 0.0f || !document.IsOpen() )
	{
		return;
	}

	// what to say, and which controls, for the tool in hand
	idStr hint;
	bool clipOptions = false;
	bool drawOptions = false;
	if( gizmoMode == GIZMO_BRUSH )
	{
		hint = planView ? "Drag on empty space to draw. Drag things to move them." : "Drag a footprint on the floor, let go, move the mouse for height, click. Esc cancels.";
		drawOptions = planView;
	}
	else if( gizmoMode == GIZMO_CLIP )
	{
		idList<Selection> brushes;
		document.SelectedBrushes( brushes );
		if( brushes.Num() == 0 )
		{
			hint = "Select the brushes to cut first: Esc, then click one.";
		}
		else
		{
			hint = va( "%d of 2-3 points placed. Click on the brushes to place points.", clipPoints.Num() );
			clipOptions = true;
		}
	}
	else if( gizmoMode == GIZMO_VERTEX )
	{
		hint = cornerSelection.Num() == 0 ? "Click a corner dot to pick it, Ctrl+click for more, then drag the arrows." : va( "%d corners picked. Drag the arrows; the brush reshapes around them.", cornerSelection.Num() );
	}
	else if( gizmoMode == GIZMO_PAINT )
	{
		hint = paintPicking ? "Click a surface to pick up its material." : va( "Click surfaces to paint them with %s. Ctrl+click paints the whole brush; I picks a material up.", ShortMaterialName( currentMaterial ).c_str() );
	}
	else if( planView )
	{
		hint = "Top view: drag things to move them. Wheel zooms, right or middle drag pans.";
	}
	const bool actions = document.CanTransformSelection() && ( gizmoMode == GIZMO_MOVE || gizmoMode == GIZMO_ROTATE || gizmoMode == GIZMO_RESIZE );
	if( hint.IsEmpty() && !actions )
	{
		return;
	}

	if( BeginOverlay( "##context_bar", ImVec2( dockTopCenter.x, dockTopCenter.y - 8.0f * UI::Scale() ), ImVec2( 0.5f, 1.0f ), 20.0f * UI::Scale() ) )
	{
		bool first = true;
		if( !hint.IsEmpty() )
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored( UI::Accent(), "%s", ToolInfo( gizmoMode ).icon );
			ImGui::SameLine();
			ImGui::TextUnformatted( hint.c_str() );
			first = false;
		}
		if( clipOptions )
		{
			ImGui::SameLine( 0.0f, 12.0f * UI::Scale() );
			static const char* keepNames[] = { "Keep both", "Keep yellow", "Keep blue" };
			ImGui::SetNextItemWidth( 120.0f * UI::Scale() );
			ImGui::Combo( "##keep", &clipKeep, keepNames, 3 );
			UI::Tip( "Which side of the cut stays (Tab cycles)" );
			ImGui::SameLine();
			ImGui::BeginDisabled( clipPoints.Num() < 2 );
			if( UI::ColorButton( ICON_CLIP "  Cut", UI::Accent() ) )
			{
				ApplyClip();
			}
			ImGui::EndDisabled();
			UI::Tip( "Cut (Enter)" );
			ImGui::SameLine();
			if( UI::IconButton( "restart", ICON_UNDO, "Start over (Esc)" ) )
			{
				clipPoints.Clear();
			}
		}
		if( drawOptions )
		{
			ImGui::SameLine( 0.0f, 12.0f * UI::Scale() );
			ImGui::SetNextItemWidth( 120.0f * UI::Scale() );
			ImGui::DragFloat( "##height", &drawHeight, 1.0f, 1.0f, 4096.0f, "height %.0f" );
			ImGui::SameLine();
			ImGui::SetNextItemWidth( 120.0f * UI::Scale() );
			ImGui::DragFloat( "##floor", &view.gridZ, 1.0f, -65536.0f, 65536.0f, "floor at %.0f" );
		}
		if( actions )
		{
			if( !first )
			{
				UI::VerticalDivider();
			}
			DrawSelectionActions();
		}
	}
	EndOverlay();
}

/*
-------------------------------------------------------------------------------
	tool wheel: hold Space for a ring of tools around the cursor
-------------------------------------------------------------------------------
*/

struct wheelItem_t
{
	const char* 	icon;
	const char* 	label;
	int				tool;		// gizmoMode_t, or -1 for the top view switch
};

static const wheelItem_t wheelItems[] =
{
	{ ICON_MOVE,		"Move",		GIZMO_MOVE },
	{ ICON_ROTATE,		"Turn",		GIZMO_ROTATE },
	{ ICON_RESIZE,		"Resize",	GIZMO_RESIZE },
	{ ICON_DRAW,		"Draw",		GIZMO_BRUSH },
	{ ICON_PAINT,		"Paint",	GIZMO_PAINT },
	{ ICON_CORNERS,		"Corners",	GIZMO_VERTEX },
	{ ICON_CLIP,		"Cut",		GIZMO_CLIP },
	{ ICON_TOP_VIEW,	"Top view",	-1 },
};
static const int NUM_WHEEL_ITEMS = sizeof( wheelItems ) / sizeof( wheelItems[0] );

static bool		wheelOpen = false;
static ImVec2	wheelCenter;

// returns true while the wheel has the mouse
static bool HandleToolWheel( bool hovered )
{
	ImGuiIO& io = ImGui::GetIO();
	const bool spaceDown = ImGui::IsKeyDown( ImGuiKey_Space ) && !io.WantTextInput;
	if( !wheelOpen )
	{
		if( spaceDown && hovered && !ImGui::IsMouseDown( ImGuiMouseButton_Right ) && !ImGui::IsMouseDown( ImGuiMouseButton_Left ) && drawStage == DRAW_NONE )
		{
			wheelOpen = true;
			wheelCenter = io.MousePos;
		}
		else
		{
			return false;
		}
	}

	const float s = UI::Scale();
	const float outer = 118.0f * s;
	const float inner = 38.0f * s;
	const ImVec2 d( io.MousePos.x - wheelCenter.x, io.MousePos.y - wheelCenter.y );
	const float dist = sqrtf( d.x * d.x + d.y * d.y );
	int hot = -1;
	const float step = idMath::TWO_PI / NUM_WHEEL_ITEMS;
	if( dist > inner * 0.8f )
	{
		// item 0 is straight up; going clockwise
		float angle = atan2f( d.x, -d.y );
		if( angle < 0.0f )
		{
			angle += idMath::TWO_PI;
		}
		hot = ( int )( ( angle + step * 0.5f ) / step ) % NUM_WHEEL_ITEMS;
	}

	ImDrawList* draw = ImGui::GetForegroundDrawList();
	draw->AddCircleFilled( wheelCenter, outer + 6.0f * s, IM_COL32( 20, 21, 24, 225 ), 64 );
	draw->AddCircle( wheelCenter, outer + 6.0f * s, ImGui::GetColorU32( ImGuiCol_Border ), 64, 1.0f );
	for( int i = 0; i < NUM_WHEEL_ITEMS; i++ )
	{
		const wheelItem_t& item = wheelItems[i];
		const float a = i * step;
		const ImVec2 dir( sinf( a ), -cosf( a ) );
		const float r = ( outer + inner ) * 0.5f;
		const ImVec2 c( wheelCenter.x + dir.x * r, wheelCenter.y + dir.y * r );
		const bool current = ( item.tool >= 0 && gizmoMode == item.tool ) || ( item.tool < 0 && planView );
		if( i == hot )
		{
			// the slice under the mouse
			// ImGui measures angles from +X; ours start straight up
			const float a0 = a - idMath::HALF_PI - step * 0.5f + 0.03f;
			const float a1 = a - idMath::HALF_PI + step * 0.5f - 0.03f;
			draw->PathArcTo( wheelCenter, ( outer + inner ) * 0.5f, a0, a1, 24 );
			draw->PathStroke( UI::AccentU32( 0.85f ), 0, outer - inner );
		}
		ImFont* iconFont = UI::Font( UI::FONT_ICON );
		const ImVec2 iconSize = iconFont->CalcTextSizeA( iconFont->FontSize, FLT_MAX, 0.0f, item.icon );
		const ImU32 color = ( i == hot ) ? IM_COL32( 20, 20, 22, 255 ) : current ? UI::AccentU32() : IM_COL32( 230, 231, 234, 255 );
		draw->AddText( iconFont, iconFont->FontSize, ImVec2( c.x - iconSize.x * 0.5f, c.y - iconSize.y * 0.5f - 7.0f * s ), color, item.icon );
		ImFont* smallFont = UI::Font( UI::FONT_SMALL );
		const ImVec2 labelSize = smallFont->CalcTextSizeA( smallFont->FontSize, FLT_MAX, 0.0f, item.label );
		draw->AddText( smallFont, smallFont->FontSize, ImVec2( c.x - labelSize.x * 0.5f, c.y + iconSize.y * 0.5f - 4.0f * s ), color, item.label );
	}
	draw->AddCircleFilled( wheelCenter, inner - 4.0f * s, IM_COL32( 32, 33, 37, 255 ), 32 );
	ImFont* smallFont = UI::Font( UI::FONT_SMALL );
	const char* centerText = hot >= 0 ? wheelItems[hot].label : "Space";
	const ImVec2 centerSize = smallFont->CalcTextSizeA( smallFont->FontSize, FLT_MAX, 0.0f, centerText );
	draw->AddText( smallFont, smallFont->FontSize, ImVec2( wheelCenter.x - centerSize.x * 0.5f, wheelCenter.y - centerSize.y * 0.5f ), IM_COL32( 200, 201, 205, 255 ), centerText );

	if( !spaceDown || ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
	{
		if( hot >= 0 )
		{
			if( wheelItems[hot].tool < 0 )
			{
				TogglePlanView();
			}
			else
			{
				SetTool( ( gizmoMode_t )wheelItems[hot].tool );
			}
		}
		wheelOpen = false;
	}
	return true;
}

// a shape or thing let go of after dragging it out of the dock
static void HandleShapeDrag()
{
	if( draggingShape < 0 && draggingThing.IsEmpty() )
	{
		return;
	}
	ImGuiIO& io = ImGui::GetIO();
	idStr label;
	if( draggingShape >= 0 )
	{
		label = ShapeName( ( shapeType_t )draggingShape );
	}
	else if( draggingThing.Cmpn( "light:", 6 ) == 0 )
	{
		label = "Light";
	}
	else
	{
		const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->FindType( DECL_ENTITYDEF, draggingThing, false ) );
		label = def != NULL ? ThingLabel( def ) : draggingThing.c_str();
	}
	ImDrawList* fg = ImGui::GetForegroundDrawList();
	const ImVec2 textSize = ImGui::CalcTextSize( label.c_str() );
	const ImVec2 at( io.MousePos.x + 16.0f, io.MousePos.y + 12.0f );
	fg->AddRectFilled( ImVec2( at.x - 8.0f, at.y - 4.0f ), ImVec2( at.x + textSize.x + 8.0f, at.y + textSize.y + 4.0f ), UI::AccentU32( 0.92f ), 6.0f );
	fg->AddText( at, IM_COL32( 20, 20, 22, 255 ), label.c_str() );

	if( ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
	{
		return;
	}

	// dropped: over the view (not over a panel) places it there
	const bool overPanel = ImGui::IsWindowHovered( ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem );
	if( !overPanel )
	{
		idVec3 point;
		bool havePoint = false;
		if( planView )
		{
			ImVec2 mins, maxs;
			if( PlanViewRect( mins, maxs ) )
			{
				const idVec2 p = ScreenToPlan( io.MousePos, mins, maxs );
				point = SnapToGrid( idVec3( p.x, p.y, view.gridZ ) );
				havePoint = true;
				if( draggingShape >= 0 )
				{
					const idVec3 size = ShapeDefaultSize( ( shapeType_t )draggingShape );
					const idVec3 corner = SnapToGrid( idVec3( p.x - size.x * 0.5f, p.y - size.y * 0.5f, view.gridZ ) );
					MakeAndAddShape( ( shapeType_t )draggingShape, idBounds( corner, corner + size ) );
				}
			}
		}
		else
		{
			if( draggingShape >= 0 )
			{
				PlaceShapeAt( ( shapeType_t )draggingShape, io.MousePos.x, io.MousePos.y );
			}
			point = PlacementPointAt( io.MousePos.x, io.MousePos.y );
			havePoint = true;
		}

		if( havePoint && draggingShape < 0 && document.IsOpen() )
		{
			if( draggingThing.Cmpn( "light:", 6 ) == 0 )
			{
				const int preset = idMath::ClampInt( 0, ( int )( sizeof( lightPresets ) / sizeof( lightPresets[0] ) ) - 1, atoi( draggingThing.c_str() + 6 ) );
				PlaceLight( lightPresets[preset], point );
			}
			else
			{
				document.AddEntity( draggingThing, point );
			}
		}
	}
	draggingShape = -1;
	draggingThing.Clear();
}

/*
-------------------------------------------------------------------------------
	everything over the view
-------------------------------------------------------------------------------
*/

static void DrawViewOverlay()
{
	ImGuiDockNode* central = ( dockspaceId != 0 ) ? ImGui::DockBuilderGetCentralNode( dockspaceId ) : NULL;
	if( central == NULL || !document.IsOpen() )
	{
		return;
	}
	DrawViewBar( central );
	DrawToolDock( central );
	DrawContextBar();
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
		if( drawStage != DRAW_NONE )
		{
			CancelBrushDraw();
		}
		else if( gizmoMode == GIZMO_CLIP && clipPoints.Num() > 0 )
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
	else if( !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_B, false ) )
	{
		SetTool( GIZMO_BRUSH );
	}
	else if( !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_P, false ) )
	{
		SetTool( GIZMO_PAINT );
	}
	else if( !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_I, false ) )
	{
		SetTool( GIZMO_PAINT );
		paintPicking = true;
	}
	else if( !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_T, false ) )
	{
		TogglePlanView();
	}
	else if( ImGui::IsKeyPressed( ImGuiKey_Comma, false ) )
	{
		TurnSelection( 90.0f );
	}
	else if( ImGui::IsKeyPressed( ImGuiKey_Period, false ) )
	{
		TurnSelection( -90.0f );
	}
	else if( !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_C, false ) )
	{
		SetTool( GIZMO_CLIP );
	}
	else if( !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_V, false ) )
	{
		SetTool( GIZMO_VERTEX );
	}
	else if( !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_W, false ) )
	{
		SetTool( GIZMO_MOVE );
	}
	else if( !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_E, false ) )
	{
		SetTool( GIZMO_ROTATE );
	}
	else if( !io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_R, false ) )
	{
		SetTool( GIZMO_RESIZE );
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
	else if( !io.KeyCtrl )
	{
		// 1-5 switch the dock's tabs
		for( int i = 0; i < MODE_COUNT; i++ )
		{
			if( ImGui::IsKeyPressed( ( ImGuiKey )( ImGuiKey_1 + i ), false ) )
			{
				SetMode( ( editMode_t )i );
			}
		}
	}
}

// what Alt + left drag orbits: the selection, else what's in the middle of the view
static idVec3 OrbitPivot()
{
	idBounds bounds;
	if( document.SelectionBounds( bounds ) )
	{
		return bounds.GetCenter();
	}
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	idVec3 point, normal;
	if( view.Trace( document, size.x * 0.5f, size.y * 0.5f, point, normal ) )
	{
		return point;
	}
	return view.camera.origin + view.camera.Axis()[0] * 512.0f;
}

static void HandleViewport()
{
	ImGuiIO& io = ImGui::GetIO();
	const bool hovered = !io.WantCaptureMouse;
	view.gridSize = GridSize();

	const bool dragging = draggingShape >= 0 || !draggingThing.IsEmpty();
	const bool wheel = HandleToolWheel( hovered && !dragging );

	if( planView )
	{
		EndGizmoDrag();
		EndCornerDrag( true );
		DrawPlanView( hovered && !dragging && !wheel );
		DrawViewOverlay();
		HandleShapeDrag();
		if( !wheel )
		{
			HandleShortcuts();
		}
		return;
	}

	if( hovered && !wheel && io.KeyAlt && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
	{
		view.orbitPivot = OrbitPivot();
	}
	const bool navigating = view.UpdateCamera( hovered && !dragging && !wheel );

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
	else if( gizmoMode == GIZMO_BRUSH )
	{
		EndGizmoDrag();
		EndCornerDrag( true );
		DrawBrushToolPreview();
	}
	else if( gizmoMode == GIZMO_PAINT )
	{
		EndGizmoDrag();
		EndCornerDrag( true );
	}
	else
	{
		EndCornerDrag( true );
		DrawGizmo();
	}
	DrawViewOverlay();
	HandleShapeDrag();

	if( wheel )
	{
		return;
	}
	if( navigating || io.KeyAlt || dragging )
	{
		HandleShortcuts();
		return;
	}

	if( gizmoMode == GIZMO_PAINT )
	{
		if( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
		{
			PaintClick( io.MousePos.x, io.MousePos.y, io.KeyCtrl );
		}
		HandleShortcuts();
		return;
	}

	if( gizmoMode == GIZMO_BRUSH )
	{
		HandleBrushTool( hovered );
		HandleShortcuts();
		return;
	}

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

	ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 10.0f * UI::Scale(), 8.0f * UI::Scale() ) );
	const bool barOpen = ImGui::BeginMainMenuBar();
	ImGui::PopStyleVar();
	if( !barOpen )
	{
		return;
	}

	// wordmark
	ImGui::PushFont( UI::Font( UI::FONT_BOLD ) );
	ImGui::TextColored( UI::Accent(), "Grizz" );
	ImGui::SameLine( 0.0f, 0.0f );
	ImGui::TextUnformatted( "Tech4" );
	ImGui::PopFont();
	ImGui::SameLine( 0.0f, 16.0f * UI::Scale() );

	if( ImGui::BeginMenu( "File", !playing ) )
	{
		if( ImGui::MenuItem( ICON_NEW "  New Map..." ) )
		{
			openNewMapPopup = true;
		}
		if( ImGui::BeginMenu( ICON_OPEN "  Open Map" ) )
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
		if( ImGui::MenuItem( ICON_SAVE "  Save", "Ctrl+S", false, document.IsOpen() ) )
		{
			if( document.Save() )
			{
				Status( "Saved" );
			}
			mapListDirty = true;
		}
		if( ImGui::MenuItem( "      Save As...", NULL, false, document.IsOpen() ) )
		{
			openSaveAsPopup = true;
		}
		ImGui::Separator();
		if( ImGui::MenuItem( ICON_PROJECT "  New Project..." ) )
		{
			openNewProjectPopup = true;
		}
		if( ImGui::MenuItem( ICON_OPEN "  Open Project..." ) )
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
		if( ImGui::MenuItem( va( ICON_UNDO "  Undo %s", document.UndoLabel() ), "Ctrl+Z", false, document.CanUndo() ) )
		{
			document.Undo();
		}
		if( ImGui::MenuItem( va( ICON_REDO "  Redo %s", document.RedoLabel() ), "Ctrl+Y", false, document.CanRedo() ) )
		{
			document.Redo();
		}
		ImGui::Separator();
		const bool canEdit = document.CanTransformSelection();
		if( ImGui::MenuItem( ICON_DUPLICATE "  Duplicate", "Ctrl+D", false, canEdit ) )
		{
			document.DuplicateSelection( idVec3( GridSize() * 4.0f, GridSize() * 4.0f, 0.0f ) );
		}
		if( ImGui::MenuItem( ICON_DELETE "  Delete", "Del", false, canEdit ) )
		{
			document.DeleteSelection();
		}
		if( ImGui::MenuItem( ICON_FOCUS "  Frame selection", "F", false, document.SelectionIsValid() ) )
		{
			FocusSelection();
		}
		if( ImGui::MenuItem( "      Select none", "Esc", false, !document.selection.IsEmpty() ) )
		{
			document.selection.Clear();
		}
		ImGui::Separator();
		for( int t = 0; t < NUM_TOOLS; t++ )
		{
			if( ImGui::MenuItem( va( "%s  %s", toolInfo[t].icon, toolInfo[t].label ), toolInfo[t].key, gizmoMode == toolInfo[t].mode ) )
			{
				SetTool( toolInfo[t].mode );
			}
		}
		if( ImGui::BeginMenu( "      Tool wheel" ) )
		{
			Hint( "Hold Space over the view for a ring of tools around the mouse; let go over one to pick it." );
			ImGui::EndMenu();
		}
		ImGui::Separator();
		if( ImGui::MenuItem( ICON_TURN_LEFT "  Turn left 90", ",", false, document.CanTransformSelection() ) )
		{
			TurnSelection( 90.0f );
		}
		if( ImGui::MenuItem( ICON_TURN_RIGHT "  Turn right 90", ".", false, document.CanTransformSelection() ) )
		{
			TurnSelection( -90.0f );
		}
		{
			idList<Selection> brushes;
			document.SelectedBrushes( brushes );
			if( ImGui::MenuItem( ICON_CARVE "  Carve selected brushes", NULL, false, brushes.Num() > 0 ) )
			{
				document.SubtractSelection();
			}
		}
		ImGui::MenuItem( ICON_LOCK "  Texture lock", NULL, &document.textureLock );
		ImGui::Separator();
		bool snap = editor_snap.GetBool();
		if( ImGui::MenuItem( ICON_SNAP "  Snap to grid", NULL, &snap ) )
		{
			editor_snap.SetBool( snap );
		}
		if( ImGui::BeginMenu( ICON_GRID "  Grid size" ) )
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
		ImGui::MenuItem( ICON_WIREFRAME "  Brush outlines", NULL, &view.wireframe );
		ImGui::MenuItem( ICON_GRID "  Floor grid", NULL, &view.showGrid );
		if( ImGui::MenuItem( ICON_TOP_VIEW "  Top view", "T", planView ) )
		{
			TogglePlanView();
		}
		ImGui::Separator();
		if( ImGui::MenuItem( "      Reset layout" ) )
		{
			showOutliner = showInspector = showContentBrowser = showProblems = true;
			requestLayoutReset = true;
		}
		ImGui::EndMenu();
	}

	if( ImGui::BeginMenu( "Help" ) )
	{
		if( ImGui::MenuItem( ICON_LESSONS "  Getting started" ) )
		{
			showWelcome = true;
		}
		if( ImGui::MenuItem( ICON_KEYBOARD "  About GrizzTech4 and controls" ) )
		{
			showAbout = true;
		}
		ImGui::EndMenu();
	}

	// Play / Stop sits in the middle of the bar, like other editors
	const float buttonWidth = 104.0f * UI::Scale();
	ImGui::SetCursorPosX( ( ImGui::GetWindowWidth() - buttonWidth ) * 0.5f );
	ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 20.0f * UI::Scale() );
	ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 10.0f * UI::Scale(), 4.0f * UI::Scale() ) );
	const float barHeight = ImGui::GetWindowHeight();
	ImGui::SetCursorPosY( ( barHeight - ( ImGui::GetFontSize() + 8.0f * UI::Scale() ) ) * 0.5f );
	if( playing )
	{
		if( UI::ColorButton( ICON_STOP "  Stop", UI::Bad(), ImVec2( buttonWidth, 0 ) ) )
		{
			Stop();
		}
		UI::Tip( "Back to the editor (Esc or F10)" );
	}
	else
	{
		ImGui::BeginDisabled( !document.IsOpen() );
		if( UI::ColorButton( ICON_PLAY "  Play", UI::Good(), ImVec2( buttonWidth, 0 ) ) )
		{
			PlayDocument();
		}
		ImGui::EndDisabled();
		UI::Tip( "Save and play the level. Esc brings you back." );
	}
	ImGui::PopStyleVar( 2 );

	// right side: status, the level and the project
	const char* game = cvarSystem->GetCVarString( "fs_game" );
	const idStr level = document.IsOpen() ? va( ICON_MAP_FILE "  %s%s", document.Name(), document.IsDirty() ? "  \xe2\x80\xa2" : "" ) : "No level open";
	const idStr project = ( game != NULL && game[0] != '\0' ) ? va( ICON_PROJECT "  %s", game ) : ICON_PROJECT "  Engine content";
	const bool showStatus = statusMessage.Length() > 0 && Sys_Milliseconds() - statusTime < 4000;
	const float gap = 24.0f * UI::Scale();
	float rightWidth = ImGui::CalcTextSize( level.c_str() ).x + gap + ImGui::CalcTextSize( project.c_str() ).x + 16.0f * UI::Scale();
	if( showStatus )
	{
		rightWidth += ImGui::CalcTextSize( statusMessage.c_str() ).x + gap;
	}
	if( !playing )
	{
		rightWidth += ImGui::GetFrameHeight() * 2.0f + gap;
	}
	ImGui::SetCursorPosX( ImGui::GetWindowWidth() - rightWidth );
	if( showStatus )
	{
		ImGui::TextColored( UI::Accent(), "%s", statusMessage.c_str() );
		ImGui::SameLine( 0.0f, gap );
	}
	if( !playing )
	{
		if( UI::IconButton( "undo", ICON_UNDO, va( "Undo %s (Ctrl+Z)", document.UndoLabel() ), false, document.CanUndo() ) )
		{
			document.Undo();
		}
		ImGui::SameLine( 0.0f, 0.0f );
		if( UI::IconButton( "redo", ICON_REDO, va( "Redo %s (Ctrl+Y)", document.RedoLabel() ), false, document.CanRedo() ) )
		{
			document.Redo();
		}
		ImGui::SameLine( 0.0f, gap );
	}
	ImGui::TextUnformatted( level.c_str() );
	UI::Tip( document.IsDirty() ? "The open level (the dot means it has unsaved changes; Ctrl+S saves)" : "The open level" );
	ImGui::SameLine( 0.0f, gap );
	ImGui::TextDisabled( "%s", project.c_str() );
	UI::Tip( "The project. File > Open Project switches." );

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
	DrawWelcome();
	DrawNewProjectDialog();
	DrawOpenProjectDialog();
	DrawMapNameDialog( "Save Map As", openSaveAsPopup, false );
	DrawMapNameDialog( "New Map", openNewMapPopup, true );
}

} // namespace GrizzEditor

#endif // GRIZZ_EDITOR
