/*
===========================================================================

Daedalus GPL Source Code
Copyright (C) 2026 Daedalus contributors

This file is part of Daedalus, which is based on RBDOOM-3-BFG and the
Doom 3 BFG Edition GPL Source Code.

Daedalus is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Daedalus is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daedalus.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

#include "precompiled.h"
#pragma hdrstop

#if defined( DAEDALUS_EDITOR )

#include "EditorShell.h"
#include "EditorDocument.h"
#include "EditorPrefabs.h"
#include "EditorImport.h"
#include "EditorProblems.h"
#include "EditorMaterials.h"
#include "EditorHelp.h"
#include "EditorView.h"
#include "EditorShapes.h"
#include "EditorBrushOps.h"
#include "EditorTheme.h"
#include "framework/GameLibrary.h"

#include "../../imgui/BFGimgui.h"
#include "../../libs/imgui/imgui_internal.h"	// DockBuilder API
#include "../../libs/imgui/ImGuizmo.h"
#include "renderer/RenderCommon.h"

idCVar editor( "editor", "1", CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT, "start in the Daedalus editor (0 = run as the game)" );
idCVar editor_startMap( "editor_startMap", "testmaps/test_room", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT, "map the editor opens at startup" );
idCVar editor_gridSize( "editor_gridSize", "8", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_NOCHEAT, "editor grid size in units" );
idCVar editor_showWelcome( "editor_showWelcome", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT, "show the Getting started window when the editor opens" );
idCVar editor_snap( "editor_snap", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT, "snap moves to the editor grid" );
idCVar editor_playFrom( "editor_playFrom", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_NOCHEAT, "where Play puts you: 0 = the player start, 1 = where the editor camera is", 0, 1 );

extern idCVar r_materialPath;

namespace DaedalusEditor
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
static const char* WINDOW_MATERIAL			= ICON_MATERIAL "  Material###Material";
static const char* WINDOW_GAMES			= ICON_PLAY "  Your games###Your games";

// pieces (see the Pieces section)
static void SelectHit( const Selection& hit, bool toggle );
static void SyncPiecesOnOpen();
static bool IsEditingPiece();
static int PieceEditingAnchor();
enum pieceEditEnd_t
{
	PIECE_SAVE_ALL,		// into the piece: every copy changes
	PIECE_SAVE_COPY,	// into this copy only, as its own changes
	PIECE_CANCEL
};
static void FinishPieceEdit( pieceEditEnd_t end );
static bool DrawPieceInspector();
static void DrawPartCard();
static void PlacePiece( const char* name, const idVec3& point );
static void PlaceModel( const char* name, const idVec3& point );
static void DrawModelCard( idMapEntity* ent );
static void DrawContentModels();
static void DrawModelTiles();
static int CountModelTiles();
static void DrawImportDialog();
static void ModelsUpdate();
static void EditorImport_f( const idCmdArgs& args );
static void EditorImportSize_f( const idCmdArgs& args );
static void EditorImportTurn_f( const idCmdArgs& args );
static void EditorReimport_f( const idCmdArgs& args );
static void EditorPlaceModel_f( const idCmdArgs& args );
static void EditorListImports_f( const idCmdArgs& args );
static void EditorDropFile_f( const idCmdArgs& args );

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

// Play from the camera: once the level has loaded, the player is moved to where the camera was
static bool			playTeleportPending = false;
static bool			playStarting = false;		// Play was pressed and the level hasn't started loading yet
static idVec3		playTeleportEye;
static idAngles		playTeleportAngles;

/*
===============================================================================

	Build

	Compiling a level (rbdmap) runs as a separate program, so the editor keeps
	working while it does; the menu bar shows how far it has got. Play waits for
	it, and brush changes made while Play is paused rebuild before the game
	carries on. If rbdmap can't be found (or editor_buildInProcess is set) the
	level is compiled inside the editor instead, which freezes it meanwhile.

===============================================================================
*/

enum buildThen_t
{
	BUILD_THEN_NOTHING,
	BUILD_THEN_PLAY,		// start playing it
	BUILD_THEN_RELOAD		// Play is paused: reload the level around the player
};

idCVar editor_buildInProcess( "editor_buildInProcess", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_ARCHIVE | CVAR_NOCHEAT, "compile levels inside the editor (it freezes meanwhile) instead of with the rbdmap program" );

static void*		buildProcess = NULL;
static idStr		buildMap;
static idStr		buildLogPath;			// OS path of rbdmap's output
static idStr		buildLog;				// all of it, for the log window
static idStr		buildLinePart;			// a line still being written
static int			buildStartTime = 0;
static int			buildStageTime = 0;
static float		buildStageFrom = 0.0f;
static float		buildStageTo = 0.0f;
static idStr		buildStage;
static buildThen_t	buildThen = BUILD_THEN_NOTHING;
static bool			buildHasResult = false;	// a build has finished since the editor started
static bool			buildFailed = false;
static idStr		buildResultMap;
static int			buildSeconds = 0;
static idStrList	buildProblems;			// warnings and errors of the last build
static idList<bool>	buildProblemIsError;	// which of them are errors
static ID_TIME_T	buildMapTime = FILE_NOT_FOUND_TIMESTAMP;	// when the level being built was saved
static bool			showBuildLog = false;
static bool			focusProblems = false;		// bring Problems to the front (a build failed)
static bool			buildLeaked = false;		// the last build found a leak

static void StartLevel( const char* mapName );
static void ReloadAtPlayer();

static bool BuildRunning()
{
	return buildProcess != NULL;
}

// rbdmap sits next to the editor in a package, and in the build tree in a development build
static bool FindCompiler( idStr& path )
{
#ifdef _WIN32
	const char* name = "rbdmap.exe";
	const char* places[] = { "", "tools/compilers/Release/", "../tools/compilers/Release/", "tools/compilers/" };
#else
	const char* name = "rbdmap";
	const char* places[] = { "", "tools/compilers/" };
#endif
	idStr dir = Sys_EXEPath();
	dir.BackSlashesToSlashes();
	dir.StripFilename();
	for( int i = 0; i < sizeof( places ) / sizeof( places[0] ); i++ )
	{
		idStr candidate = va( "%s/%s%s", dir.c_str(), places[i], name );
		FILE* f = fopen( candidate.c_str(), "rb" );
		if( f != NULL )
		{
			fclose( f );
#ifdef _WIN32
			candidate.SlashesToBackSlashes();
#endif
			path = candidate;
			return true;
		}
	}
	return false;
}

// the level has never been compiled, or was saved since
static bool LevelNeedsBuild( const char* mapName )
{
	ID_TIME_T mapTime = FILE_NOT_FOUND_TIMESTAMP;
	ID_TIME_T procTime = FILE_NOT_FOUND_TIMESTAMP;
	fileSystem->ReadFile( va( "maps/%s.map", mapName ), NULL, &mapTime );
	fileSystem->ReadFile( va( "maps/%s.proc", mapName ), NULL, &procTime );
	return procTime == FILE_NOT_FOUND_TIMESTAMP || ( mapTime != FILE_NOT_FOUND_TIMESTAMP && mapTime > procTime );
}

// the level was saved again after the running build read it
static bool LevelNeedsBuildSince()
{
	ID_TIME_T mapTime = FILE_NOT_FOUND_TIMESTAMP;
	fileSystem->ReadFile( va( "maps/%s.map", buildMap.c_str() ), NULL, &mapTime );
	return mapTime != buildMapTime;
}

static void BuildSetStage( const char* label, float from, float to )
{
	buildStage = label;
	buildStageFrom = Max( from, buildStageFrom );
	buildStageTo = Max( to, buildStageFrom );
	buildStageTime = Sys_Milliseconds();
}

// how far the build has got: the stage it's in, creeping on towards the next one
static float BuildProgress()
{
	const float t = ( Sys_Milliseconds() - buildStageTime ) * 0.001f;
	return buildStageFrom + ( buildStageTo - buildStageFrom ) * ( 1.0f - idMath::Exp( -t / 6.0f ) );
}

static void BuildReadLine( const char* line )
{
	struct stage_t
	{
		const char* marker;
		const char* label;
		float		from, to;
	};
	static const stage_t stages[] =
	{
		{ "Initializing File System",	"Starting",					0.02f, 0.05f },
		{ "Initializing Decls",			"Loading materials",		0.05f, 0.12f },
		{ "--- LoadDMapFile ---",		"Reading the level",		0.12f, 0.35f },
		{ "--- FloodEntities ---",		"Checking it's sealed",		0.35f, 0.42f },
		{ "--- FillOutside ---",		"Cutting away the outside",	0.42f, 0.50f },
		{ "--- FloodAreas ---",			"Splitting it into areas",	0.50f, 0.75f },
		{ "----- WriteOutputFile -----", "Writing the level",		0.75f, 0.82f },
		{ "collision map",				"Building collision",		0.82f, 0.92f },
		{ "compiling AAS",				"Building AI navigation",	0.92f, 0.98f },
	};
	for( int i = 0; i < sizeof( stages ) / sizeof( stages[0] ); i++ )
	{
		if( strstr( line, stages[i].marker ) != NULL )
		{
			BuildSetStage( stages[i].label, stages[i].from, stages[i].to );
			break;
		}
	}

	const bool warning = idStr::Cmpn( line, "WARNING: ", 9 ) == 0;
	const bool error = idStr::Cmpn( line, "ERROR: ", 7 ) == 0 || idStr::Cmpn( line, "FATAL ERROR: ", 13 ) == 0;
	if( !warning && !error )
	{
		return;
	}
	idStr text = strchr( line, ':' ) + 1;
	text.StripLeading( ' ' );
	text.StripTrailingWhitespace();
	if( strstr( line, "Couldn't load image" ) != NULL )
	{
		return;		// rbdmap has no renderer; images are the game's business
	}
	if( strstr( line, "leaked" ) != NULL )
	{
		buildLeaked = true;
		text = "The level leaks: there's a gap somewhere between the inside and the void outside, so it can't be built.";
	}
	buildProblems.Append( text );
	buildProblemIsError.Append( error || strstr( line, "leaked" ) != NULL );
}

// pick up what rbdmap has printed since last time
static void BuildReadLog()
{
	char data[4096];
	int got;
	while( buildProcess != NULL && ( got = Sys_ProcessReadOutput( buildProcess, data, sizeof( data ) - 1 ) ) > 0 )
	{
		data[got] = '\0';
		if( buildLog.Length() < 512 * 1024 )
		{
			buildLog += data;
		}
		buildLinePart += data;
		int start = 0;
		for( int i = 0; i < buildLinePart.Length(); i++ )
		{
			if( buildLinePart[i] == '\n' )
			{
				idStr line = buildLinePart.Mid( start, i - start );
				line.StripTrailing( '\r' );
				BuildReadLine( line.c_str() );
				start = i + 1;
			}
		}
		buildLinePart = buildLinePart.Right( buildLinePart.Length() - start );
	}
}

static void CancelBuild()
{
	if( buildProcess != NULL )
	{
		Sys_ProcessKill( buildProcess );
		Sys_ProcessFree( buildProcess );
		buildProcess = NULL;
		common->Printf( "Editor: build of %s cancelled\n", buildMap.c_str() );
	}
	if( buildThen == BUILD_THEN_PLAY )
	{
		playStarting = false;
	}
	buildThen = BUILD_THEN_NOTHING;
}

// compile a level in the background; 'then' says what happens once it has built
static void StartBuild( const char* mapName, buildThen_t then )
{
	if( BuildRunning() )
	{
		if( buildMap.Icmp( mapName ) == 0 && !LevelNeedsBuildSince() )
		{
			buildThen = then;	// already on it
			return;
		}
		CancelBuild();
	}

	buildMap = mapName;
	buildThen = then;
	buildProblems.Clear();
	buildProblemIsError.Clear();
	buildLeaked = false;
	view.leakPath.Clear();
	buildLog.Clear();
	buildLinePart.Clear();
	buildStage = "Starting";
	buildStageFrom = buildStageTo = 0.0f;
	BuildSetStage( "Starting", 0.0f, 0.05f );
	buildStartTime = Sys_Milliseconds();
	fileSystem->ReadFile( va( "maps/%s.map", mapName ), NULL, &buildMapTime );

	idStr compiler;
	if( editor_buildInProcess.GetBool() || !FindCompiler( compiler ) )
	{
		// the old way: dmap runs inside the editor, which waits for it
		common->Printf( "Editor: compiling %s inside the editor\n", mapName );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "dmap %s\n", mapName ) );
		buildThen = BUILD_THEN_NOTHING;
		if( then == BUILD_THEN_PLAY )
		{
			StartLevel( mapName );
		}
		else if( then == BUILD_THEN_RELOAD )
		{
			ReloadAtPlayer();
		}
		return;
	}

	// the log goes next to the project's saved files
	idStr logName = mapName;
	logName.Replace( "/", "_" );
	logName = va( "build/%s.log", logName.c_str() );
	fileSystem->WriteFile( logName.c_str(), "", 0, "fs_savepath" );	// creates the folder
	buildLogPath = fileSystem->RelativePathToOSPath( logName.c_str(), "fs_savepath" );

	idStrList args;
	args.Append( "-nogui" );
	const char* cvars[] = { "fs_basepath", "fs_savepath", "fs_game", "fs_game_base" };
	for( int i = 0; i < sizeof( cvars ) / sizeof( cvars[0] ); i++ )
	{
		const char* value = cvarSystem->GetCVarString( cvars[i] );
		if( value != NULL && value[0] != '\0' )
		{
			args.Append( "+set" );
			args.Append( cvars[i] );
			args.Append( value );
		}
	}
	args.Append( mapName );
	idList<const char*> argv;
	for( int i = 0; i < args.Num(); i++ )
	{
		argv.Append( args[i].c_str() );
	}

	common->Printf( "Editor: building %s with %s\n", mapName, compiler.c_str() );
	buildProcess = Sys_ProcessStart( compiler.c_str(), argv.Ptr(), argv.Num(), cvarSystem->GetCVarString( "fs_basepath" ), buildLogPath.c_str() );
	if( buildProcess == NULL )
	{
		// couldn't start it: fall back to building inside the editor
		editor_buildInProcess.SetBool( true );
		StartBuild( mapName, then );
		editor_buildInProcess.SetBool( false );
	}
}

static void StartLevel( const char* mapName )
{
	// a held F5 repeats: start loading once
	static int lastStart = -100000;
	if( Sys_Milliseconds() - lastStart < 3000 && !IsPlaying() )
	{
		return;
	}
	lastStart = Sys_Milliseconds();
	common->Printf( "Editor: playing %s (Esc pauses to edit, F10 stops)\n", mapName );
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "map %s\n", mapName ) );
}

// Problems (defined near the end, after everything they use)
static void			RefreshProblems( bool force );
static int			EntityProblemSeverity( int entity );			// worst warning or error, -1 for none
static int			WorldProblemSeverity( int primitive );
static void			RowTypeProblem( const char* label, const char* text, int severity, int entity, int primitive = -1 );
static int			LevelErrorCount();								// errors Play would run into
static void			RequestPlayWithErrors( bool fromCamera );
static void			DrawSelectionProblems();
static void			DrawProblemMarkers( const ImGuiDockNode* central );
static void			DrawLiveBanner( const ImGuiDockNode* central );
static void			DrawLiveSelection();
static bool			playWithErrors = false;		// Play anyway: skip the question once
static void			RegisterProblemCommands();
// Materials (defined near the end)
static bool			showMaterialPanel = false;
static bool			showGames = false;
static bool			gamesFocus = false;
static void			OpenMaterialPanel( const char* declName );
static void			RequestNewMaterial();
static bool			DrawMaterialThumb( const char* declName, float size, bool selected = false );
static void			MaterialsUpdate();
static void			MaterialFilesRestored();
static void			RegisterMaterialCommands();
static void			RegisterGameCommands();
static void			OpenGames();
static bool			MaterialListed( const char* declName );

// build the level when it needs it, then play it
static void Play( const char* mapName )
{
	if( mapName == NULL || mapName[0] == '\0' )
	{
		common->Warning( "Play: no map open" );
		return;
	}
	if( LevelNeedsBuild( mapName ) || ( BuildRunning() && buildMap.Icmp( mapName ) == 0 ) )
	{
		StartBuild( mapName, BUILD_THEN_PLAY );
		return;
	}
	StartLevel( mapName );
}


static void PlayDocument( bool fromCamera );

// Play the open level, from where editor_playFrom says (Shift flips it for one go)
static void PlayDocument()
{
	bool fromCamera = editor_playFrom.GetInteger() == 1;
	if( ImGui::GetIO().KeyShift )
	{
		fromCamera = !fromCamera;
	}
	PlayDocument( fromCamera );
}

// Play the open level: save it first so the game sees exactly what's in the editor
static void PlayDocument( bool fromCamera )
{
	if( !playWithErrors && document.IsOpen() && !IsPlaying() && LevelErrorCount() > 0 )
	{
		RequestPlayWithErrors( fromCamera );
		return;
	}
	playWithErrors = false;
	playTeleportPending = fromCamera;
	playStarting = true;
	playTeleportEye = view.camera.origin;
	playTeleportAngles = idAngles( view.camera.pitch, view.camera.yaw, 0.0f );
	if( !document.IsOpen() )
	{
		return;
	}
	if( IsEditingPiece() )
	{
		FinishPieceEdit( PIECE_SAVE_ALL );	// play what's there, piece included
	}
	Help::Event( "play.start" );
	if( document.IsDirty() && !document.Save() )
	{
		Status( "Couldn't save the level, so it can't be played" );
		return;
	}
	mapListDirty = true;
	Play( document.Name() );
}

// File > Build: save and compile the open level, without playing it
static void BuildDocument()
{
	if( !document.IsOpen() )
	{
		return;
	}
	if( document.IsDirty() && !document.Save() )
	{
		Status( "Couldn't save the level, so it can't be built" );
		return;
	}
	mapListDirty = true;
	StartBuild( document.Name(), ( BuildRunning() && buildThen != BUILD_THEN_NOTHING ) ? buildThen : BUILD_THEN_NOTHING );
}

static void Stop()
{
	if( IsPlaying() )
	{
		Help::Event( "play.stop" );
	}
	if( buildThen == BUILD_THEN_RELOAD )
	{
		buildThen = BUILD_THEN_NOTHING;		// the build carries on; nothing to reload into
	}
	if( IsPlaying() )
	{
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
	}
}

/*
===============================================================================

	Live edits

	Esc during Play pauses the game and brings the editor back; the game stays
	loaded underneath. Edits made then go straight into the running game:
	lights change in place, other things are respawned with their new settings,
	things added or removed appear or go. Brush geometry is compiled into the
	level, so changes to it wait for a rebuild (see the Build section).

	The game side of a level is the map as it was when Play started (the editor
	saves it first); after every change the document is compared with that.

===============================================================================
*/

static bool			playPaused = false;
idCVar editor_debugLive( "editor_debugLive", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT, "print what live edits do to the running game" );

static void LiveLog( const char* what, const char* name )
{
	if( editor_debugLive.GetBool() )
	{
		common->Printf( "Live: %s %s\n", what, name );
	}
}

struct liveEntity_t
{
	idStr			name;
	idDict			args;
	unsigned int	geometryHash;		// 0 = no brushes or patches
};
static idList<liveEntity_t>	liveBase;
static idStr		liveWorldKeys;
static int			liveRevision = -1;
static bool			liveGeometryChanged = false;		// brushes changed since Play started: a rebuild is needed
static bool			liveSessionActive = false;

// what each change made while paused did: shown over the view and in the Inspector, summed up on resume
enum liveTag_t
{
	LIVE_IN_GAME,			// already in the running game
	LIVE_NEXT_PLAY,			// saved, but only matters when Play starts again (the player start)
	LIVE_REBUILD			// brushes or level settings: the level is rebuilt and reloaded on resume
};
struct liveChange_t
{
	idStr			name;
	idStr			what;
	liveTag_t		tag;
};
static idList<liveChange_t>	liveChanges;
static bool			liveChangesOpen = false;
static idStr		whenPlayingCommand;		// editorWhenPlaying: run once the game is up

static const char* LiveTagLabel( liveTag_t tag )
{
	switch( tag )
	{
		case LIVE_IN_GAME:
			return "In the game";
		case LIVE_NEXT_PLAY:
			return "Next time you play";
		default:
			return "Rebuilds when you resume";
	}
}

static const char* LiveTagCode( liveTag_t tag )
{
	return tag == LIVE_IN_GAME ? "in_game" : ( tag == LIVE_NEXT_PLAY ? "next_play" : "rebuild" );
}

static const char* LiveTagIcon( liveTag_t tag )
{
	return tag == LIVE_IN_GAME ? ICON_OK : ( tag == LIVE_NEXT_PLAY ? ICON_PLAYER : ICON_BUILD );
}

static ImVec4 LiveTagColor( liveTag_t tag )
{
	return tag == LIVE_IN_GAME ? UI::Good() : ( tag == LIVE_NEXT_PLAY ? UI::Accent() : UI::Warn() );
}

// one thing's latest change; a rebuild, once needed, stays needed
static void LiveNote( const char* name, const char* what, liveTag_t tag )
{
	for( int i = 0; i < liveChanges.Num(); i++ )
	{
		if( liveChanges[i].name.Cmp( name ) == 0 )
		{
			liveChanges[i].what = what;
			liveChanges[i].tag = ( liveTag_t )Max( ( int )liveChanges[i].tag, ( int )tag );
			return;
		}
	}
	liveChange_t& change = liveChanges.Alloc();
	change.name = name;
	change.what = what;
	change.tag = tag;
}

static const liveChange_t* FindLiveChange( const char* name )
{
	for( int i = 0; i < liveChanges.Num(); i++ )
	{
		if( liveChanges[i].name.Cmp( name ) == 0 )
		{
			return &liveChanges[i];
		}
	}
	return NULL;
}

static void LiveCounts( int& inGame, int& nextPlay, int& rebuild )
{
	inGame = nextPlay = rebuild = 0;
	for( int i = 0; i < liveChanges.Num(); i++ )
	{
		( liveChanges[i].tag == LIVE_IN_GAME ? inGame : ( liveChanges[i].tag == LIVE_NEXT_PLAY ? nextPlay : rebuild ) )++;
	}
}

static bool IsPlayerStart( const idDict& args )
{
	return idStr::Icmp( args.GetString( "classname" ), "info_player_start" ) == 0;
}

static unsigned int GeometryHash( const idMapEntity* ent )
{
	unsigned int hash = 0;
	for( int p = 0; p < ent->GetNumPrimitives(); p++ )
	{
		const idMapPrimitive* prim = ent->GetPrimitive( p );
		hash = hash * 31 + 7;
		if( prim->GetType() == idMapPrimitive::TYPE_BRUSH )
		{
			const idMapBrush* brush = static_cast<const idMapBrush*>( prim );
			for( int i = 0; i < brush->GetNumSides(); i++ )
			{
				const idMapBrushSide* side = brush->GetSide( i );
				const idPlane& plane = side->GetPlane();
				for( int k = 0; k < 4; k++ )
				{
					hash = hash * 31 + ( unsigned int )idMath::Ftoi( plane[k] * 64.0f );
				}
				idVec3 texMat[2];
				const_cast<idMapBrushSide*>( side )->GetTextureMatrix( texMat[0], texMat[1] );
				for( int k = 0; k < 2; k++ )
				{
					for( int j = 0; j < 3; j++ )
					{
						hash = hash * 31 + ( unsigned int )idMath::Ftoi( texMat[k][j] * 4096.0f );
					}
				}
				hash = hash * 31 + ( unsigned int )idStr::Hash( side->GetMaterial() );
			}
		}
		else
		{
			const idMapPatch* patch = static_cast<const idMapPatch*>( prim );
			for( int i = 0; i < patch->GetNumVertices(); i++ )
			{
				const idVec3& v = ( *patch )[i].xyz;
				hash = hash * 31 + ( unsigned int )idMath::Ftoi( v.x * 16.0f + v.y * 32.0f + v.z * 64.0f );
			}
			hash = hash * 31 + ( unsigned int )idStr::Hash( patch->GetMaterial() );
		}
	}
	return ent->GetNumPrimitives() > 0 ? ( hash | 1u ) : 0u;
}

static idStr DictText( const idDict& dict )
{
	idStr text;
	for( int i = 0; i < dict.GetNumKeyVals(); i++ )
	{
		const idKeyValue* kv = dict.GetKeyVal( i );
		text += kv->GetKey() + "\x1f" + kv->GetValue() + "\x1e";
	}
	return text;
}

// the game's copy of the level: the document as it was when Play started
static void LiveCaptureBase()
{
	liveBase.Clear();
	liveWorldKeys.Clear();
	liveGeometryChanged = false;
	liveChanges.Clear();
	const idMapFile* map = document.Map();
	if( map == NULL )
	{
		return;
	}
	for( int i = 0; i < map->GetNumEntities(); i++ )
	{
		const idMapEntity* ent = map->GetEntity( i );
		if( i > 0 && IsPrefabAnchor( ent ) )
		{
			continue;	// editor-only: the game never spawns piece anchors
		}
		if( i == 0 )
		{
			liveWorldKeys = DictText( ent->epairs );
		}
		liveEntity_t& live = liveBase.Alloc();
		live.name = i == 0 ? "worldspawn" : ent->epairs.GetString( "name" );
		live.args = ent->epairs;
		live.geometryHash = GeometryHash( ent );
	}
	liveRevision = document.Revision();
}

static int FindLive( const char* name )
{
	for( int i = 0; i < liveBase.Num(); i++ )
	{
		if( liveBase[i].name.Icmp( name ) == 0 )
		{
			return i;
		}
	}
	return -1;
}

// a game entity made again from the editor's settings
static bool LiveRespawn( const char* name, const idDict& args )
{
	idEntity* old = gameEdit->FindEntity( name );
	if( old != NULL )
	{
		gameEdit->EntityDelete( old );
	}
	idEntity* spawned = NULL;
	gameEdit->SpawnEntityDef( args, &spawned );
	LiveLog( spawned != NULL ? "respawned" : "couldn't respawn", name );
	return spawned != NULL;
}

static void LiveUpdateLight( const char* name, const idDict& oldArgs, const idDict& args )
{
	idEntity* ent = gameEdit->FindEntity( name );
	if( ent == NULL )
	{
		LiveRespawn( name, args );
		return;
	}
	// keys that went away are passed as empty, which removes them
	idDict change = args;
	for( int i = 0; i < oldArgs.GetNumKeyVals(); i++ )
	{
		if( args.FindKey( oldArgs.GetKeyVal( i )->GetKey() ) == NULL )
		{
			change.Set( oldArgs.GetKeyVal( i )->GetKey(), "" );
		}
	}
	gameEdit->EntityChangeSpawnArgs( ent, &change );
	gameEdit->EntityUpdateChangeableSpawnArgs( ent, NULL );
	gameEdit->EntityUpdateVisuals( ent );
	if( editor_debugLive.GetBool() )
	{
		idVec3 origin;
		gameEdit->EntityGetOrigin( ent, origin );
		common->Printf( "Live: updated light %s, now at %s\n", name, origin.ToString( 0 ) );
	}
}

// called every frame while paused: pass on what changed since last time
static void LiveSync()
{
	const idMapFile* map = document.Map();
	if( map == NULL || document.Revision() == liveRevision || gameEdit == NULL || game == NULL || !game->IsInGame() )
	{
		return;
	}
	liveRevision = document.Revision();

	idList<bool> seen;
	seen.AssureSize( liveBase.Num(), false );

	for( int i = 0; i < map->GetNumEntities(); i++ )
	{
		const idMapEntity* ent = map->GetEntity( i );
		if( i > 0 && IsPrefabAnchor( ent ) )
		{
			continue;
		}
		const idStr name = i == 0 ? idStr( "worldspawn" ) : idStr( ent->epairs.GetString( "name" ) );
		const unsigned int geometry = GeometryHash( ent );
		const int index = FindLive( name );

		if( index < 0 )
		{
			// new: point things appear at once; brush things need the rebuild
			liveEntity_t& live = liveBase.Alloc();
			seen.Append( true );
			live.name = name;
			live.args = ent->epairs;
			live.geometryHash = geometry;
			if( geometry != 0 )
			{
				liveGeometryChanged = true;
				LiveLog( "new brushes (rebuild needed):", name );
				LiveNote( name, "added (made of brushes)", LIVE_REBUILD );
			}
			else
			{
				idEntity* spawned = NULL;
				gameEdit->SpawnEntityDef( ent->epairs, &spawned );
				LiveLog( "spawned", name );
				if( spawned == NULL )
				{
					// it couldn't be made in the running game: the reload will have it
					liveGeometryChanged = true;
					LiveNote( name, "added, but the running game couldn't make it", LIVE_REBUILD );
				}
				else
				{
					LiveNote( name, "added", IsPlayerStart( ent->epairs ) ? LIVE_NEXT_PLAY : LIVE_IN_GAME );
				}
			}
			continue;
		}

		seen[index] = true;
		liveEntity_t& live = liveBase[index];
		const bool argsChanged = DictText( live.args ) != DictText( ent->epairs );
		if( geometry != live.geometryHash )
		{
			liveGeometryChanged = true;
			LiveLog( "brushes changed (rebuild needed):", name );
			LiveNote( name, i == 0 ? "the level's brushes changed" : "its brushes changed", LIVE_REBUILD );
		}
		else if( argsChanged )
		{
			if( i == 0 )
			{
				// level settings are read when the level loads
				liveGeometryChanged = true;
				LiveNote( name, "level settings changed", LIVE_REBUILD );
			}
			else if( idStr::Icmp( ent->epairs.GetString( "classname" ), "light" ) == 0 && idStr::Icmp( live.args.GetString( "classname" ), "light" ) == 0 )
			{
				LiveUpdateLight( name, live.args, ent->epairs );
				LiveNote( name, "light changed", LIVE_IN_GAME );
			}
			else if( LiveRespawn( name, ent->epairs ) )
			{
				LiveNote( name, IsPlayerStart( ent->epairs ) ? "moved or changed; you start there next time" : "made again with its new settings",
						  IsPlayerStart( ent->epairs ) ? LIVE_NEXT_PLAY : LIVE_IN_GAME );
			}
			else
			{
				liveGeometryChanged = true;
				LiveNote( name, "changed, but the running game couldn't remake it", LIVE_REBUILD );
			}
		}
		live.args = ent->epairs;
		live.geometryHash = geometry;
	}

	// gone from the level: gone from the game
	for( int i = liveBase.Num() - 1; i >= 0; i-- )
	{
		if( seen[i] )
		{
			continue;
		}
		if( liveBase[i].geometryHash != 0 )
		{
			liveGeometryChanged = true;
		}
		LiveNote( liveBase[i].name, "removed", liveBase[i].geometryHash != 0 ? LIVE_REBUILD : ( IsPlayerStart( liveBase[i].args ) ? LIVE_NEXT_PLAY : LIVE_IN_GAME ) );
		idEntity* ent = gameEdit->FindEntity( liveBase[i].name );
		if( ent != NULL )
		{
			gameEdit->EntityDelete( ent );
		}
		LiveLog( "removed", liveBase[i].name );
		liveBase.RemoveIndex( i );
	}
}

static void PausePlay()
{
	if( !IsPlaying() || playPaused || game == NULL || !game->IsInGame() )
	{
		return;
	}
	playPaused = true;
	Help::Event( "play.pause" );

	// the editor camera takes over where the player was looking
	if( gameEdit->PlayerIsValid() )
	{
		idVec3 eye;
		idAngles angles;
		gameEdit->PlayerGetEyePosition( eye );
		gameEdit->PlayerGetViewAngles( angles );
		view.camera.origin = eye;
		view.camera.yaw = angles.yaw;
		view.camera.pitch = idMath::ClampFloat( -89.0f, 89.0f, angles.pitch );
	}
	Status( "Paused. Edit away: changes go into the game. Play resumes, Stop ends." );
}

// the game wants the keyboard: take it back from any panel window
static void FocusGame()
{
	if( Sys_MainWindowHandle() != NULL )
	{
		Sys_ExtraWindowFocus( Sys_MainWindowHandle() );
	}
}

static bool			playReloading = false;		// the level is being reloaded around the player
static int			playReloadTime = 0;

// brushes changed while paused: load the rebuilt level and put the player back where they were
static void ReloadAtPlayer()
{
	if( !IsPlaying() || !document.IsOpen() )
	{
		return;
	}
	if( gameEdit->PlayerIsValid() )
	{
		gameEdit->PlayerGetEyePosition( playTeleportEye );
		gameEdit->PlayerGetViewAngles( playTeleportAngles );
		playTeleportAngles.roll = 0.0f;
		playTeleportPending = true;
	}
	playStarting = true;
	playPaused = false;
	playReloading = true;
	playReloadTime = Sys_Milliseconds();
	liveSessionActive = false;		// the reloaded level is the new base
	liveGeometryChanged = false;
	view.ClearOutlines();
	FocusGame();
	common->Printf( "Editor: reloading %s around the player\n", document.Name() );
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "map %s\n", document.Name() ) );
}

// on resume: one line about what the changes made while paused did
static void LiveSummary()
{
	int inGame, nextPlay, rebuild;
	LiveCounts( inGame, nextPlay, rebuild );
	common->Printf( "Editor: resumed: %d in the game, %d next time you play, %d rebuilt\n", inGame, nextPlay, rebuild );
	if( liveChanges.Num() == 0 )
	{
		return;
	}
	idStr text;
	if( rebuild > 0 )
	{
		text = va( "Rebuilding for %d change%s", rebuild, rebuild == 1 ? "" : "s" );
		if( inGame > 0 )
		{
			text += va( "; %d already in the game", inGame );
		}
		text += ". The game carries on when it's done.";
	}
	else
	{
		text = va( "Resumed with %d change%s in the game", inGame, inGame == 1 ? "" : "s" );
		if( nextPlay > 0 )
		{
			text += va( "; %d take%s effect next time you play", nextPlay, nextPlay == 1 ? "s" : "" );
		}
		text += ".";
	}
	Status( text );
}

static void ResumePlay()
{
	if( !playPaused )
	{
		return;
	}
	if( BuildRunning() && buildThen == BUILD_THEN_RELOAD )
	{
		return;		// already rebuilding; the game carries on when it's done
	}
	LiveSync();
	LiveSummary();
	if( liveGeometryChanged )
	{
		// brushes are compiled into the level: rebuild it, then reload around the player
		if( document.IsDirty() && !document.Save() )
		{
			Status( "Couldn't save the level, so the brush changes can't be built" );
			return;
		}
		mapListDirty = true;
		if( LevelNeedsBuild( document.Name() ) )
		{
			StartBuild( document.Name(), BUILD_THEN_RELOAD );
		}
		else
		{
			ReloadAtPlayer();
		}
		return;
	}
	playPaused = false;
	liveChanges.Clear();		// they're in the game now
	liveChangesOpen = false;
	view.ClearOutlines();
	FocusGame();
	Help::Event( "play.resume" );
}

// a background build has finished (or is still going): deal with it
static void BuildUpdate()
{
	if( buildProcess == NULL )
	{
		return;
	}
	BuildReadLog();
	int exitCode = 0;
	if( !Sys_ProcessFinished( buildProcess, exitCode ) )
	{
		return;
	}
	BuildReadLog();
	if( buildLinePart.Length() > 0 )
	{
		BuildReadLine( buildLinePart.c_str() );
		buildLinePart.Clear();
	}
	Sys_ProcessFree( buildProcess );
	buildProcess = NULL;

	buildHasResult = true;
	buildFailed = exitCode != 0;
	if( !buildFailed )
	{
		// it can stop early without saying it failed: make sure it wrote the level
		ID_TIME_T procTime = FILE_NOT_FOUND_TIMESTAMP;
		fileSystem->ReadFile( va( "maps/%s.proc", buildMap.c_str() ), NULL, &procTime );
		if( procTime == FILE_NOT_FOUND_TIMESTAMP || ( buildMapTime != FILE_NOT_FOUND_TIMESTAMP && procTime < buildMapTime ) )
		{
			buildFailed = true;
			if( buildProblems.Num() == 0 )
			{
				buildProblems.Append( "The level compiler finished without writing the level. The build log may say why." );
				buildProblemIsError.Append( true );
			}
		}
	}
	buildResultMap = buildMap;
	buildSeconds = ( Sys_Milliseconds() - buildStartTime + 500 ) / 1000;
	if( buildFailed && buildProblems.Num() == 0 )
	{
		buildProblems.Append( va( "rbdmap stopped with exit code %d before it finished.", exitCode ) );
		buildProblemIsError.Append( true );
	}
	common->Printf( "Editor: build of %s %s in %d s\n", buildMap.c_str(), buildFailed ? "FAILED" : "finished", buildSeconds );

	const buildThen_t then = buildThen;
	buildThen = BUILD_THEN_NOTHING;
	Help::Event( buildLeaked ? "build.leak" : buildFailed ? "build.failed" : "build.done" );
	if( buildFailed )
	{
		if( then == BUILD_THEN_PLAY )
		{
			playStarting = false;
		}
		showProblems = true;
		focusProblems = true;
		Status( "The level didn't build. Problems says why." );
		return;
	}

	if( then == BUILD_THEN_PLAY )
	{
		if( !IsPlaying() && document.IsOpen() && buildMap.Icmp( document.Name() ) == 0 )
		{
			if( LevelNeedsBuildSince() )
			{
				StartBuild( buildMap, BUILD_THEN_PLAY );	// saved again while it was building
			}
			else
			{
				StartLevel( buildMap );
			}
		}
		else
		{
			playStarting = false;
		}
	}
	else if( then == BUILD_THEN_RELOAD )
	{
		if( IsPlaying() && playPaused && document.IsOpen() && buildMap.Icmp( document.Name() ) == 0 )
		{
			ReloadAtPlayer();
		}
	}
	else
	{
		Status( buildProblems.Num() > 0 ? "Built, with warnings (see Problems)" : va( "Built in %d s", buildSeconds ) );
	}
}

static void EditorPause_f( const idCmdArgs& args )
{
	PausePlay();
}

static void EditorResume_f( const idCmdArgs& args )
{
	ResumePlay();
}

static void OpenMap( const char* mapName )
{
	if( IsEditingPiece() )
	{
		FinishPieceEdit( PIECE_SAVE_ALL );	// keep what was being done to the piece
	}
	if( document.IsDirty() && document.IsOpen() )
	{
		// never lose work silently: save what's open before switching
		document.Save();
	}
	if( document.Load( mapName ) )
	{
		view.leakPath.Clear();
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
		if( idStr::Icmpn( mapName, PREFAB_FOLDER "/", idStr::Length( PREFAB_FOLDER ) + 1 ) == 0 )
		{
			// a piece on its own: look at its origin, where copies are placed from
			view.camera.origin.Set( -224.0f, -224.0f, 176.0f );
			view.camera.yaw = 45.0f;
			view.camera.pitch = 25.0f;
		}
		SyncPiecesOnOpen();
		Status( va( "Opened %s", mapName ) );
	}
	else
	{
		Status( va( "Couldn't open %s", mapName ) );
	}
}

static void EditorPlay_f( const idCmdArgs& args )
{
	if( args.Argc() > 1 && idStr::Icmp( args.Argv( 1 ), "anyway" ) == 0 )
	{
		playWithErrors = true;
		PlayDocument();
	}
	else if( args.Argc() > 1 )
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
		common->Printf( "usage: editorOpen <map, e.g. testmaps/test_room>\n" );
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
			Help::Event( "shape.added" );
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

static void EditorMakePiece_f( const idCmdArgs& args );
static void EditorPlacePiece_f( const idCmdArgs& args );
static void EditorSelectPiece_f( const idCmdArgs& args );
static void EditorEditPiece_f( const idCmdArgs& args );
static void EditorPiecePart_f( const idCmdArgs& args );
static void EditorListPieces_f( const idCmdArgs& args );
static void EditorDelete_f( const idCmdArgs& args );
static void EditorHelp_f( const idCmdArgs& args );
static void EditorShowMe_f( const idCmdArgs& args );
static void EditorDuplicate_f( const idCmdArgs& args );

static void RegisterCommands()
{
	if( commandsRegistered )
	{
		return;
	}
	commandsRegistered = true;
	RegisterProblemCommands();
	RegisterGameCommands();
	RegisterMaterialCommands();

	cmdSystem->AddCommand( "editorPlay", EditorPlay_f, CMD_FL_SYSTEM, "plays the open level (or the given map) in the editor window" );
	cmdSystem->AddCommand( "editorStop", EditorStop_f, CMD_FL_SYSTEM, "stops playing and returns to the editor" );
	cmdSystem->AddCommand( "editorPause", EditorPause_f, CMD_FL_SYSTEM, "pauses Play and brings the editor back; edits go into the running game" );
	cmdSystem->AddCommand( "editorResume", EditorResume_f, CMD_FL_SYSTEM, "carries on playing after editorPause" );
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
	cmdSystem->AddCommand( "editorMakePiece", EditorMakePiece_f, CMD_FL_SYSTEM, "makes a piece from the selection: editorMakePiece <name>" );
	cmdSystem->AddCommand( "editorPlacePiece", EditorPlacePiece_f, CMD_FL_SYSTEM, "adds a copy of a piece: editorPlacePiece <name> <x> <y> <z> [yaw]" );
	cmdSystem->AddCommand( "editorSelectPiece", EditorSelectPiece_f, CMD_FL_SYSTEM, "selects a copy of a piece, or one part: editorSelectPiece <copy> [part]" );
	cmdSystem->AddCommand( "editorEditPiece", EditorEditPiece_f, CMD_FL_SYSTEM, "edits a piece in place: editorEditPiece <copy> | done (every copy) | copy (this copy only) | cancel" );
	cmdSystem->AddCommand( "editorPiecePart", EditorPiecePart_f, CMD_FL_SYSTEM, "the selected part of a copy: editorPiecePart revert | apply" );
	cmdSystem->AddCommand( "editorListPieces", EditorListPieces_f, CMD_FL_SYSTEM, "lists copies of pieces and their overrides" );
	cmdSystem->AddCommand( "editorDelete", EditorDelete_f, CMD_FL_SYSTEM, "deletes the selection" );
	cmdSystem->AddCommand( "editorShowMe", EditorShowMe_f, CMD_FL_SYSTEM, "makes a control glow, as a lesson's Show me does: editorShowMe [tab/]control" );
	cmdSystem->AddCommand( "editorHelp", EditorHelp_f, CMD_FL_SYSTEM, "opens the Help panel, on a page if given: editorHelp [page]" );
	cmdSystem->AddCommand( "editorDuplicate", EditorDuplicate_f, CMD_FL_SYSTEM, "duplicates the selection: editorDuplicate <dx> <dy> <dz>" );
	cmdSystem->AddCommand( "editorImport", EditorImport_f, CMD_FL_SYSTEM, "brings a model file in (.glb, .gltf, .obj): editorImport <file> [place]" );
	cmdSystem->AddCommand( "editorImportSize", EditorImportSize_f, CMD_FL_SYSTEM, "what a model was made in: editorImportSize <model> <meters|centimeters|units|scale>" );
	cmdSystem->AddCommand( "editorImportTurn", EditorImportTurn_f, CMD_FL_SYSTEM, "turns a model itself: editorImportTurn <model> <degrees>" );
	cmdSystem->AddCommand( "editorReimport", EditorReimport_f, CMD_FL_SYSTEM, "brings a model in again from its file: editorReimport <model>" );
	cmdSystem->AddCommand( "editorPlaceModel", EditorPlaceModel_f, CMD_FL_SYSTEM, "adds a model to the level: editorPlaceModel <model> [x y z]" );
	cmdSystem->AddCommand( "editorListImports", EditorListImports_f, CMD_FL_SYSTEM, "lists the models brought in" );
	cmdSystem->AddCommand( "editorDropFile", EditorDropFile_f, CMD_FL_SYSTEM, "acts as if a file was dropped on the window: editorDropFile <file>" );

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
			if( name.Icmpn( PREFAB_FOLDER "/", idStr::Length( PREFAB_FOLDER ) + 1 ) == 0 )
			{
				continue;	// pieces live in Kits > My pieces
			}
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
			// Windows lists files below subfolders too; those are copied with their folder
			if( name == "." || name == ".." || name.Find( '/' ) >= 0 || name.Find( '\\' ) >= 0 )
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
	projectFile.AppendPath( "project.daedalus" );
	idFile* f = fileSystem->OpenExplicitFileWrite( projectFile.c_str() );
	if( f == NULL )
	{
		result = va( "Could not write %s.", projectFile.c_str() );
		return false;
	}
	f->Printf( "// Daedalus project\n" );
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
		f->Printf( "// Daedalus project settings (written by New Project)\n" );
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

// projects are folders next to the engine with a project.daedalus in them
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
		projectFile.AppendPath( "project.daedalus" );
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
	Hint( "Daedalus restarts to open a project. Unsaved changes to the open map are saved first." );
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
		ImGui::TextWrapped( "Open it now? Daedalus restarts into the new project. You can also open it later from File > Open Project." );
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
	SelectHit( sel, ImGui::GetIO().KeyCtrl );
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

	UI::SearchField( "##filter", outlinerFilter, sizeof( outlinerFilter ), "Search the level", ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x );
	ImGui::SameLine();
	UI::HelpButton( "panels/outliner", "About the Outliner" );
	ImGui::Spacing();

	ImGui::BeginChild( "outliner_list" );

	// the level's own brushes
	const idMapEntity* world = map->GetEntity( 0 );
	if( outlinerFilter[0] == '\0' )
	{
		const bool open = ImGui::TreeNodeEx( "Level geometry", ImGuiTreeNodeFlags_SpanAvailWidth, ICON_BRUSH "  Level geometry" );
		RowTypeProblem( ICON_BRUSH "      Level geometry", va( "%d", world->GetNumPrimitives() ), WorldProblemSeverity( -1 ), 0 );
		if( open )
		{
			for( int p = 0; p < world->GetNumPrimitives(); p++ )
			{
				const bool isBrush = world->GetPrimitive( p )->GetType() == idMapPrimitive::TYPE_BRUSH;
				const bool selected = document.selection.Contains( Selection( 0, p ) );
				const idStr label = va( "%s  %s %d##prim%d", isBrush ? ICON_BRUSH : ICON_PATCH, isBrush ? "Brush" : "Patch", p, p );
				if( ImGui::Selectable( label, selected, ImGuiSelectableFlags_AllowDoubleClick ) )
				{
					ClickSelect( Selection( 0, p ) );
					if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
					{
						FocusSelection();
					}
				}
				RowTypeProblem( label, "", WorldProblemSeverity( p ), 0, p );
			}
			ImGui::TreePop();
		}
	}

	// everything else, in file order
	for( int i = 1; i < map->GetNumEntities(); i++ )
	{
		const idMapEntity* ent = map->GetEntity( i );
		const idStr searchLabel = EntityLabel( ent, i );
		if( outlinerFilter[0] != '\0' && searchLabel.Find( outlinerFilter, false ) < 0 )
		{
			continue;
		}
		if( FindAnchor( map, ent->epairs.GetString( KEY_OWNER ) ) >= 0 && !( IsEditingPiece() && TopAnchorOf( map, Selection( i ) ) == PieceEditingAnchor() ) )
		{
			continue;	// part of a piece copy: the copy has the row (unless it's the piece being edited)
		}
		if( IsPrefabAnchor( ent ) )
		{
			const bool selected = document.selection.Contains( Selection( i ) );
			const idStr label = va( ICON_MODE_KITS "  %s##ent%d", ent->epairs.GetString( "name" ), i );
			if( ImGui::Selectable( label, selected, ImGuiSelectableFlags_AllowDoubleClick ) )
			{
				SelectHit( Selection( i ), ImGui::GetIO().KeyCtrl );
				if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
				{
					FocusSelection();
				}
			}
			RowTypeProblem( label, va( "Piece: %s", ent->epairs.GetString( KEY_PREFAB ) ), EntityProblemSeverity( i ), i );
			continue;
		}
		const char* classname = ent->epairs.GetString( "classname", "?" );
		const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->FindType( DECL_ENTITYDEF, classname, false ) );
		const char* name = ent->epairs.GetString( "name", classname );
		const bool selected = document.selection.Contains( Selection( i ) );
		const idStr label = va( "%s  %s##ent%d", def != NULL ? ThingIcon( def ) : ICON_ENTITY, name, i );
		if( ImGui::Selectable( label, selected, ImGuiSelectableFlags_AllowDoubleClick ) )
		{
			ClickSelect( Selection( i ) );
			if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
			{
				FocusSelection();
			}
		}
		RowTypeProblem( label, def != NULL ? ThingLabel( def ) : classname, EntityProblemSeverity( i ), i );
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
	if( UI::BeginCard( "door", ICON_DOOR, "Door", "The violet arrow in the view shows where the door slides to. It opens when the player comes near, unless it's locked." ) )
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
	if( UI::BeginCardAt( 1, "advanced", ICON_ADVANCED, "Advanced", "Every setting this thing has, as the raw keys and values the game reads.", false ) )
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

	DrawMaterialThumb( current, ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y );
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::PushFont( UI::Font( UI::FONT_BOLD ) );
	ImGui::TextUnformatted( ShortMaterialName( current ).c_str() );
	ImGui::PopFont();
	ImGui::TextDisabled( "%s", current );
	ImGui::EndGroup();

	const float buttonWidth = ImGui::GetFrameHeight();
	ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - ( buttonWidth + ImGui::GetStyle().ItemSpacing.x ) * 3.0f );
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
	ImGui::SameLine();
	if( UI::IconButton( "editmaterial", ICON_SETTINGS, "Open this material in the Material panel" ) )
	{
		OpenMaterialPanel( current );
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

// worldspawn: how bright the level is where no light reaches
static void DrawLevelLightingCard( idMapEntity* ent )
{
	if( UI::BeginCard( "levellight", ICON_LIGHT, "Lighting", "Fill light is how bright the level is where none of your lights reach. Keep it low so your lights stand out; baking lighting replaces it with light bounced from your lights." ) )
	{
		float fill = ent->epairs.GetFloat( "fill_light", "0.12" );
		UI::FieldLabel( "Fill light" );
		if( ImGui::SliderFloat( "##fill", &fill, 0.0f, 1.0f, fill <= 0.0f ? "None: pitch dark" : "%.2f" ) )
		{
			ent->epairs.Set( "fill_light", va( "%.2f", fill ) );
			document.EndChange();
		}
		UndoBracket( "Fill light" );
		UI::Tip( "0 is pitch dark away from lights; 1 lights everything evenly" );
	}
	UI::EndCard();
}

static void DrawEntityInspector( idMapEntity* ent )
{
	const char* classname = ent->epairs.GetString( "classname" );
	const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->FindType( DECL_ENTITYDEF, classname, false ) );
	const bool isWorld = document.selection.Primary().entity == 0;

	if( !isWorld )
	{
		DrawPlacementCard();
	}
	else
	{
		DrawLevelLightingCard( ent );
	}
	if( idStr::Icmp( classname, "func_door" ) == 0 )
	{
		DrawDoorCard( ent, def );
	}
	if( idStr::Icmp( classname, "light" ) == 0 )
	{
		DrawLightCard( ent );
	}
	if( !isWorld && ent->GetNumPrimitives() == 0 && Import::IsModelFile( ent->epairs.GetString( "model" ) ) )
	{
		DrawModelCard( ent );
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
	const float startY = ImGui::GetCursorPosY();
	ImGui::SetCursorPosX( ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight() );
	UI::HelpButton( "panels/inspector", "About the Inspector" );
	ImGui::SetCursorPosY( startY );
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
	DrawLiveSelection();
	DrawSelectionProblems();
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
	if( DrawPieceInspector() )
	{
		ImGui::End();
		return;
	}
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
		DrawPartCard();
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
			DrawPartCard();
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
	Help::Event( "shape.added" );
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

	if( ImGui::BeginTabBar( "content_tabs", ImGuiTabBarFlags_None ) )
	{
		if( ImGui::TabItemButton( ICON_HELP "##contenthelp", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip ) )
		{
			Help::Open( "panels/content" );
		}
		UI::Tip( "About the Content panel" );
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

			ImGui::SameLine();
			if( ImGui::SmallButton( ICON_PLUS "  New material" ) )
			{
				RequestNewMaterial();
			}
			UI::Tip( "Make a new material. You can also drop pictures on the editor: they become a material." );

			ImGui::BeginChild( "materials" );
			const float tile = 72.0f * UI::Scale();
			const int perRow = Max( 1, ( int )( ImGui::GetContentRegionAvail().x / ( tile + ImGui::GetStyle().ItemSpacing.x ) ) );
			int shown = 0;
			const int count = declManager->GetNumDecls( DECL_MATERIAL );
			for( int i = 0; i < count; i++ )
			{
				const idDecl* decl = declManager->DeclByIndex( DECL_MATERIAL, i, false );
				if( decl == NULL || !MaterialListed( decl->GetName() ) )
				{
					continue;
				}
				if( materialFilter[0] != '\0' && idStr::FindText( decl->GetName(), materialFilter, false ) < 0 )
				{
					continue;
				}
				if( shown++ % perRow != 0 )
				{
					ImGui::SameLine();
				}
				ImGui::PushID( i );
				const bool clicked = DrawMaterialThumb( decl->GetName(), tile, currentMaterial.Icmp( decl->GetName() ) == 0 );
				UI::Tip( va( "%s\nClick: paint with it (and put it on anything selected). Double-click: open it in the Material panel.", decl->GetName() ) );
				if( clicked && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
				{
					OpenMaterialPanel( decl->GetName() );
				}
				ImGui::PopID();
				if( clicked )
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
		if( ImGui::BeginTabItem( ICON_SHAPE_BOX "  Models" ) )
		{
			DrawContentModels();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
}

// load the leak line dmap wrote (maps/<level>.lin) and look along it
static void ShowLeak()
{
	view.leakPath.Clear();
	char* text = NULL;
	if( fileSystem->ReadFile( va( "maps/%s.lin", buildResultMap.c_str() ), ( void** )&text ) <= 0 || text == NULL )
	{
		Status( "The leak's line wasn't found; build again to make it" );
		return;
	}
	idLexer src( text, idStr::Length( text ), "leak", LEXFL_NOERRORS | LEXFL_ALLOWMULTICHARLITERALS );
	idVec3 point;
	while( true )
	{
		point.x = src.ParseFloat();
		point.y = src.ParseFloat();
		point.z = src.ParseFloat();
		if( src.EndOfFile() && point == vec3_origin )
		{
			break;
		}
		view.leakPath.Insert( point, 0 );		// the file runs from the void inwards; keep it inside out
		if( src.EndOfFile() )
		{
			break;
		}
	}
	fileSystem->FreeFile( text );
	if( view.leakPath.Num() < 2 )
	{
		view.leakPath.Clear();
		return;
	}
	idBounds bounds;
	bounds.Clear();
	for( int i = 0; i < view.leakPath.Num(); i++ )
	{
		bounds.AddPoint( view.leakPath[i] );
	}
	view.camera.Focus( bounds );
	Status( "The red line runs from inside the level out through the gap" );
}

static void DrawBuildLog()
{
	if( !showBuildLog )
	{
		return;
	}
	ImGui::SetNextWindowSize( ImVec2( 640.0f * UI::Scale(), 420.0f * UI::Scale() ), ImGuiCond_FirstUseEver );
	if( ImGui::Begin( va( "Build log: %s###buildlog", buildMap.c_str() ), &showBuildLog ) )
	{
		if( ImGui::SmallButton( "Copy" ) )
		{
			ImGui::SetClipboardText( buildLog.c_str() );
		}
		ImGui::Separator();
		ImGui::BeginChild( "log", ImVec2( 0, 0 ), false, ImGuiWindowFlags_HorizontalScrollbar );
		ImGui::PushFont( UI::Font( UI::FONT_SMALL ) );
		ImGui::TextUnformatted( buildLog.c_str(), buildLog.c_str() + buildLog.Length() );
		ImGui::PopFont();
		if( BuildRunning() && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f )
		{
			ImGui::SetScrollHereY( 1.0f );
		}
		ImGui::EndChild();
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
		UI::Emblem( ImGui::GetFontSize() );
		ImGui::SameLine( 0.0f, 10.0f * UI::Scale() );
		ImGui::TextUnformatted( "Welcome to Daedalus" );
		ImGui::PopFont();
		Hint( "A level in five steps. The lesson walks you through them in the Help panel and ticks each off as you do it." );
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
			{ ICON_PLAY,		"Play",				"Press Play at the top (F5), or use its arrow to start where the camera is. Esc pauses: edit anything, and the game changes with it. F5 carries on, F10 stops." },
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
		ImGui::TextColored( UI::Muted(), "Own DOOM 3, Quake 4 or Prey? Daedalus can open them from where they're installed." );
		ImGui::SameLine();
		if( ImGui::SmallButton( "Your games" ) )
		{
			showWelcome = false;
			OpenGames();
		}
		ImGui::Spacing();
		bool again = editor_showWelcome.GetBool();
		if( ImGui::Checkbox( "Show this when the editor opens", &again ) )
		{
			editor_showWelcome.SetBool( again );
		}
		const float buttonWidth = 120.0f * UI::Scale();
		ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - buttonWidth * 1.3f - 70.0f * UI::Scale() );
		if( ImGui::Button( "Close" ) )
		{
			showWelcome = false;
		}
		ImGui::SameLine();
		if( UI::ColorButton( "Start the lesson", UI::Accent(), ImVec2( buttonWidth * 1.3f, 0 ) ) )
		{
			showWelcome = false;
			Help::Open( "lessons/getting-started" );
		}
		UI::Tip( "Opens the Getting started lesson in the Help panel: it ticks off each step as you do it" );
	}
	ImGui::End();
}

/*
===============================================================================

	Your games

	The id Tech 4 games installed on this computer (framework/GameLibrary).
	Daedalus reads them where they are; it never copies or shares their files.

===============================================================================
*/

static char gamesFolderInput[1024] = "";

static void OpenGames()
{
	showGames = true;
	gamesFocus = true;
}

static void AddGamesFolder( const char* folder )
{
	if( folder == NULL || folder[0] == 0 )
	{
		return;
	}
	const int found = GameLibrary::AddFolder( folder );
	if( found == 0 )
	{
		Status( va( "No id Tech 4 games in %s. Pick the folder the game is installed in, or a folder of games.", folder ) );
		return;
	}
	Status( va( "Found %d game%s in %s.", found, found == 1 ? "" : "s", folder ) );
	OpenGames();
}

static ImVec4 SupportColor( gameSupport_t support )
{
	switch( support )
	{
		case SUPPORT_PLAY_EDIT:
			return UI::Good();
		case SUPPORT_EDIT_PORT:
			return UI::Warn();
		default:
			return UI::Muted();
	}
}

static void DrawGames()
{
	if( !showGames )
	{
		return;
	}
	ImGui::SetNextWindowSize( ImVec2( 560.0f * UI::Scale(), 600.0f * UI::Scale() ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2( 0.5f, 0.5f ) );
	if( gamesFocus )
	{
		gamesFocus = false;
		ImGui::SetNextWindowFocus();
	}
	if( !ImGui::Begin( WINDOW_GAMES, &showGames, ImGuiWindowFlags_NoDocking ) )
	{
		ImGui::End();
		return;
	}

	UI::Hint( "id Tech 4 games on this computer. Daedalus reads them where they're installed and never copies or shares their files. Opening one to play or edit is the next step." );
	ImGui::Spacing();

	// adding a folder: the system's picker, or typed
	if( UI::ColorButton( ICON_PLUS "  Add a folder", UI::Accent() ) )
	{
		char picked[1024];
		if( Sys_OpenFolderDialog( "Pick a game's folder, or a folder of games", picked, sizeof( picked ) ) )
		{
			AddGamesFolder( picked );
		}
	}
	UI::Tip( "A game's own folder (the one with base or q4base in it), or a folder that holds several games. You can also drop a folder on the window." );
	ImGui::SameLine();
	if( ImGui::Button( ICON_TURN_RIGHT "  Search again" ) )
	{
		GameLibrary::Refresh();
	}
	UI::Tip( "Looks again in your folders, Steam and GOG" );
	ImGui::SameLine();
	bool stores = cvarSystem->GetCVarBool( "games_searchStores" );
	if( ImGui::Checkbox( "Look in Steam and GOG", &stores ) )
	{
		cvarSystem->SetCVarBool( "games_searchStores", stores );
		GameLibrary::Refresh();
	}
	ImGui::SameLine();
	UI::HelpButton( "panels/games", "About Your games" );

	ImGui::SetNextItemWidth( -90.0f * UI::Scale() );
	const bool enter = ImGui::InputTextWithHint( "##gamefolder", "Or type a folder, e.g. C:/Games/Doom 3", gamesFolderInput, sizeof( gamesFolderInput ), ImGuiInputTextFlags_EnterReturnsTrue );
	ImGui::SameLine();
	if( ( ImGui::Button( "Add", ImVec2( -1, 0 ) ) || enter ) && gamesFolderInput[0] )
	{
		AddGamesFolder( gamesFolderInput );
		gamesFolderInput[0] = 0;
	}

	ImGui::Spacing();
	const idList<installedGame_t>& games = GameLibrary::Games();
	if( games.Num() == 0 )
	{
		UI::SectionLabel( "No games found yet" );
		ImGui::TextWrapped( "Add the folder you installed a game to. Daedalus knows DOOM 3 BFG Edition, DOOM 3 and Resurrection of Evil, Quake 4, Prey (2006) and Wolfenstein (2009)." );
	}
	else
	{
		UI::SectionLabel( va( "%d game%s", games.Num(), games.Num() == 1 ? "" : "s" ) );
	}
	for( int i = 0; i < games.Num(); i++ )
	{
		const installedGame_t& game = games[i];
		const gameSupport_t support = GameLibrary::Support( game.kind );
		if( UI::BeginCard( va( "game%d", i ), ICON_PLAY, GameLibrary::Name( game.kind ) ) )
		{
			ImGui::TextColored( SupportColor( support ), "%s", GameLibrary::SupportLabel( support ) );
			ImGui::TextWrapped( "%s", GameLibrary::SupportText( game.kind ) );
			if( UI::IconButton( va( "showgame%d", i ), ICON_OPEN, "Show the folder" ) )
			{
				Sys_OpenFolder( game.data.c_str() );
			}
			ImGui::SameLine();
			ImGui::PushStyleColor( ImGuiCol_Text, UI::Muted() );
			ImGui::TextWrapped( "%s  (found in %s)", game.data.c_str(), game.foundBy.c_str() );
			ImGui::PopStyleColor();
		}
		UI::EndCard();
	}

	const idStrList& folders = GameLibrary::Folders();
	if( folders.Num() > 0 )
	{
		ImGui::Spacing();
		UI::SectionLabel( "Your folders" );
		for( int i = 0; i < folders.Num(); i++ )
		{
			if( UI::IconButton( va( "forget%d", i ), ICON_CLOSE, "Stop looking here. The games stay where they are." ) )
			{
				GameLibrary::RemoveFolder( folders[i] );
				break;
			}
			ImGui::SameLine();
			ImGui::TextUnformatted( folders[i].c_str() );
		}
	}
	ImGui::End();
}

static void EditorGames_f( const idCmdArgs& args )
{
	OpenGames();
	common->Printf( "Your games: %d\n", GameLibrary::Games().Num() );
}

static void EditorDepth_f( const idCmdArgs& args )
{
	if( args.Argc() > 1 )
	{
		const char* a = args.Argv( 1 );
		int d = atoi( a );
		for( int i = 0; i < 3; i++ )
		{
			if( idStr::Icmp( a, UI::DepthName( i ) ) == 0 )
			{
				d = i;
			}
		}
		UI::SetDepth( d );
	}
	common->Printf( "Depth: %s\n", UI::DepthName( UI::Depth() ) );
}

static void RegisterGameCommands()
{
	cmdSystem->AddCommand( "editorDepth", EditorDepth_f, CMD_FL_TOOL, "shows or sets how much the editor shows: Create, Understand or Control" );
	cmdSystem->AddCommand( "editorGames", EditorGames_f, CMD_FL_TOOL, "opens Your games: the id Tech 4 games on this computer" );
}

static void DrawAbout()
{
	if( !showAbout )
	{
		return;
	}

	ImGui::SetNextWindowSize( ImVec2( 460, 0 ), ImGuiCond_Appearing );
	if( ImGui::Begin( "About Daedalus", &showAbout, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse ) )
	{
		UI::Emblem( 40.0f * UI::Scale() );
		ImGui::SameLine( 0.0f, 12.0f * UI::Scale() );
		ImGui::BeginGroup();
		ImGui::PushFont( UI::Font( UI::FONT_TITLE ) );
		ImGui::TextUnformatted( "Daedalus" );
		ImGui::PopFont();
		ImGui::TextDisabled( "%s", ENGINE_VERSION );
		ImGui::EndGroup();
		ImGui::Spacing();
		ImGui::TextDisabled( "Based on %s and the Doom 3 BFG Edition GPL source.", UPSTREAM_VERSION );
		ImGui::Spacing();
		ImGui::TextWrapped( "Licensed under the GNU GPL v3 with id Software's additional terms." );
		ImGui::TextWrapped( "Material path: %s", r_materialPath.GetString() );
		ImGui::Separator();
		ImGui::TextColored( UI::Accent(), ICON_HELP );
		ImGui::SameLine();
		ImGui::TextUnformatted( "F1 opens Help: a page for every part of the editor, and lessons that tick off as you go." );
		ImGui::Spacing();
		ImGui::Text( "Moving around" );
		Hint( "Hold the right mouse button to look around; WASD moves, Q and E go down and up, Shift is faster, and the wheel changes speed. Hold the middle button to pan, Alt + left button to orbit, and roll the wheel to move forward and back. T switches to the top view." );
		ImGui::Text( "Playing" );
		Hint( "F5 plays from the player start (or, with the arrow on Play, from where the camera is; Shift flips it once). Esc pauses and brings the editor back with the game still loaded: what you change goes into the game, and brush changes rebuild when you carry on. F5 carries on, F10 stops." );
		ImGui::Text( "Pieces" );
		Hint( "Select things and press Make piece (in the Kits tab, or the bar above the dock) to save them as a piece; place copies from Kits. A click selects a whole copy. Double-click picks one part: change it and only that copy changes (the Inspector marks it, with Revert and Apply to all copies). Edit piece changes the piece where a copy sits, with the rest of the level as ghosts; then save it to every copy, or to this copy only. Undo also undoes changes to pieces. Pieces can hold pieces." );
		ImGui::Text( "Building" );
		Hint( "The game doesn't read the level you edit directly: building turns it into what the game loads (it also checks it for leaks). Play builds when it needs to, in the background, with the progress left of Play; Ctrl+B builds without playing. Problems shows what the build said." );
		ImGui::Text( "Panels" );
		Hint( "Drag a panel's tab to move it: onto another panel to stack them, to a side to split, or out of the window onto another monitor. View > Reset layout puts everything back." );
		ImGui::Text( "Editing" );
		Hint( "Click to select, Ctrl+click to add or remove, Shift+click to pick faces. W, E and R move, turn and resize; B draws brushes, C cuts them, V drags their corners, P paints and I picks a material up. Hold Space for the tool wheel. 1 to 5 switch the dock's tabs (Build, Paint, Logic, Light, Kits). [ and ] change the grid. Carve cuts doorways and windows out of walls. Ctrl+Z undo, Ctrl+Y redo, Ctrl+D duplicate, Delete removes, F frames the selection, Ctrl+S saves, Ctrl+B builds." );
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
		label = "Turn";
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
	if( !io.KeyCtrl && !hit.IsEmpty() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) && !IsEditingPiece() && TopAnchorOf( document.Map(), hit ) >= 0 )
	{
		document.selection.Set( hit );	// just this part of the copy
		return;
	}
	SelectHit( hit, io.KeyCtrl );
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
	const idStr text = ( drawStage == DRAW_FOOTPRINT ) ? idStr( va( "%.0f x %.0f", size.x, size.y ) ) : idStr( va( "%.0f x %.0f x %.0f   click to finish, Esc cancels", size.x, size.y, size.z ) );
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
			draw->AddConvexPolyFilled( pts.Ptr(), pts.Num(), selected ? IM_COL32( 52, 211, 153, 70 ) : fill );
			draw->AddPolyline( pts.Ptr(), pts.Num(), selected ? IM_COL32( 52, 211, 153, 255 ) : IM_COL32( 200, 205, 215, 255 ), ImDrawFlags_Closed, selected ? 2.0f : 1.0f );
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
			const ImU32 col = selected ? IM_COL32( 52, 211, 153, 255 ) : IM_COL32( ( int )( color.x * 255 ), ( int )( color.y * 255 ), ( int )( color.z * 255 ), 255 );
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
			SelectHit( hit, false );
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
		SelectHit( hit, true );
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
	static const char* toolEvents[GIZMO_COUNT] = { "tool.move", "tool.turn", "tool.resize", "tool.cut", "tool.corners", "tool.draw", "tool.paint" };
	if( mode >= 0 && mode < GIZMO_COUNT )
	{
		Help::Event( toolEvents[mode] );
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
	static const char* modeEvents[] = { "mode.build", "mode.paint", "mode.logic", "mode.light", "mode.kits" };
	if( mode >= 0 && mode < ( int )( sizeof( modeEvents ) / sizeof( modeEvents[0] ) ) )
	{
		Help::Event( modeEvents[mode] );
	}
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
	Help::Event( planView ? "view.top" : "view.3d" );
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

/*
===============================================================================

	Pieces (prefabs; see EditorPrefabs.h)

	A click on any part of a copy selects the whole copy; a double-click picks
	just that part, and what changes then is that copy's alone (an override).
	"Edit piece" changes the piece itself right where a copy sits: the rest of
	the level turns to ghosts. Saving writes the piece (every copy changes) or
	keeps the changes in that copy alone. Undo puts piece files back as well.

===============================================================================
*/

static idStr		pieceEditing;				// the anchor name of the copy being edited in place, or ""
static idStr		pieceEditPrefab;
static int			pieceEditUndoDepth = 0;
static pieceEditStart_t	pieceEditStart;			// the copy's own changes when the edit began
static idStrList	pieceAdopted;				// "owner|part" of things made during the edit
static idStrList	pieceNames;
static bool			pieceListDirty = true;
static bool			pieceMakeRequest = false;
static char			pieceMakeName[64] = "";

static int PieceEditingAnchor()
{
	return pieceEditing.Length() > 0 ? FindAnchor( document.Map(), pieceEditing ) : -1;
}

static bool IsEditingPiece()
{
	return PieceEditingAnchor() >= 0;
}

// the ghosts: everything not in the copy being edited
static void UpdatePieceFocus()
{
	view.focusOwners.Clear();
	const int anchor = PieceEditingAnchor();
	if( anchor < 0 )
	{
		return;
	}
	idList<Selection> items;
	PieceItems( document.Map(), anchor, items );
	for( int i = 0; i < items.Num(); i++ )
	{
		if( items[i].primitive < 0 && IsPrefabAnchor( document.Map()->GetEntity( items[i].entity ) ) )
		{
			view.focusOwners.Append( document.Map()->GetEntity( items[i].entity )->epairs.GetString( "name" ) );
		}
	}
}

static void SelectItems( const idList<Selection>& items, bool toggle )
{
	if( toggle )
	{
		bool allIn = true;
		for( int i = 0; i < items.Num(); i++ )
		{
			allIn &= document.selection.Contains( items[i] );
		}
		for( int i = 0; i < items.Num(); i++ )
		{
			if( allIn )
			{
				document.selection.Remove( items[i] );
			}
			else if( !document.selection.Contains( items[i] ) )
			{
				document.selection.Add( items[i] );
			}
		}
		return;
	}
	document.selection.Clear();
	// a copy's own anchor (first in PieceItems) goes last, so the copy is what the Inspector shows
	const bool anchorFirst = items.Num() > 0 && items[0].primitive < 0 && items[0].entity > 0 && IsPrefabAnchor( document.Map()->GetEntity( items[0].entity ) );
	for( int i = anchorFirst ? 1 : 0; i < items.Num(); i++ )
	{
		document.selection.Add( items[i] );
	}
	if( anchorFirst )
	{
		document.selection.Add( items[0] );
	}
}

// what a click selects: whole copies of pieces, or (while editing one) its parts
static void SelectHit( const Selection& hit, bool toggle )
{
	if( hit.IsEmpty() )
	{
		if( !toggle )
		{
			document.selection.Clear();
		}
		return;
	}
	if( document.Map() == NULL )
	{
		return;
	}
	idList<Selection> items;
	GrabSelection( document.Map(), hit, PieceEditingAnchor(), items );
	if( items.Num() == 0 )
	{
		if( IsEditingPiece() )
		{
			Status( "While you edit a piece only the piece changes. Save or Cancel above the view goes back to the level." );
		}
		return;
	}
	SelectItems( items, toggle );
	if( !document.selection.IsEmpty() )
	{
		Help::Event( "select.any" );
	}
}

static void StartPieceEdit( int anchor )
{
	if( anchor < 0 || document.Map() == NULL )
	{
		return;
	}
	if( IsEditingPiece() )
	{
		FinishPieceEdit( PIECE_SAVE_ALL );
		anchor = -1;
		return;
	}
	const idMapEntity* ent = document.Map()->GetEntity( anchor );
	pieceEditing = ent->epairs.GetString( "name" );
	pieceEditPrefab = ent->epairs.GetString( KEY_PREFAB );
	pieceEditUndoDepth = document.UndoDepth();
	SnapshotCopyChanges( document.Map(), anchor, pieceEditStart );
	pieceAdopted.Clear();
	document.selection.Clear();
	UpdatePieceFocus();
	Help::Event( "piece.edit" );
	Status( va( "Editing the piece \"%s\". When you're done, save it to every copy or to this copy only.", pieceEditPrefab.c_str() ) );
}

static void FinishPieceEdit( pieceEditEnd_t end )
{
	const int anchor = PieceEditingAnchor();
	if( pieceEditing.Length() == 0 )
	{
		return;
	}
	idMapFile* map = document.Map();
	if( end == PIECE_SAVE_ALL && anchor >= 0 )
	{
		idStr error;
		document.BeginChange( "Edit piece" );
		const bool written = WritePrefabFromCopy( map, anchor, error, &pieceEditStart );
		pieceEditing.Clear();
		view.focusOwners.Clear();
		document.EndChange();
		document.ValidateSelection();
		int copies = 0;
		for( int e = 1; e < map->GetNumEntities(); e++ )
		{
			copies += ( IsPrefabAnchor( map->GetEntity( e ) ) && pieceEditPrefab.Icmp( map->GetEntity( e )->epairs.GetString( KEY_PREFAB ) ) == 0 ) ? 1 : 0;
		}
		Help::Event( "piece.saved.all" );
		Status( written ? va( "Saved the piece \"%s\": %d %s in this level updated (other levels update when they open). Undo puts the old piece back.", pieceEditPrefab.c_str(), copies, copies == 1 ? "copy" : "copies" ) : error.c_str() );
	}
	else if( end == PIECE_SAVE_COPY && anchor >= 0 )
	{
		// the piece stays as it was; what changed is this copy's own (marked as overrides).
		// Things made during the edit become ordinary level things beside it.
		document.BeginChange( "Edit copy" );
		idList<Selection> items;
		PieceItems( map, anchor, items );
		for( int i = 0; i < items.Num(); i++ )
		{
			idDict* dict = items[i].primitive >= 0 ? &map->GetEntity( 0 )->GetPrimitive( items[i].primitive )->epairs : &map->GetEntity( items[i].entity )->epairs;
			if( pieceAdopted.FindIndex( va( "%s|%s", dict->GetString( KEY_OWNER ), dict->GetString( KEY_PART ) ) ) >= 0 )
			{
				dict->Delete( KEY_OWNER );
				dict->Delete( KEY_PART );
			}
		}
		pieceEditing.Clear();
		view.focusOwners.Clear();
		document.EndChange();		// the change hook records what differs from the piece
		document.ValidateSelection();
		Help::Event( "piece.saved.copy" );
		Status( va( "Saved to this copy only: the piece \"%s\" and its other copies are unchanged.", pieceEditPrefab.c_str() ) );
	}
	else
	{
		pieceEditing.Clear();
		view.focusOwners.Clear();
		while( document.UndoDepth() > pieceEditUndoDepth && document.Undo() )
		{
		}
		Status( "Piece edit cancelled" );
	}
	pieceAdopted.Clear();
	document.selection.Clear();
	pieceListDirty = true;
}

// While a piece is edited in place, new things made then (and pieces placed then) become part of it.
static void AdoptIntoEditedPiece( Document& doc, int anchor )
{
	idMapFile* map = doc.Map();
	const idStr name = map->GetEntity( anchor )->epairs.GetString( "name" );
	idStrList parts;
	const idMapEntity* world = map->GetEntity( 0 );
	for( int p = 0; p < world->GetNumPrimitives(); p++ )
	{
		if( name == world->GetPrimitive( p )->epairs.GetString( KEY_OWNER ) )
		{
			parts.Append( world->GetPrimitive( p )->epairs.GetString( KEY_PART ) );
		}
	}

	for( int i = 0; i < doc.selection.Num(); i++ )
	{
		const Selection& sel = doc.selection[i];
		if( sel.IsFace() || sel.entity == anchor || sel.entity < 0 || sel.entity >= map->GetNumEntities() )
		{
			continue;
		}
		if( sel.entity == 0 )
		{
			if( sel.primitive < 0 || sel.primitive >= map->GetEntity( 0 )->GetNumPrimitives() )
			{
				continue;
			}
			idDict& dict = map->GetEntity( 0 )->GetPrimitive( sel.primitive )->epairs;
			if( dict.GetString( KEY_OWNER )[0] != '\0' )
			{
				continue;
			}
			idStr part;
			int n = parts.Num();
			do
			{
				part = va( "b%d", n++ );
			}
			while( parts.FindIndex( part ) >= 0 );
			parts.Append( part );
			dict.Set( KEY_OWNER, name );
			dict.Set( KEY_PART, part );
			pieceAdopted.AddUnique( va( "%s|%s", name.c_str(), part.c_str() ) );
			continue;
		}
		idMapEntity* ent = map->GetEntity( sel.entity );
		if( ent->epairs.GetString( KEY_OWNER )[0] != '\0' )
		{
			continue;
		}
		if( IsPrefabAnchor( ent ) && pieceEditPrefab.Icmp( ent->epairs.GetString( KEY_PREFAB ) ) == 0 )
		{
			Status( "A piece can't contain a copy of itself, so that copy stays outside it." );
			continue;
		}
		if( ent->epairs.GetString( "name" )[0] == '\0' )
		{
			ent->epairs.Set( "name", map->GetUniqueEntityName( ent->epairs.GetString( "classname" ) ) );
		}
		ent->epairs.Set( KEY_OWNER, name );
		ent->epairs.Set( KEY_PART, ent->epairs.GetString( "name" ) );
		pieceAdopted.AddUnique( va( "%s|%s", name.c_str(), ent->epairs.GetString( "name" ) ) );
	}
}

// things in copies of pieces that have changes of their own
static int CountOverrides( const idMapFile* map )
{
	int n = 0;
	for( int e = 1; e < map->GetNumEntities(); e++ )
	{
		n += map->GetEntity( e )->epairs.GetString( KEY_OVERRIDES )[0] != '\0' ? 1 : 0;
	}
	const idMapEntity* world = map->GetEntity( 0 );
	for( int p = 0; p < world->GetNumPrimitives(); p++ )
	{
		n += world->GetPrimitive( p )->epairs.GetString( KEY_OVERRIDES )[0] != '\0' ? 1 : 0;
	}
	return n;
}

// after every change: keep copies linked and their overrides recorded
static void PieceAfterChange( Document& doc )
{
	if( doc.Map() == NULL )
	{
		return;
	}
	const int editing = PieceEditingAnchor();
	if( editing >= 0 )
	{
		AdoptIntoEditedPiece( doc, editing );
	}
	const int overridesBefore = CountOverrides( doc.Map() );
	TrackPrefabEdits( doc.Map(), editing >= 0 ? pieceEditing.c_str() : NULL, doc.duplicateRenames, doc.selection.items );
	if( CountOverrides( doc.Map() ) > overridesBefore )
	{
		Help::Event( "piece.override" );
	}
	Help::Event( va( "did:%s", doc.ChangeLabel() ) );
	doc.ValidateSelection();
	if( editing >= 0 )
	{
		UpdatePieceFocus();
	}
}

// piece files written by a change go in its undo step
static void PieceFileWritten( const char* relativePath, bool existedBefore, const idStr& before, const idStr& after )
{
	document.RecordFileWrite( relativePath, existedBefore, before, after );
}

// undo or redo put piece files back: what was read from them is out of date
static void PieceFilesRestored()
{
	ForgetAllPrefabs();
	pieceListDirty = true;
	MaterialFilesRestored();
}

// "Show me" in a lesson: open the dock tab before the slash, make the control after it glow
static void ShowMeFor( const char* target, float seconds )
{
	idStr name = target;
	const int slash = name.Find( '/' );
	if( slash > 0 )
	{
		static const char* modeNames[] = { "build", "paint", "logic", "light", "kits" };
		const idStr mode = name.Left( slash );
		for( int i = 0; i < ( int )( sizeof( modeNames ) / sizeof( modeNames[0] ) ); i++ )
		{
			if( mode.Icmp( modeNames[i] ) == 0 )
			{
				SetMode( ( editMode_t )i );
			}
		}
		name = name.Right( name.Length() - slash - 1 );
	}
	UI::Highlight( name, seconds );
}

static void ShowMe( const char* target )
{
	ShowMeFor( target, 6.0f );
}

static void OpenHelpPage( const char* page )
{
	Help::Open( page );
}

// a level just opened: bring its copies up to date with their pieces
static void SyncPiecesOnOpen()
{
	pieceEditing.Clear();
	view.focusOwners.Clear();
	document.afterChange = PieceAfterChange;
	if( !document.IsOpen() )
	{
		return;
	}
	document.BeginChange( "Update pieces" );
	const int changed = SyncPrefabs( document.Map() );
	if( changed > 0 )
	{
		document.EndChange();
		Status( "Copies of pieces were updated to match their pieces" );
	}
	else
	{
		document.CancelChange();
	}
}

static void PlacePiece( const char* name, const idVec3& point )
{
	if( !document.IsOpen() )
	{
		return;
	}
	document.BeginChange( va( "Add %s", name ) );
	const int anchor = PlacePrefab( document.Map(), name, point, mat3_identity );
	if( anchor < 0 )
	{
		document.CancelChange();
		Status( va( "The piece \"%s\" couldn't be read", name ) );
		return;
	}
	idList<Selection> items;
	PieceItems( document.Map(), anchor, items );
	SelectItems( items, false );
	document.EndChange();
	Help::Event( "piece.placed" );
}

static void RefreshPieceList()
{
	if( pieceListDirty )
	{
		ListPrefabs( pieceNames );
		pieceListDirty = false;
	}
}

static idStr CleanPieceName( const char* text )
{
	idStr name;
	for( const char* c = text; *c != '\0'; c++ )
	{
		const char ch = idStr::ToLower( *c );
		if( ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' ) || ch == '_' || ch == '-' )
		{
			name += ch;
		}
		else if( ch == ' ' && name.Length() > 0 && name[name.Length() - 1] != '_' )
		{
			name += '_';
		}
	}
	name.StripTrailing( '_' );
	return name;
}

static void DrawMakePieceDialog()
{
	if( pieceMakeRequest )
	{
		pieceMakeRequest = false;
		pieceMakeName[0] = '\0';
		ImGui::OpenPopup( "Make a piece" );
	}
	ImGui::SetNextWindowSize( ImVec2( 420.0f * UI::Scale(), 0.0f ) );
	if( !ImGui::BeginPopupModal( "Make a piece", NULL, ImGuiWindowFlags_NoResize ) )
	{
		return;
	}
	UI::Hint( "The selection becomes a piece you can place again and again from Kits > My pieces. Every copy stays linked: change the piece and they all change." );
	ImGui::Spacing();
	UI::FieldLabel( "Name" );
	if( ImGui::IsWindowAppearing() )
	{
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::SetNextItemWidth( -FLT_MIN );
	const bool enter = ImGui::InputText( "##piecename", pieceMakeName, sizeof( pieceMakeName ), ImGuiInputTextFlags_EnterReturnsTrue );
	const idStr name = CleanPieceName( pieceMakeName );
	RefreshPieceList();
	const bool taken = name.Length() > 0 && pieceNames.FindIndex( name ) >= 0;
	if( taken )
	{
		ImGui::TextColored( UI::Warn(), ICON_WARNING "  There's already a piece called \"%s\"", name.c_str() );
	}
	else if( name.Length() > 0 && name != pieceMakeName )
	{
		ImGui::TextDisabled( "Saved as \"%s\"", name.c_str() );
	}
	ImGui::Spacing();
	const bool ok = name.Length() > 0 && !taken;
	ImGui::BeginDisabled( !ok );
	if( ( UI::ColorButton( ICON_OK "  Make piece", UI::Accent(), ImVec2( 140.0f * UI::Scale(), 0 ) ) || enter ) && ok )
	{
		idBounds bounds;
		idVec3 origin = PlacementPoint();
		if( document.SelectionBounds( bounds ) )
		{
			origin = SnapToGrid( idVec3( ( bounds[0].x + bounds[1].x ) * 0.5f, ( bounds[0].y + bounds[1].y ) * 0.5f, bounds[0].z ) );
		}
		idStr error;
		document.BeginChange( "Make piece" );
		const int anchor = MakePrefab( document, name, origin, error );
		if( anchor >= 0 )
		{
			idList<Selection> items;
			PieceItems( document.Map(), anchor, items );
			SelectItems( items, false );
			document.EndChange();
			Status( va( "Made the piece \"%s\". Place more from Kits > My pieces.", name.c_str() ) );
			Help::Event( "piece.made" );
		}
		else
		{
			document.CancelChange();
			Status( error.c_str() );
		}
		pieceListDirty = true;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if( ImGui::Button( "Cancel" ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) )
	{
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

static void DrawPieceTiles()
{
	RefreshPieceList();
	const bool canMake = document.IsOpen() && !document.selection.IsEmpty() && !document.selection.IsFaces() && !IsEditingPiece();
	if( UI::ToolTile( "makepiece", ICON_PLUS, "Make piece", false, "Make a piece from what's selected: save it to place again and again. Every copy stays linked, so changing the piece changes them all.", canMake, dockTile ) )
	{
		pieceMakeRequest = true;
	}
	for( int i = 0; i < pieceNames.Num(); i++ )
	{
		ImGui::SameLine();
		idStr label = pieceNames[i];
		label.StripPath();
		if( UI::ToolTile( va( "piece:%s", pieceNames[i].c_str() ), ICON_MODE_KITS, label, false, va( "Your piece \"%s\".\nClick to add a copy in the middle of the view, or drag it in.", pieceNames[i].c_str() ), document.IsOpen(), dockTile ) )
		{
			PlacePiece( pieceNames[i], PlacementPoint() );
		}
		DragOut( -1, va( "piece:%s", pieceNames[i].c_str() ) );
	}
}

static int CountCopies( const char* prefab )
{
	int copies = 0;
	const idMapFile* map = document.Map();
	for( int e = 1; map != NULL && e < map->GetNumEntities(); e++ )
	{
		copies += ( IsPrefabAnchor( map->GetEntity( e ) ) && idStr::Icmp( map->GetEntity( e )->epairs.GetString( KEY_PREFAB ), prefab ) == 0 ) ? 1 : 0;
	}
	return copies;
}

static const char* OverrideLabel( const idStr& key )
{
	if( key == "brush" )
	{
		return "Shape and paint";
	}
	if( key == "origin" )
	{
		return "Position";
	}
	if( key == "angle" || key == "rotation" )
	{
		return "Facing";
	}
	if( key == KEY_REMOVED )
	{
		return "Deleted parts";
	}
	if( key == "_color" )
	{
		return "Colour";
	}
	if( key == "light_radius" )
	{
		return "Reach";
	}
	return key.c_str();
}

// the whole copy of a piece is selected: what to do with it. False when it isn't.
static bool DrawPieceInspector()
{
	idMapFile* map = document.Map();
	if( map == NULL )
	{
		return false;
	}
	const Selection primary = document.selection.Primary();
	if( primary.primitive >= 0 || primary.entity <= 0 || !IsPrefabAnchor( map->GetEntity( primary.entity ) ) )
	{
		return false;
	}
	const int anchor = primary.entity;
	idList<Selection> items;
	PieceItems( map, anchor, items );
	for( int i = 0; i < items.Num(); i++ )
	{
		if( !document.selection.Contains( items[i] ) )
		{
			return false;
		}
	}
	if( items.Num() != document.selection.Num() )
	{
		return false;
	}

	idMapEntity* ent = map->GetEntity( anchor );
	const idStr prefab = ent->epairs.GetString( KEY_PREFAB );
	DrawInspectorHeader( ICON_MODE_KITS, ent->epairs.GetString( "name" ), va( "A copy of the piece \"%s\"", prefab.c_str() ),
						 "Copies stay linked to their piece: change the piece and every copy changes. Double-click part of it to change that part in this copy only." );

	if( UI::BeginCard( "piececard", ICON_MODE_KITS, "Piece" ) )
	{
		const int copies = CountCopies( prefab );
		ImGui::TextDisabled( copies == 1 ? "The only copy in this level" : "%d copies in this level", copies );
		ImGui::Spacing();
		const bool nested = ent->epairs.GetString( KEY_OWNER )[0] != '\0';
		ImGui::BeginDisabled( nested || IsEditingPiece() );
		if( UI::ColorButton( ICON_DRAW "  Edit piece", UI::Accent(), ImVec2( -FLT_MIN, 0 ) ) )
		{
			StartPieceEdit( anchor );
			ImGui::EndDisabled();
			UI::EndCard();
			return true;
		}
		ImGui::EndDisabled();
		UI::Tip( "Change the piece right here, with the rest of the level as ghosts. When you're done, save to every copy or to this copy only." );
		if( ImGui::Button( ICON_OPEN "  Open on its own", ImVec2( -FLT_MIN, 0 ) ) )
		{
			OpenMap( PrefabMapName( prefab ) );
			UI::EndCard();
			return true;
		}
		UI::Tip( "Open the piece by itself, like a small level. Copies everywhere update the next time their level opens." );

		// what this copy does differently
		int changed = 0;
		for( int i = 0; i < items.Num(); i++ )
		{
			idStrList keys;
			GetOverrides( map, items[i], keys );
			changed += keys.Num() > 0 ? 1 : 0;
		}
		idStrList removed;
		{
			idStr word;
			const char* list = ent->epairs.GetString( KEY_REMOVED );
			for( const char* c = list; ; c++ )
			{
				if( *c == ' ' || *c == '\0' )
				{
					if( word.Length() > 0 )
					{
						removed.Append( word );
						word.Clear();
					}
					if( *c == '\0' )
					{
						break;
					}
					continue;
				}
				word += *c;
			}
		}
		ImGui::Spacing();
		if( changed > 0 || removed.Num() > 0 )
		{
			ImGui::TextColored( UI::Accent(), "\xe2\x97\x8f" );
			ImGui::SameLine();
			ImGui::TextWrapped( "This copy differs from its piece: %d %s changed%s.", changed, changed == 1 ? "thing" : "things",
								removed.Num() > 0 ? va( ", %d %s deleted", removed.Num(), removed.Num() == 1 ? "part" : "parts" ) : "" );
			if( ImGui::Button( ICON_UNDO "  Revert all" ) )
			{
				document.BeginChange( "Revert copy" );
				for( int i = 0; i < items.Num(); i++ )
				{
					GetOverrides( map, items[i], removed );
					if( removed.Num() > 0 && !( items[i].entity == anchor && items[i].primitive < 0 ) )
					{
						RevertOverrides( map, items[i] );
						PieceItems( map, anchor, items );
					}
				}
				RevertOverrides( map, Selection( anchor ) );
				document.EndChange();
				PieceItems( map, FindAnchor( map, ent->epairs.GetString( "name" ) ), items );
				SelectItems( items, false );
				Status( "This copy matches its piece again" );
				UI::EndCard();
				return true;
			}
			UI::Tip( "Throw away this copy's own changes, so it matches the piece" );
			ImGui::SameLine();
			ImGui::BeginDisabled( nested );
			if( ImGui::Button( ICON_SPARKLES "  Apply to all copies" ) )
			{
				idStr error;
				document.BeginChange( "Apply to piece" );
				const bool ok = WritePrefabFromCopy( map, anchor, error );
				document.EndChange();
				document.ValidateSelection();
				Status( ok ? va( "The piece \"%s\" now looks like this copy, and so do the others", prefab.c_str() ) : error.c_str() );
				pieceListDirty = true;
				ImGui::EndDisabled();
				UI::EndCard();
				return true;
			}
			ImGui::EndDisabled();
			UI::Tip( "Make the piece itself look like this copy, so every copy changes to match" );
		}
		else
		{
			ImGui::TextDisabled( ICON_OK "  Matches its piece" );
		}
		ImGui::Spacing();
		ImGui::BeginDisabled( nested );
		if( ImGui::Button( ICON_UNGROUP "  Unlink" ) )
		{
			document.BeginChange( "Unlink piece" );
			UnlinkPiece( map, anchor );
			document.selection.Clear();
			document.EndChange();
			Status( "That copy is ordinary level things now, no longer linked to its piece" );
			ImGui::EndDisabled();
			UI::EndCard();
			return true;
		}
		ImGui::EndDisabled();
		UI::Tip( "Turn this copy into ordinary things, no longer linked to the piece" );
	}
	UI::EndCard();

	DrawPlacementCard();
	return true;
}

// one part of a copy is selected: say so, and what this copy changes about it
static void DrawPartCard()
{
	idMapFile* map = document.Map();
	const Selection primary = document.selection.Primary();
	if( map == NULL || primary.IsEmpty() || primary.IsFace() )
	{
		return;
	}
	const Selection item = primary.entity > 0 ? Selection( primary.entity ) : primary;
	idList<int> chain;
	AnchorChain( map, item, chain );
	if( chain.Num() == 0 )
	{
		return;
	}
	const idMapEntity* owner = map->GetEntity( chain[0] );
	const idStr prefab = owner->epairs.GetString( KEY_PREFAB );
	const bool editingThis = IsEditingPiece() && chain.FindIndex( PieceEditingAnchor() ) >= 0;

	if( UI::BeginCard( "partcard", ICON_MODE_KITS, "Piece" ) )
	{
		if( editingThis )
		{
			ImGui::TextWrapped( "Part of the piece \"%s\" you're editing. Save it to every copy or to this copy only, above the view.", pieceEditPrefab.c_str() );
			UI::EndCard();
			return;
		}
		ImGui::TextWrapped( "Part of %s, a copy of the piece \"%s\".", owner->epairs.GetString( "name" ), prefab.c_str() );
		idStrList keys;
		GetOverrides( map, item, keys );
		if( keys.Num() == 0 )
		{
			UI::Hint( "Change it and the change is this copy's alone; it's then marked here, with a way to undo it or give it to every copy." );
		}
		else
		{
			ImGui::Spacing();
			ImGui::TextDisabled( "Changed in this copy only:" );
			for( int i = 0; i < keys.Num(); i++ )
			{
				ImGui::TextColored( UI::Accent(), "\xe2\x97\x8f" );
				ImGui::SameLine();
				ImGui::TextUnformatted( OverrideLabel( keys[i] ) );
			}
			if( ImGui::Button( ICON_UNDO "  Revert" ) )
			{
				const idStr name = item.entity > 0 ? map->GetEntity( item.entity )->epairs.GetString( "name" ) : "";
				document.BeginChange( "Revert part" );
				RevertOverrides( map, item );
				document.EndChange();
				document.ValidateSelection();
				Status( "Back to the piece's version" );
			}
			UI::Tip( "Back to how the piece has it" );
			ImGui::SameLine();
			if( ImGui::Button( ICON_SPARKLES "  Apply to all copies" ) )
			{
				idStr error;
				document.BeginChange( "Apply to piece" );
				const bool ok = ApplyOverridesToPiece( map, item, error );
				document.EndChange();
				document.ValidateSelection();
				Status( ok ? va( "Every copy of \"%s\" has this change now", prefab.c_str() ) : error.c_str() );
			}
			UI::Tip( "Put this change into the piece itself, so every copy gets it" );
		}
	}
	UI::EndCard();
}

// while editing a piece in place: what's going on, and the way out
static void DrawPieceEditBanner( const ImGuiDockNode* central )
{
	if( pieceEditing.Length() > 0 && !IsEditingPiece() )
	{
		// its copy went (undone past the start, say)
		pieceEditing.Clear();
		view.focusOwners.Clear();
	}
	if( !IsEditingPiece() )
	{
		return;
	}
	const float pad = 10.0f * UI::Scale();
	// below the view bar, so the two never overlap however narrow the view
	if( BeginOverlay( "##piece_banner", ImVec2( central->Pos.x + central->Size.x * 0.5f, central->Pos.y + pad + ImGui::GetFrameHeight() + 34.0f * UI::Scale() ), ImVec2( 0.5f, 0.0f ), 10.0f * UI::Scale() ) )
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored( UI::Accent(), ICON_MODE_KITS );
		ImGui::SameLine();
		ImGui::Text( "Editing the piece \"%s\"", pieceEditPrefab.c_str() );
		ImGui::SameLine( 0.0f, 12.0f * UI::Scale() );
		if( UI::ColorButton( ICON_OK "  Save to every copy", UI::Good() ) )
		{
			FinishPieceEdit( PIECE_SAVE_ALL );
		}
		UI::Tip( "Change the piece itself: every copy of it, in every level, gets this. Undo puts the old piece back." );
		ImGui::SameLine();
		if( UI::ColorButton( ICON_SELECT "  Save to this copy", UI::Accent() ) )
		{
			FinishPieceEdit( PIECE_SAVE_COPY );
		}
		UI::Tip( "Keep these changes in this copy only. The piece and its other copies stay as they are; the Inspector marks what this copy does differently." );
		ImGui::SameLine();
		if( ImGui::Button( ICON_CLOSE "  Cancel" ) )
		{
			FinishPieceEdit( PIECE_CANCEL );
		}
		UI::Tip( "Undo what you changed and leave the piece as it was" );
	}
	EndOverlay();
}

/*
===============================================================================

	Models (see EditorImport.h)

	A model comes in by dragging its file onto the window, from Kits >
	Bring in a model, or by saving it into the project's import folder. It's
	placed as a "Static detail" thing whose model is the imported file. Its
	source is watched, so saving it again in Blender updates it here (and in a
	paused game) a moment later.

===============================================================================
*/

static bool			modelImportRequest = false;
static char			modelImportPath[1024] = "";
static bool			modelImportPlace = true;
static idStr		modelImportError;
static int			modelViewRebuild = 0;			// frames left to rebuild the view after models reload
static idStr		modelDropPending;				// a dropped file waiting for the mouse to be over the view

// the size a model comes out at in the level
static bool ModelBounds( const char* modelPath, idBounds& bounds )
{
	idRenderModel* model = renderModelManager->FindModel( modelPath );
	if( model == NULL || model->IsDefaultModel() )
	{
		return false;
	}
	bounds = model->Bounds();
	return !bounds.IsCleared();
}

// a "Static detail" thing showing the model, standing on the point
static void PlaceModel( const char* name, const idVec3& point )
{
	const Import::model_t* m = Import::FindByName( name );
	if( m == NULL || !document.IsOpen() )
	{
		return;
	}
	idVec3 origin = point;
	idBounds bounds;
	if( ModelBounds( m->model, bounds ) )
	{
		origin.z -= bounds[0].z;	// its lowest point on the floor
	}
	idDict keys;
	keys.Set( "model", m->model );
	keys.Set( "name", document.Map()->GetUniqueEntityName( m->name ) );
	document.BeginChange( va( "Add %s", m->name.c_str() ) );
	document.AddEntity( "func_static", origin, &keys );
	Help::Event( "model.placed" );
}

static bool ImportModel( const char* osPath, bool place, bool atMouse )
{
	idStr name, message;
	const int before = Import::Models().Num();
	if( !Import::ImportFile( osPath, name, message ) )
	{
		Status( message );
		modelImportError = message;
		return false;
	}
	modelImportError.Clear();
	modelViewRebuild = 3;
	Status( message );
	Help::Event( Import::Models().Num() > before ? "model.imported" : "model.updated" );
	if( place && document.IsOpen() )
	{
		const ImGuiIO& io = ImGui::GetIO();
		PlaceModel( name, atMouse ? PlacementPointAt( io.MousePos.x, io.MousePos.y ) : PlacementPoint() );
	}
	return true;
}

// a file dropped on the window: pictures become materials, models come in as models
static void FileDropped( const char* osPath )
{
	if( Materials::IsPictureFile( osPath ) )
	{
		Materials::QueueDroppedPicture( osPath );
	}
	else
	{
		Import::QueueDroppedFile( osPath );
	}
}

// once a frame: sources that changed, files saved into the import folder, files dropped on the window
static void ModelsUpdate()
{
	if( Import::Poll() )
	{
		modelViewRebuild = 3;
		Help::Event( "model.updated" );
	}
	if( modelViewRebuild > 0 )
	{
		// the models reload at the start of a frame (reloadModels), so rebuild for a few frames
		modelViewRebuild--;
		view.Rebuild();
	}

	idStr dropped;
	while( Import::TakeDroppedFile( dropped ) )
	{
		if( Sys_IsFolder( dropped.c_str() ) == FOLDER_YES )
		{
			AddGamesFolder( dropped );
			continue;
		}
		if( !Import::IsModelFile( dropped ) )
		{
			Status( "Drop .glb, .gltf or .obj models, or .png, .jpg or .tga pictures to make materials." );
			continue;
		}
		// dropped on the view: place it where it landed (the mouse position arrives with the next events)
		modelDropPending = dropped;
	}
	if( modelDropPending.Length() > 0 && !IsPlaying() )
	{
		const bool overPanel = ImGui::IsWindowHovered( ImGuiHoveredFlags_AnyWindow );
		ImportModel( modelDropPending, document.IsOpen(), !overPanel && !planView );
		modelDropPending.Clear();
	}
}

static void DrawImportDialog()
{
	if( modelImportRequest )
	{
		modelImportRequest = false;
		modelImportError.Clear();
		ImGui::OpenPopup( "Bring in a model" );
	}
	ImGui::SetNextWindowSize( ImVec2( 520.0f * UI::Scale(), 0.0f ) );
	if( !ImGui::BeginPopupModal( "Bring in a model", NULL, ImGuiWindowFlags_NoResize ) )
	{
		return;
	}
	UI::Hint( "Bring in a .glb or .gltf (glTF 2) or an .obj file: from Blender, an asset store or anywhere else. Its pictures and materials come with it." );
	ImGui::Spacing();
	UI::FieldLabel( "File" );
	ImGui::SetNextItemWidth( -FLT_MIN - 90.0f * UI::Scale() );
	const bool enter = ImGui::InputTextWithHint( "##modelpath", "Paste or type where the file is", modelImportPath, sizeof( modelImportPath ), ImGuiInputTextFlags_EnterReturnsTrue );
	ImGui::SameLine();
	if( ImGui::Button( ICON_OPEN "  Browse", ImVec2( 84.0f * UI::Scale(), 0 ) ) )
	{
		idStr path;
		if( Import::BrowseForModel( path ) )
		{
			idStr::Copynz( modelImportPath, path.c_str(), sizeof( modelImportPath ) );
		}
		else
		{
			modelImportError = "No file window is available here: paste the file's path, or drag the file onto the editor.";
		}
	}
	ImGui::Checkbox( "Place it in the level", &modelImportPlace );
	if( modelImportError.Length() > 0 )
	{
		ImGui::TextColored( UI::Warn(), ICON_WARNING );
		ImGui::SameLine();
		ImGui::PushTextWrapPos( 0.0f );
		ImGui::TextUnformatted( modelImportError.c_str() );
		ImGui::PopTextWrapPos();
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	ImGui::TextUnformatted( ICON_INFO "  Two quicker ways" );
	UI::Hint( "Drag a model file from your desktop onto the editor: it lands where you drop it.\n"
			  "Or save models into your project's import folder: they come in by themselves, and every time you save them again they update in the editor." );
	if( ImGui::Button( ICON_OPEN "  Open the import folder" ) )
	{
		Import::ShowFolder( Import::ImportFolder() );
	}
	UI::Tip( Import::ImportFolder().c_str() );

	ImGui::Spacing();
	idStr path = modelImportPath;
	path.StripLeading( ' ' );
	path.StripTrailingWhitespace();
	path.StripLeading( '"' );
	path.StripTrailing( '"' );
	const bool ok = path.Length() > 0;
	ImGui::BeginDisabled( !ok );
	if( ( UI::ColorButton( ICON_OK "  Bring it in", UI::Accent(), ImVec2( 140.0f * UI::Scale(), 0 ) ) || enter ) && ok )
	{
		if( ImportModel( path, modelImportPlace, false ) )
		{
			modelImportPath[0] = '\0';
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if( ImGui::Button( "Cancel" ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) )
	{
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

static int CountModelTiles()
{
	return 1 + Import::Models().Num();
}

static void DrawModelTiles()
{
	if( UI::ToolTile( "importmodel", ICON_OPEN, "Bring in", false, "Bring in a model (.glb, .gltf or .obj) from Blender or anywhere else.\nYou can also drag a model file onto the editor, or save it into the project's import folder.", true, dockTile ) )
	{
		modelImportRequest = true;
	}
	const idList<Import::model_t>& models = Import::Models();
	for( int i = 0; i < models.Num(); i++ )
	{
		ImGui::SameLine();
		const idStr tip = va( "The model \"%s\".\nClick to add one in the middle of the view, or drag it in.", models[i].name.c_str() );
		if( UI::ToolTile( va( "model:%s", models[i].name.c_str() ), ICON_SHAPE_BOX, models[i].name, false, tip, document.IsOpen(), dockTile ) )
		{
			PlaceModel( models[i].name, PlacementPoint() );
		}
		DragOut( -1, va( "model:%s", models[i].name.c_str() ) );
	}
}

// the Model card, for things showing a model file
static void DrawModelCard( idMapEntity* ent )
{
	const idStr modelPath = ent->epairs.GetString( "model" );
	const Import::model_t* m = Import::FindByModel( modelPath );
	if( !UI::BeginCard( "model", ICON_SHAPE_BOX, "Model", "The model this thing shows. Models brought in update by themselves when their file is saved again." ) )
	{
		UI::EndCard();
		return;
	}

	idBounds bounds;
	const bool haveBounds = ModelBounds( modelPath, bounds );
	if( m == NULL )
	{
		UI::FieldLabel( "File" );
		ImGui::TextUnformatted( modelPath.c_str() );
		if( !haveBounds )
		{
			ImGui::TextColored( UI::Warn(), ICON_WARNING "  This model file can't be found." );
		}
		UI::EndCard();
		return;
	}

	UI::FieldLabel( "Name" );
	ImGui::TextUnformatted( m->name.c_str() );

	// how big it is, in meters and game units
	if( haveBounds )
	{
		const idVec3 size = bounds[1] - bounds[0];
		UI::FieldLabel( "Size" );
		ImGui::Text( "%.2f x %.2f x %.2f m", size.x * 0.0254f, size.y * 0.0254f, size.z * 0.0254f );
		UI::Tip( va( "%.0f x %.0f x %.0f game units (1 unit is an inch; the player is about 1.8 m tall)", size.x, size.y, size.z ) );
	}

	// what it was made in sets its size
	UI::FieldLabel( "Made in" );
	ImGui::SetNextItemWidth( -FLT_MIN );
	if( ImGui::BeginCombo( "##madein", Import::MadeInLabel( m->madeIn ) ) )
	{
		for( int i = 0; i < Import::MADE_IN_CUSTOM; i++ )
		{
			if( ImGui::Selectable( Import::MadeInLabel( ( Import::madeIn_t )i ), m->madeIn == i ) )
			{
				idStr message;
				Import::SetMadeIn( m->name, ( Import::madeIn_t )i, 0.0f, message );
				Status( message );
				modelViewRebuild = 3;
				Help::Event( "model.size" );
			}
		}
		if( ImGui::Selectable( Import::MadeInLabel( Import::MADE_IN_CUSTOM ), m->madeIn == Import::MADE_IN_CUSTOM ) )
		{
			idStr message;
			Import::SetMadeIn( m->name, Import::MADE_IN_CUSTOM, Import::MadeInScale( m->madeIn, m->customScale ), message );
			Status( message );
			modelViewRebuild = 3;
		}
		ImGui::EndCombo();
	}
	UI::Tip( "glTF files are made in meters. If the model comes out much too big or too small, pick what it was made in." );
	if( m->madeIn == Import::MADE_IN_CUSTOM )
	{
		static float customScale = 1.0f;
		static idStr customFor;
		if( customFor != m->name )
		{
			customFor = m->name;
			customScale = m->customScale;
		}
		UI::FieldLabel( "Scale" );
		ImGui::SetNextItemWidth( -FLT_MIN );
		ImGui::DragFloat( "##customscale", &customScale, 0.05f, 0.001f, 10000.0f, "%.3f units per model unit" );
		if( ImGui::IsItemDeactivatedAfterEdit() )
		{
			idStr message;
			Import::SetMadeIn( m->name, Import::MADE_IN_CUSTOM, customScale, message );
			Status( message );
			modelViewRebuild = 3;
			Help::Event( "model.size" );
		}
	}

	// facing, for models that were made facing another way
	UI::FieldLabel( "Turn" );
	if( ImGui::Button( ICON_TURN_LEFT "##modelturnleft" ) )
	{
		idStr message;
		Import::SetTurn( m->name, m->turn + 90.0f, message );
		Status( message );
		modelViewRebuild = 3;
	}
	UI::Tip( "Turn the model itself a quarter left (every copy). To turn just this one, use Facing above." );
	ImGui::SameLine();
	if( ImGui::Button( ICON_TURN_RIGHT "##modelturnright" ) )
	{
		idStr message;
		Import::SetTurn( m->name, m->turn - 90.0f, message );
		Status( message );
		modelViewRebuild = 3;
	}
	UI::Tip( "Turn the model itself a quarter right (every copy)" );
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled( "%.0f%s", m->turn, "\xc2\xb0" );

	// where it came from
	idStr sourceName = m->source;
	sourceName.StripPath();
	UI::FieldLabel( "From" );
	if( m->sourceMissing )
	{
		ImGui::TextColored( UI::Warn(), ICON_WARNING "  %s (missing)", sourceName.c_str() );
		UI::Tip( va( "%s\n\nThe model stays as it was last brought in. Bring the file back to have it update again.", m->source.c_str() ) );
	}
	else
	{
		ImGui::TextUnformatted( sourceName.c_str() );
		UI::Tip( va( "%s\n\nSave this file again and the model updates by itself.", m->source.c_str() ) );
	}
	if( ImGui::Button( ICON_UNDO "  Update now" ) )
	{
		idStr message;
		Import::Reimport( m->name, message );
		Status( message );
		modelViewRebuild = 3;
	}
	UI::Tip( "Read the file again now (it also happens by itself when the file is saved)" );
	ImGui::SameLine();
	if( ImGui::Button( ICON_OPEN "  Show file" ) )
	{
		idStr folder = m->source;
		const bool absolute = folder.Length() > 1 && ( folder[0] == '/' || folder[1] == ':' );
		if( !absolute )
		{
			folder = fileSystem->RelativePathToOSPath( folder, "fs_basepath" );
		}
		folder.BackSlashesToSlashes();
		folder.StripFilename();
		Import::ShowFolder( folder );
	}

	ImGui::TextDisabled( "%d material%s, %d picture%s", m->materials, m->materials == 1 ? "" : "s", m->textures, m->textures == 1 ? "" : "s" );
	for( int i = 0; i < m->warnings.Num(); i++ )
	{
		ImGui::PushTextWrapPos( 0.0f );
		ImGui::TextColored( UI::Warn(), ICON_INFO " %s", m->warnings[i].c_str() );
		ImGui::PopTextWrapPos();
	}
	UI::EndCard();
}

// Content > Models
static void DrawContentModels()
{
	if( ImGui::Button( ICON_PLUS "  Bring in a model" ) )
	{
		modelImportRequest = true;
	}
	ImGui::SameLine();
	if( ImGui::Button( ICON_OPEN "  Import folder" ) )
	{
		Import::ShowFolder( Import::ImportFolder() );
	}
	UI::Tip( "Models saved into this folder come in by themselves" );
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	Hint( "Double-click to add one to the level." );

	ImGui::BeginChild( "models" );
	const idList<Import::model_t>& models = Import::Models();
	for( int i = 0; i < models.Num(); i++ )
	{
		const Import::model_t& m = models[i];
		if( ImGui::Selectable( va( ICON_SHAPE_BOX "  %s##model%d", m.name.c_str(), i ), false, ImGuiSelectableFlags_AllowDoubleClick ) &&
				ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
		{
			PlaceModel( m.name, PlacementPoint() );
		}
		DragOut( -1, va( "model:%s", m.name.c_str() ) );
		RowType( m.sourceMissing ? "file gone" : ( m.warnings.Num() > 0 ? "notes" : Import::MadeInLabel( m.madeIn ) ) );
	}
	if( models.Num() == 0 )
	{
		Hint( "No models brought in yet. Drag a .glb, .gltf or .obj file onto the editor to start." );
	}
	ImGui::EndChild();
}

/*
-------------------------------------------------------------------------------
	model console commands (for tests and scripting)
-------------------------------------------------------------------------------
*/

static void EditorImport_f( const idCmdArgs& args )
{
	if( args.Argc() < 2 )
	{
		common->Printf( "usage: editorImport <file> [place]\n" );
		return;
	}
	ImportModel( args.Args( 1, args.Argc() >= 3 && idStr::Icmp( args.Argv( args.Argc() - 1 ), "place" ) == 0 ? args.Argc() - 2 : -1 ),
				 args.Argc() >= 3 && idStr::Icmp( args.Argv( args.Argc() - 1 ), "place" ) == 0, false );
}

static void EditorImportSize_f( const idCmdArgs& args )
{
	if( args.Argc() < 3 )
	{
		common->Printf( "usage: editorImportSize <model> <meters|centimeters|units|scale>\n" );
		return;
	}
	idStr message;
	const char* what = args.Argv( 2 );
	Import::madeIn_t madeIn = Import::MADE_IN_CUSTOM;
	if( idStr::Icmp( what, "meters" ) == 0 )
	{
		madeIn = Import::MADE_IN_METERS;
	}
	else if( idStr::Icmp( what, "centimeters" ) == 0 )
	{
		madeIn = Import::MADE_IN_CENTIMETERS;
	}
	else if( idStr::Icmp( what, "units" ) == 0 )
	{
		madeIn = Import::MADE_IN_INCHES;
	}
	Import::SetMadeIn( args.Argv( 1 ), madeIn, madeIn == Import::MADE_IN_CUSTOM ? atof( what ) : 0.0f, message );
	modelViewRebuild = 3;
	common->Printf( "%s\n", message.c_str() );
}

static void EditorImportTurn_f( const idCmdArgs& args )
{
	if( args.Argc() < 3 )
	{
		common->Printf( "usage: editorImportTurn <model> <degrees>\n" );
		return;
	}
	idStr message;
	Import::SetTurn( args.Argv( 1 ), atof( args.Argv( 2 ) ), message );
	modelViewRebuild = 3;
	common->Printf( "%s\n", message.c_str() );
}

static void EditorReimport_f( const idCmdArgs& args )
{
	idStr message;
	Import::Reimport( args.Argv( 1 ), message );
	modelViewRebuild = 3;
	common->Printf( "%s\n", message.c_str() );
}

static void EditorPlaceModel_f( const idCmdArgs& args )
{
	if( args.Argc() < 2 )
	{
		common->Printf( "usage: editorPlaceModel <model> [x y z]\n" );
		return;
	}
	idVec3 point = PlacementPoint();
	if( args.Argc() >= 5 )
	{
		point.Set( atof( args.Argv( 2 ) ), atof( args.Argv( 3 ) ), atof( args.Argv( 4 ) ) );
	}
	PlaceModel( args.Argv( 1 ), point );
}

static void EditorListImports_f( const idCmdArgs& args )
{
	const idList<Import::model_t>& models = Import::Models();
	for( int i = 0; i < models.Num(); i++ )
	{
		const Import::model_t& m = models[i];
		idBounds bounds;
		idStr size = "?";
		if( ModelBounds( m.model, bounds ) )
		{
			size = va( "%.1f %.1f %.1f .. %.1f %.1f %.1f", bounds[0].x, bounds[0].y, bounds[0].z, bounds[1].x, bounds[1].y, bounds[1].z );
		}
		common->Printf( "%s: %s from %s, %s, turn %g, %d materials, %d pictures, %d meshes%s, bounds %s%s\n", m.name.c_str(), m.model.c_str(), m.source.c_str(),
						Import::MadeInLabel( m.madeIn ), m.turn, m.materials, m.textures, m.meshes, m.skinned ? ", skinned" : "", size.c_str(),
						m.sourceMissing ? " (source missing)" : "" );
		for( int j = 0; j < m.warnings.Num(); j++ )
		{
			common->Printf( "    note: %s\n", m.warnings[j].c_str() );
		}
	}
	common->Printf( "%d imported models\n", models.Num() );
}

static void EditorDropFile_f( const idCmdArgs& args )
{
	// what dropping a file on the window does, for tests
	FileDropped( args.Args() );
}

/*
-------------------------------------------------------------------------------
	piece console commands (for tests and scripting)
-------------------------------------------------------------------------------
*/

static void StartPieceEdit( int anchor );

static void EditorMakePiece_f( const idCmdArgs& args )
{
	if( args.Argc() < 2 || document.selection.IsEmpty() )
	{
		common->Printf( "usage: editorMakePiece <name> (makes a piece from the selection)\n" );
		return;
	}
	idBounds bounds;
	idVec3 origin = vec3_origin;
	if( document.SelectionBounds( bounds ) )
	{
		origin = SnapToGrid( idVec3( ( bounds[0].x + bounds[1].x ) * 0.5f, ( bounds[0].y + bounds[1].y ) * 0.5f, bounds[0].z ) );
	}
	idStr error;
	document.BeginChange( "Make piece" );
	const int anchor = MakePrefab( document, args.Argv( 1 ), origin, error );
	if( anchor < 0 )
	{
		document.CancelChange();
		common->Printf( "editorMakePiece: %s\n", error.c_str() );
		return;
	}
	idList<Selection> items;
	PieceItems( document.Map(), anchor, items );
	SelectItems( items, false );
	document.EndChange();
	pieceListDirty = true;
	common->Printf( "made piece %s around %s: %d things\n", args.Argv( 1 ), origin.ToString( 0 ), items.Num() );
}

static void EditorPlacePiece_f( const idCmdArgs& args )
{
	if( args.Argc() < 5 )
	{
		common->Printf( "usage: editorPlacePiece <name> <x> <y> <z> [yaw]\n" );
		return;
	}
	document.BeginChange( "Add piece" );
	const idMat3 axis = idAngles( 0.0f, args.Argc() > 5 ? atof( args.Argv( 5 ) ) : 0.0f, 0.0f ).ToMat3();
	const int anchor = PlacePrefab( document.Map(), args.Argv( 1 ), idVec3( atof( args.Argv( 2 ) ), atof( args.Argv( 3 ) ), atof( args.Argv( 4 ) ) ), axis );
	if( anchor < 0 )
	{
		document.CancelChange();
		common->Printf( "editorPlacePiece: no piece %s\n", args.Argv( 1 ) );
		return;
	}
	idList<Selection> items;
	PieceItems( document.Map(), anchor, items );
	SelectItems( items, false );
	document.EndChange();
	common->Printf( "placed %s: %d things\n", document.Map()->GetEntity( FindAnchor( document.Map(), document.Map()->GetEntity( document.selection.Primary().entity )->epairs.GetString( "name" ) ) )->epairs.GetString( "name" ), items.Num() );
}

// editorSelectPiece <anchor> [part]: a whole copy, or one part of it (a brush part as b<n>)
static void EditorSelectPiece_f( const idCmdArgs& args )
{
	const int anchor = FindAnchor( document.Map(), args.Argv( 1 ) );
	if( anchor < 0 )
	{
		common->Printf( "usage: editorSelectPiece <copy> [part]\n" );
		return;
	}
	idList<Selection> items;
	PieceItems( document.Map(), anchor, items );
	if( args.Argc() < 3 )
	{
		SelectItems( items, false );
		return;
	}
	for( int i = 0; i < items.Num(); i++ )
	{
		const idDict& dict = items[i].primitive >= 0 ? document.Map()->GetEntity( 0 )->GetPrimitive( items[i].primitive )->epairs : document.Map()->GetEntity( items[i].entity )->epairs;
		if( idStr::Cmp( dict.GetString( KEY_OWNER ), args.Argv( 1 ) ) == 0 && idStr::Cmp( dict.GetString( KEY_PART ), args.Argv( 2 ) ) == 0 )
		{
			document.selection.Set( items[i] );
			return;
		}
	}
	common->Printf( "no part %s in %s\n", args.Argv( 2 ), args.Argv( 1 ) );
}

static void EditorEditPiece_f( const idCmdArgs& args )
{
	if( args.Argc() > 1 && idStr::Icmp( args.Argv( 1 ), "done" ) == 0 )
	{
		FinishPieceEdit( PIECE_SAVE_ALL );
	}
	else if( args.Argc() > 1 && idStr::Icmp( args.Argv( 1 ), "copy" ) == 0 )
	{
		FinishPieceEdit( PIECE_SAVE_COPY );
	}
	else if( args.Argc() > 1 && idStr::Icmp( args.Argv( 1 ), "cancel" ) == 0 )
	{
		FinishPieceEdit( PIECE_CANCEL );
	}
	else
	{
		StartPieceEdit( FindAnchor( document.Map(), args.Argv( 1 ) ) );
	}
}

// editorPiecePart <revert|apply>: on the selected part of a copy
static void EditorPiecePart_f( const idCmdArgs& args )
{
	const Selection sel = document.selection.Primary();
	idStr error;
	document.BeginChange( "Piece part" );
	bool ok = true;
	if( idStr::Icmp( args.Argv( 1 ), "apply" ) == 0 )
	{
		ok = ApplyOverridesToPiece( document.Map(), sel, error );
	}
	else
	{
		RevertOverrides( document.Map(), sel );
	}
	document.EndChange();
	document.ValidateSelection();
	common->Printf( ok ? "done\n" : "%s\n", error.c_str() );
}

static void ShowMeFor( const char* target, float seconds );

// for lesson writers: try a "Show me" target (it glows for a minute)
static void EditorShowMe_f( const idCmdArgs& args )
{
	idStr target = args.Args( 1, -1 );
	target.Replace( " ", "" );
	ShowMeFor( target, 60.0f );
}

static void EditorHelp_f( const idCmdArgs& args )
{
	// page ids have dashes, which the console splits off: put the pieces back together
	idStr page = args.Argc() > 1 ? args.Args( 1, -1 ) : "";
	page.Replace( " ", "" );
	Help::Open( page.Length() > 0 ? page.c_str() : NULL );
}

static void EditorDelete_f( const idCmdArgs& args )
{
	document.DeleteSelection();
}

static void EditorDuplicate_f( const idCmdArgs& args )
{
	document.DuplicateSelection( idVec3( atof( args.Argv( 1 ) ), atof( args.Argv( 2 ) ), atof( args.Argv( 3 ) ) ) );
}

// lists the copies of pieces and what each overrides
static void EditorListPieces_f( const idCmdArgs& args )
{
	const idMapFile* map = document.Map();
	for( int e = 1; map != NULL && e < map->GetNumEntities(); e++ )
	{
		const idMapEntity* ent = map->GetEntity( e );
		if( IsPrefabAnchor( ent ) )
		{
			common->Printf( "copy %s of %s at %s owner '%s' removed '%s'\n", ent->epairs.GetString( "name" ), ent->epairs.GetString( KEY_PREFAB ), ent->epairs.GetString( "origin" ),
							ent->epairs.GetString( KEY_OWNER ), ent->epairs.GetString( KEY_REMOVED ) );
		}
		else if( ent->epairs.GetString( KEY_OWNER )[0] != '\0' )
		{
			common->Printf( "  entity %s (%s|%s) at %s overrides '%s'\n", ent->epairs.GetString( "name" ), ent->epairs.GetString( KEY_OWNER ), ent->epairs.GetString( KEY_PART ),
							ent->epairs.GetString( "origin" ), ent->epairs.GetString( KEY_OVERRIDES ) );
		}
	}
	const idMapEntity* world = map != NULL ? map->GetEntity( 0 ) : NULL;
	for( int p = 0; world != NULL && p < world->GetNumPrimitives(); p++ )
	{
		const idMapPrimitive* prim = world->GetPrimitive( p );
		if( prim->epairs.GetString( KEY_OWNER )[0] != '\0' )
		{
			idBounds b;
			PrimitiveBounds( prim, b );
			common->Printf( "  brush %d (%s|%s) %s - %s overrides '%s'\n", p, prim->epairs.GetString( KEY_OWNER ), prim->epairs.GetString( KEY_PART ), b[0].ToString( 0 ), b[1].ToString( 0 ), prim->epairs.GetString( KEY_OVERRIDES ) );
		}
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
		UI::SearchField( "##pickfilter", materialFilter, sizeof( materialFilter ), "Find a material", 260.0f * UI::Scale() );
		ImGui::SameLine();
		if( ImGui::Button( ICON_PLUS "  New" ) )
		{
			ImGui::CloseCurrentPopup();
			RequestNewMaterial();
		}
		UI::Tip( "Make a new material (or drop pictures on the editor)" );
		ImGui::BeginChild( "##picklist", ImVec2( 360.0f * UI::Scale(), 320.0f * UI::Scale() ) );
		const float tile = 64.0f * UI::Scale();
		const int perRow = Max( 1, ( int )( ImGui::GetContentRegionAvail().x / ( tile + ImGui::GetStyle().ItemSpacing.x ) ) );
		int shown = 0;
		const int count = declManager->GetNumDecls( DECL_MATERIAL );
		for( int i = 0; i < count; i++ )
		{
			const idDecl* decl = declManager->DeclByIndex( DECL_MATERIAL, i, false );
			if( decl == NULL || !MaterialListed( decl->GetName() ) || ( materialFilter[0] != '\0' && idStr::FindText( decl->GetName(), materialFilter, false ) < 0 ) )
			{
				continue;
			}
			if( shown++ % perRow != 0 )
			{
				ImGui::SameLine();
			}
			ImGui::PushID( i );
			if( DrawMaterialThumb( decl->GetName(), tile, currentMaterial.Icmp( decl->GetName() ) == 0 ) )
			{
				currentMaterial = decl->GetName();
				ImGui::CloseCurrentPopup();
			}
			UI::Tip( decl->GetName() );
			ImGui::PopID();
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
	if( UI::ToolTile( "editmat", ICON_SETTINGS, "Edit", false, "Change the paint material in the Material panel: pictures, colour, how rough or shiny it is", true, dockTile ) )
	{
		OpenMaterialPanel( currentMaterial );
	}
	ImGui::SameLine();
	if( UI::ToolTile( "newmat", ICON_PLUS, "New", false, "Make a new material. You can also drop pictures on the editor: they become a material.", true, dockTile ) )
	{
		RequestNewMaterial();
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
	DrawPieceTiles();
	TileDivider();
	DrawModelTiles();
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
			return TileRowWidth( CountDefs( false ) + 1 + ( int )pieceNames.Num() + CountModelTiles(), 2 );
	}
}

static idCVar editor_toolsPanel( "editor_toolsPanel", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT, "the tool dock is a panel you can dock anywhere, instead of floating over the view" );
static idCVar editor_toolDockOffset( "editor_toolDockOffset", "0 0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT, "where the tool dock was dragged to, from the bottom middle of the view" );
static const char* WINDOW_TOOLS = ICON_BUILD "  Tools###Tools";

// the tabs and tiles, in the overlay or in the Tools panel
static void DrawToolDockContents()
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
}

// a grip at the dock's top left: drag to move the dock, right-click for where it lives
static void DrawDockGrip()
{
	const float s = UI::Scale();
	const ImVec2 back = ImGui::GetCursorPos();
	ImGui::SetCursorPos( ImVec2( 6.0f * s, 6.0f * s ) );
	const ImVec2 size( 12.0f * s, 22.0f * s );
	ImGui::InvisibleButton( "##dockgrip", size );
	const bool hovered = ImGui::IsItemHovered();
	const ImVec2 min = ImGui::GetItemRectMin();
	if( ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Left, 0.0f ) )
	{
		idVec2 offset;
		sscanf( editor_toolDockOffset.GetString(), "%f %f", &offset.x, &offset.y );
		offset.x += ImGui::GetIO().MouseDelta.x;
		offset.y += ImGui::GetIO().MouseDelta.y;
		editor_toolDockOffset.SetString( va( "%.0f %.0f", offset.x, offset.y ) );
	}
	if( hovered && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
	{
		editor_toolDockOffset.SetString( "0 0" );
	}
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImU32 dot = ImGui::ColorConvertFloat4ToU32( hovered || ImGui::IsItemActive() ? ImVec4( 0.9f, 0.9f, 0.92f, 1.0f ) : UI::Muted() );
	for( int y = 0; y < 3; y++ )
	{
		for( int x = 0; x < 2; x++ )
		{
			draw->AddCircleFilled( ImVec2( min.x + ( 3.0f + x * 6.0f ) * s, min.y + ( 5.0f + y * 6.0f ) * s ), 1.6f * s, dot );
		}
	}
	UI::Tip( "Drag to move the tool dock. Double-click puts it back in the middle; right-click to make it a panel you can dock anywhere." );
	if( ImGui::BeginPopupContextItem( "##dockgripmenu" ) )
	{
		if( ImGui::MenuItem( ICON_DETACH "  Make it a panel" ) )
		{
			editor_toolsPanel.SetBool( true );
		}
		if( ImGui::MenuItem( "      Back to the middle" ) )
		{
			editor_toolDockOffset.SetString( "0 0" );
		}
		ImGui::EndPopup();
	}
	ImGui::SetCursorPos( back );
}

static void DrawToolDock( const ImGuiDockNode* central )
{
	// tiles shrink to fit narrow views; Build has the most: 13, a divider, the spacing and the padding
	const float s = UI::Scale();
	const float avail = central->Size.x - 24.0f * s;
	dockTile = idMath::ClampFloat( 36.0f * s, UI::TileSize(), ( avail - 12.0f * 4.0f * s - 17.0f * s - 16.0f * s ) / 13.0f );

	// as a panel: dockable anywhere, moved by its tab; closing it puts it back over the view
	if( editor_toolsPanel.GetBool() )
	{
		bool open = true;
		ImGui::SetNextWindowSize( ImVec2( 760.0f * s, 150.0f * s ), ImGuiCond_FirstUseEver );
		// the first time, it joins Content and Problems at the bottom
		if( ImGui::FindWindowByName( WINDOW_TOOLS ) == NULL )
		{
			ImGuiWindow* content = ImGui::FindWindowByName( WINDOW_CONTENT_BROWSER );
			if( content != NULL && content->DockId != 0 )
			{
				ImGui::SetNextWindowDockID( content->DockId, ImGuiCond_FirstUseEver );
			}
		}
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 4.0f * s, 6.0f * s ) );
		if( ImGui::Begin( WINDOW_TOOLS, &open, ImGuiWindowFlags_HorizontalScrollbar ) )
		{
			DrawToolDockContents();
		}
		ImGui::End();
		ImGui::PopStyleVar();
		if( !open )
		{
			editor_toolsPanel.SetBool( false );
		}
		// the context bar sits at the bottom of the view instead
		dockTopCenter = ImVec2( central->Pos.x + central->Size.x * 0.5f, central->Pos.y + central->Size.y - 8.0f * s );
		return;
	}

	idVec2 offset( 0, 0 );
	sscanf( editor_toolDockOffset.GetString(), "%f %f", &offset.x, &offset.y );
	ImVec2 pos( central->Pos.x + central->Size.x * 0.5f + offset.x, central->Pos.y + central->Size.y - 12.0f * UI::Scale() + offset.y );
	// keep it reachable when the view shrinks
	pos.x = idMath::ClampFloat( central->Pos.x + 60.0f * s, central->Pos.x + central->Size.x - 60.0f * s, pos.x );
	pos.y = idMath::ClampFloat( central->Pos.y + 120.0f * s, central->Pos.y + central->Size.y, pos.y );
	// the dock keeps the width of its widest tab, so the tabs don't jump about
	float widest = 0.0f;
	for( int i = 0; i < MODE_COUNT; i++ )
	{
		widest = Max( widest, ModeRowWidth( ( editMode_t )i ) );
	}
	ImGui::SetNextWindowSizeConstraints( ImVec2( widest + 16.0f * UI::Scale(), 0.0f ), ImVec2( FLT_MAX, FLT_MAX ) );
	if( BeginOverlay( "##tool_dock", pos, ImVec2( 0.5f, 1.0f ), 14.0f * UI::Scale() ) )
	{
		DrawToolDockContents();
		DrawDockGrip();
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
	if( UI::IconButton( "makepiece", ICON_MODE_KITS, "Make a piece: save the selection to place again and again, with every copy linked", false, !IsEditingPiece() ) )
	{
		pieceMakeRequest = true;
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
	const bool pausedNote = IsPlayPaused() && liveGeometryChanged;
	if( hint.IsEmpty() && !actions && !pausedNote )
	{
		return;
	}

	if( BeginOverlay( "##context_bar", ImVec2( dockTopCenter.x, dockTopCenter.y - 8.0f * UI::Scale() ), ImVec2( 0.5f, 1.0f ), 20.0f * UI::Scale() ) )
	{
		bool first = true;
		if( pausedNote )
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored( UI::Warn(), ICON_INFO "  Brush changes appear when you carry on (F5): the level rebuilds around you." );
			first = false;
			if( !hint.IsEmpty() )
			{
				UI::VerticalDivider();
			}
		}
		if( !hint.IsEmpty() )
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored( UI::Accent(), "%s", ToolInfo( gizmoMode ).icon );
			ImGui::SameLine();
			ImGui::TextUnformatted( hint.c_str() );
			static const char* toolPages[GIZMO_COUNT] = { "tools/move", "tools/turn", "tools/resize", "tools/cut", "tools/corners", "tools/draw", "tools/paint" };
			if( !planView && gizmoMode >= 0 && gizmoMode < GIZMO_COUNT )
			{
				ImGui::SameLine();
				UI::HelpButton( toolPages[gizmoMode], va( "About %s", ToolInfo( gizmoMode ).label ) );
			}
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
	else if( draggingThing.Cmpn( "piece:", 6 ) == 0 )
	{
		label = draggingThing.c_str() + 6;
	}
	else if( draggingThing.Cmpn( "model:", 6 ) == 0 )
	{
		label = draggingThing.c_str() + 6;
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
			else if( draggingThing.Cmpn( "piece:", 6 ) == 0 )
			{
				PlacePiece( draggingThing.c_str() + 6, point );
			}
			else if( draggingThing.Cmpn( "model:", 6 ) == 0 )
			{
				PlaceModel( draggingThing.c_str() + 6, point );
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
	DrawPieceEditBanner( central );
	DrawLiveBanner( central );
	DrawProblemMarkers( central );
	Help::DrawLessonStrip( ImVec2( central->Pos.x + central->Size.x, central->Pos.y ) );
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
	else if( ImGui::IsKeyPressed( ImGuiKey_F1, false ) )
	{
		if( Help::Visible() )
		{
			Help::Visible() = false;
		}
		else
		{
			Help::Open( NULL );
		}
	}
	else if( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_B, false ) )
	{
		BuildDocument();
	}
	else if( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_S, false ) )
	{
		if( IsEditingPiece() )
		{
			FinishPieceEdit( PIECE_SAVE_ALL );	// saving keeps the piece too
		}
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
	if( navigating )
	{
		ImGuiIO& navIO = ImGui::GetIO();
		if( navIO.MouseDown[ImGuiMouseButton_Right] )
		{
			Help::Event( "camera.look" );
			if( ImGui::IsKeyDown( ImGuiKey_W ) || ImGui::IsKeyDown( ImGuiKey_A ) || ImGui::IsKeyDown( ImGuiKey_S ) || ImGui::IsKeyDown( ImGuiKey_D ) ||
					ImGui::IsKeyDown( ImGuiKey_Q ) || ImGui::IsKeyDown( ImGuiKey_E ) )
			{
				Help::Event( "camera.fly" );
			}
		}
		else if( navIO.MouseDown[ImGuiMouseButton_Middle] )
		{
			Help::Event( "camera.pan" );
		}
		else if( navIO.KeyAlt && navIO.MouseDown[ImGuiMouseButton_Left] )
		{
			Help::Event( "camera.orbit" );
		}
	}

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
			if( !io.KeyCtrl && !hit.IsEmpty() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) && !IsEditingPiece() && TopAnchorOf( document.Map(), hit ) >= 0 )
			{
				document.selection.Set( hit );	// just this part of the copy
				Status( "Just this part: what you change now is this copy's alone" );
				Help::Event( "select.part" );
			}
			else
			{
				SelectHit( hit, io.KeyCtrl );
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
	return IsEditorMode() && ( !IsPlaying() || playPaused );
}

bool IsPlayPaused()
{
	return IsEditorMode() && IsPlaying() && playPaused;
}

/*
===============================================================================

	Materials (see EditorMaterials.h)

	The Material panel shows one material: a picture of it, where it's used,
	and, for materials made in the editor, its preset, pictures, look and
	behaviour. Changes are written to its .mtr file when a control is let go,
	the level shows them straight away, and undo covers them. Pictures dropped
	on the editor become materials, or fill the open material's slots when
	they're dropped on the panel.

===============================================================================
*/

static idStr		matDecl;					// the material shown in the panel
static idStr		matName;					// its editor name, when the editor made it
static float		matMakeBumps = 0.5f;		// Make bumps and roughness: how deep
static Materials::settings_t	matSettings;	// the panel's working copy
static int			matLoadedFor = -1;			// reload the working copy when this changes
static int			matLoadCounter = 0;
static bool			matPanelHovered = false;	// last frame: pictures dropped on it fill its slots
static bool			matFocus = false;
static bool			newMaterialRequest = false;
static char			newMaterialName[64];
static int			newMaterialPreset = 0;
static idStr		materialError;

static bool MaterialListed( const char* declName )
{
	return declName[0] != '_' && idStr::Icmpn( declName, "editorpic/", 10 ) != 0 && idStr::Icmpn( declName, "lighteditor/", 12 ) != 0;
}

static void ReloadMaterialPanel()
{
	matLoadCounter++;
}

static void LoadMaterialPanel()
{
	matLoadedFor = matLoadCounter;
	matName = Materials::EditableName( matDecl );
	if( matName.Length() > 0 )
	{
		Materials::Load( matName, matSettings );
	}
}

static void OpenMaterialPanel( const char* declName )
{
	matDecl = declName;
	showMaterialPanel = true;
	matFocus = true;
	ReloadMaterialPanel();
	LoadMaterialPanel();
	Help::Event( "material.open" );
}

static void RequestNewMaterial()
{
	newMaterialRequest = true;
}

// undo put material files back: load whatever changed, and show it
static void MaterialFilesRestored()
{
	declManager->Reload( false );
	ReloadMaterialPanel();
	LoadMaterialPanel();
}

// a material's picture, drawn flat; a click on it returns true
static bool DrawMaterialThumb( const char* declName, float size, bool selected )
{
	const idMaterial* thumb = Materials::Thumbnail( declName );
	const ImVec2 at = ImGui::GetCursorScreenPos();
	ImGui::PushID( declName );
	const bool clicked = ImGui::InvisibleButton( "thumb", ImVec2( size, size ) );
	ImGui::PopID();
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 end( at.x + size, at.y + size );
	draw->AddRectFilled( at, end, IM_COL32( 30, 34, 40, 255 ), 4.0f * UI::Scale() );
	if( thumb != NULL )
	{
		draw->AddImageRounded( ( ImTextureID )thumb, at, end, ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32_WHITE, 4.0f * UI::Scale() );
	}
	else
	{
		const ImVec2 iconSize = ImGui::CalcTextSize( ICON_MATERIAL );
		draw->AddText( ImVec2( at.x + ( size - iconSize.x ) * 0.5f, at.y + ( size - iconSize.y ) * 0.5f ), ImGui::GetColorU32( UI::Muted() ), ICON_MATERIAL );
	}
	if( selected || ImGui::IsItemHovered() )
	{
		draw->AddRect( at, end, ImGui::GetColorU32( selected ? UI::Accent() : UI::Muted() ), 4.0f * UI::Scale(), 0, selected ? 2.0f : 1.0f );
	}
	return clicked;
}

// a picture by its project path, drawn flat
static void DrawPictureThumb( const idStr& picture, float size )
{
	const ImVec2 at = ImGui::GetCursorScreenPos();
	ImGui::Dummy( ImVec2( size, size ) );
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 end( at.x + size, at.y + size );
	draw->AddRectFilled( at, end, IM_COL32( 30, 34, 40, 255 ), 4.0f * UI::Scale() );
	if( picture.Length() > 0 )
	{
		idStr image = picture;
		image.StripFileExtension();
		const idMaterial* mat = declManager->FindMaterial( va( "editorpic/%s", image.c_str() ) );
		draw->AddImageRounded( ( ImTextureID )mat, at, end, ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32_WHITE, 4.0f * UI::Scale() );
	}
	else
	{
		draw->AddRect( at, end, ImGui::GetColorU32( UI::Muted( ) ), 4.0f * UI::Scale(), 0, 1.0f );
		const ImVec2 plus = ImGui::CalcTextSize( ICON_PLUS );
		draw->AddText( ImVec2( at.x + ( size - plus.x ) * 0.5f, at.y + ( size - plus.y ) * 0.5f ), ImGui::GetColorU32( UI::Muted() ), ICON_PLUS );
	}
}

// write the working copy, as one undo step when a level is open
static bool SaveMaterial( const char* label )
{
	if( matName.Length() == 0 )
	{
		return false;
	}
	matSettings.preset = matSettings.preset.Length() > 0 ? matSettings.preset : idStr( "Custom" );
	idStr before, after, error;
	const bool existed = fileSystem->ReadFile( Materials::FilePath( matName ), NULL, NULL ) > 0;
	const bool recordUndo = document.IsOpen() && !document.IsChangeOpen();
	if( recordUndo )
	{
		document.BeginChange( label );
	}
	const bool ok = Materials::Save( matName, matSettings, error, &before, &after );
	if( recordUndo )
	{
		if( ok && before != after )
		{
			document.RecordFileWrite( Materials::FilePath( matName ), existed, before, after );
			document.EndChange();
		}
		else
		{
			document.CancelChange();
		}
	}
	if( !ok )
	{
		materialError = error;
		Status( error );
	}
	else
	{
		materialError.Clear();
		Help::Event( "material.changed" );
	}
	return ok;
}

// pictures go into the open material's slots, by what their names say
static void FillSlotsFromPictures( const idStrList& files )
{
	idStr error;
	int placed = 0;
	for( int i = 0; i < files.Num(); i++ )
	{
		bool known;
		const Materials::slot_t slot = Materials::GuessSlot( files[i], known );
		if( Materials::SetPicture( matName, matSettings, slot, files[i], error ) )
		{
			placed++;
			if( slot == Materials::SLOT_GLOW && matSettings.glow <= 0.0f )
			{
				matSettings.glow = 1.0f;
			}
		}
	}
	if( placed > 0 && SaveMaterial( "Add pictures to material" ) )
	{
		Status( va( "Put %d picture%s into %s.", placed, placed == 1 ? "" : "s", matName.c_str() ) );
	}
	else if( error.Length() > 0 )
	{
		Status( error );
	}
}

static void MaterialsUpdate()
{
	idStrList files;
	if( Materials::TakeDroppedPictures( files ) && !IsPlaying() )
	{
		if( showMaterialPanel && matPanelHovered && matName.Length() > 0 )
		{
			FillSlotsFromPictures( files );
			return;
		}
		idStrList made;
		idStr message;
		if( Materials::CreateFromPictures( files, made, message ) )
		{
			currentMaterial = Materials::DeclName( made[0] );
			OpenMaterialPanel( currentMaterial );
			Help::Event( "material.made" );
		}
		Status( message );
	}
}

static void DrawNewMaterialDialog()
{
	if( newMaterialRequest )
	{
		newMaterialRequest = false;
		idStr::Copynz( newMaterialName, Materials::UniqueName( "new_material" ), sizeof( newMaterialName ) );
		ImGui::OpenPopup( "New material" );
	}
	ImGui::SetNextWindowSize( ImVec2( 440.0f * UI::Scale(), 0.0f ) );
	if( !ImGui::BeginPopupModal( "New material", NULL, ImGuiWindowFlags_NoResize ) )
	{
		return;
	}
	UI::Hint( "Pick what it's like; you can add pictures and change everything after. Quicker still: drop a set of pictures (colour, normal, roughness ...) on the editor and they become a material." );
	ImGui::Spacing();
	UI::FieldLabel( "Name" );
	ImGui::SetNextItemWidth( -FLT_MIN );
	ImGui::InputText( "##newmatname", newMaterialName, sizeof( newMaterialName ) );
	UI::FieldLabel( "Like" );
	int presetCount = 0;
	const Materials::preset_t* presets = Materials::Presets( presetCount );
	ImGui::SetNextItemWidth( -FLT_MIN );
	if( ImGui::BeginCombo( "##newmatpreset", presets[newMaterialPreset].name ) )
	{
		for( int i = 0; i < presetCount; i++ )
		{
			if( ImGui::Selectable( presets[i].name, i == newMaterialPreset ) )
			{
				newMaterialPreset = i;
			}
			UI::Tip( presets[i].hint );
		}
		ImGui::EndCombo();
	}
	Hint( presets[newMaterialPreset].hint );
	ImGui::Spacing();
	if( UI::ColorButton( ICON_PLUS "  Make it", UI::Accent() ) )
	{
		const idStr name = Materials::UniqueName( newMaterialName );
		idStr error;
		if( Materials::Create( name, presets[newMaterialPreset].name, error ) )
		{
			currentMaterial = Materials::DeclName( name );
			OpenMaterialPanel( currentMaterial );
			Status( va( "Made %s. Add pictures in the Material panel, or drop them on it.", name.c_str() ) );
			Help::Event( "material.made" );
			ImGui::CloseCurrentPopup();
		}
		else
		{
			Status( error );
		}
	}
	ImGui::SameLine();
	if( ImGui::Button( "Cancel" ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) )
	{
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

static void DrawMaterialPanel()
{
	// the first time, it joins the Inspector's tabs
	if( ImGui::FindWindowByName( WINDOW_MATERIAL ) == NULL )
	{
		ImGuiWindow* inspector = ImGui::FindWindowByName( WINDOW_INSPECTOR );
		if( inspector != NULL && inspector->DockId != 0 )
		{
			ImGui::SetNextWindowDockID( inspector->DockId, ImGuiCond_FirstUseEver );
		}
	}
	if( matFocus )
	{
		matFocus = false;
		ImGui::SetNextWindowFocus();
	}
	if( !ImGui::Begin( WINDOW_MATERIAL, &showMaterialPanel ) )
	{
		matPanelHovered = false;
		ImGui::End();
		return;
	}
	matPanelHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem );
	if( matDecl.Length() == 0 )
	{
		matDecl = currentMaterial;
	}
	if( matLoadedFor != matLoadCounter )
	{
		LoadMaterialPanel();
	}

	// header: the picture, the name, where it's used
	const float thumb = 72.0f * UI::Scale();
	DrawMaterialThumb( matDecl, thumb );
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::PushFont( UI::Font( UI::FONT_TITLE ) );
	ImGui::TextUnformatted( ShortMaterialName( matDecl ).c_str() );
	ImGui::PopFont();
	ImGui::TextDisabled( "%s", matDecl.c_str() );
	const int uses = Materials::CountUses( document.Map(), matDecl );
	ImGui::TextDisabled( "%s", uses == 0 ? "Not used in this level" : ( uses == 1 ? "On 1 face in this level" : va( "On %d faces in this level", uses ) ) );
	ImGui::EndGroup();
	{
		const float startY = ImGui::GetCursorPosY();
		ImGui::SameLine( ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight() );
		UI::HelpButton( "panels/material", "About the Material panel" );
		ImGui::SetCursorPosY( startY );
	}

	if( ImGui::Button( ICON_PAINT "  Paint with it" ) )
	{
		currentMaterial = matDecl;
		SetMode( MODE_PAINT );
		Status( va( "Painting with %s.", ShortMaterialName( matDecl ).c_str() ) );
	}
	UI::Tip( "Make it the paint material (the Paint tab)" );
	// the buttons wrap when the panel is narrow
	auto nextButton = []( const char* text )
	{
		const float width = ImGui::CalcTextSize( text ).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		const float lastEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
		if( lastEnd + ImGui::GetStyle().ItemSpacing.x + width < ImGui::GetWindowContentRegionMax().x )
		{
			ImGui::SameLine();
		}
	};
	nextButton( ICON_OK "  Put on selection" );
	ImGui::BeginDisabled( document.selection.IsEmpty() );
	if( ImGui::Button( ICON_OK "  Put on selection" ) )
	{
		document.SetMaterial( matDecl );
	}
	ImGui::EndDisabled();
	UI::Tip( "Put it on everything selected: whole brushes, or just the selected faces" );
	nextButton( ICON_PLUS "  New" );
	if( ImGui::Button( ICON_PLUS "  New" ) )
	{
		RequestNewMaterial();
	}
	UI::Tip( "Make a new material" );
	ImGui::Spacing();

	if( matName.Length() == 0 )
	{
		const idMaterial* mat = declManager->FindMaterial( matDecl, false );
		if( mat == NULL || mat->GetState() == DS_DEFAULTED )
		{
			ImGui::TextColored( UI::Warn(), ICON_WARNING );
			ImGui::SameLine();
			ImGui::TextWrapped( "There's no material called %s.", matDecl.c_str() );
		}
		else
		{
			UI::Hint( va( "This material comes from %s, written by hand or made by bringing in a model, so the panel can't change it. Make an editable copy to change it here; the copy uses the same pictures.", mat->GetFileName() ) );
			if( ImGui::Button( ICON_DUPLICATE "  Make an editable copy" ) )
			{
				idStr name, error;
				if( Materials::MakeEditableCopy( matDecl, name, error ) )
				{
					OpenMaterialPanel( Materials::DeclName( name ) );
					Status( va( "Made %s, an editable copy. Put it on surfaces to use it.", name.c_str() ) );
				}
				else
				{
					Status( error );
				}
			}
		}
		ImGui::End();
		return;
	}

	ImGui::BeginChild( "matsettings" );
	bool changed = false;		// one-shot controls: save now
	const char* label = "Change material";

	// what it's like
	if( UI::BeginCard( "matpreset", ICON_SPARKLES, "Like", "Start from what it's like: a preset sets how rough and shiny it is, whether you can see through it, and what it sounds like underfoot." ) )
	{
		int presetCount = 0;
		const Materials::preset_t* presets = Materials::Presets( presetCount );
		ImGui::SetNextItemWidth( -FLT_MIN );
		if( ImGui::BeginCombo( "##preset", matSettings.preset.c_str() ) )
		{
			for( int i = 0; i < presetCount; i++ )
			{
				if( ImGui::Selectable( presets[i].name, matSettings.preset.Icmp( presets[i].name ) == 0 ) )
				{
					Materials::ApplyPreset( matSettings, presets[i].name );
					changed = true;
					label = "Change material preset";
				}
				UI::Tip( presets[i].hint );
			}
			ImGui::EndCombo();
		}
	}
	UI::EndCard();

	// pictures
	if( UI::BeginCard( "matpictures", ICON_MATERIAL, "Pictures", "The pictures it's made of. Drop picture files on this panel to put them in (which goes where is worked out from their names), or choose one for a slot." ) )
	{
		const float pic = 52.0f * UI::Scale();
		for( int i = 0; i < Materials::SLOT_COUNT; i++ )
		{
			const Materials::slot_t slot = ( Materials::slot_t )i;
			ImGui::PushID( i );
			DrawPictureThumb( matSettings.pictures[i], pic );
			UI::Tip( Materials::SlotHint( slot ) );
			ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::TextUnformatted( Materials::SlotLabel( slot ) );
			if( ImGui::SmallButton( matSettings.pictures[i].Length() > 0 ? "Change" : "Choose" ) )
			{
				char path[MAX_OSPATH] = "";
				if( Sys_OpenFileDialog( va( "Choose the %s picture", Materials::SlotLabel( slot ) ), "Pictures|*.png;*.jpg;*.jpeg;*.tga;*.bmp", path, sizeof( path ) ) )
				{
					idStr error;
					if( Materials::SetPicture( matName, matSettings, slot, path, error ) )
					{
						changed = true;
						label = "Choose material picture";
						if( slot == Materials::SLOT_GLOW && matSettings.glow <= 0.0f )
						{
							matSettings.glow = 1.0f;
						}
					}
					else
					{
						Status( error );
					}
				}
			}
			if( matSettings.pictures[i].Length() > 0 )
			{
				ImGui::SameLine();
				if( ImGui::SmallButton( ICON_CLOSE ) )
				{
					matSettings.pictures[i].Clear();
					changed = true;
					label = "Remove material picture";
				}
				UI::Tip( "Take this picture out" );
			}
			ImGui::EndGroup();
			ImGui::PopID();
			if( i % 2 == 0 && ImGui::GetContentRegionAvail().x > pic * 4.5f )
			{
				ImGui::SameLine( ImGui::GetContentRegionAvail().x * 0.5f + ImGui::GetCursorPosX() * 0.5f );
			}
		}

		// one colour picture is enough: the bumps and roughness can be made from it
		if( matSettings.pictures[Materials::SLOT_COLOR].Length() > 0 )
		{
			ImGui::Spacing();
			UI::SectionLabel( "Made from the colour picture" );
			UI::FieldLabel( "Bumps" );
			ImGui::SetNextItemWidth( -FLT_MIN );
			ImGui::SliderFloat( "##makebumps", &matMakeBumps, 0.0f, 1.0f, matMakeBumps < 0.05f ? "Flat" : matMakeBumps < 0.4f ? "Gentle" : matMakeBumps < 0.75f ? "Clear" : "Deep" );
			UI::Tip( "How deep the bumps look: light parts of the picture are read as raised, dark parts as dents" );
			const bool hasNormal = matSettings.pictures[Materials::SLOT_NORMAL].Length() > 0;
			const bool hasRough = matSettings.pictures[Materials::SLOT_ROUGH].Length() > 0;
			if( ImGui::Button( ICON_SPARKLES "  Make bumps and roughness", ImVec2( -FLT_MIN, 0 ) ) )
			{
				idStr error;
				if( Materials::MakeFromColor( matName, matSettings, matMakeBumps, matSettings.roughness, true, true, error ) )
				{
					changed = true;
					label = "Make pictures from colour";
					Status( "Made the bumps and roughness pictures from the colour picture." );
				}
				else
				{
					Status( error );
				}
			}
			UI::Tip( hasNormal || hasRough ? "Replaces the bumps and roughness pictures with ones made from the colour picture (undo brings yours back). The Roughness slider below sets how rough it is overall." :
					 "Makes a bumps (normal) picture and a roughness picture from the colour picture, so one picture gives a full material. The Roughness slider below sets how rough it is overall." );
		}
	}
	UI::EndCard();

	// how it looks
	bool commit = false;		// drags and sliders: save when let go
	if( UI::BeginCard( "matlook", ICON_EYE, "Look", "Colour, how rough or shiny it is, and whether you can see through it." ) )
	{
		UI::FieldLabel( "Tint" );
		ImGui::SetNextItemWidth( -FLT_MIN );
		ImGui::ColorEdit3( "##tint", matSettings.tint.ToFloatPtr(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel );
		commit |= ImGui::IsItemDeactivatedAfterEdit();
		UI::Tip( matSettings.pictures[Materials::SLOT_COLOR].Length() > 0 ? "Tints the colour picture" : "The colour of the surface (there's no colour picture)" );

		UI::FieldLabel( "Roughness" );
		ImGui::BeginDisabled( matSettings.pictures[Materials::SLOT_ROUGH].Length() > 0 );
		ImGui::SetNextItemWidth( -FLT_MIN );
		ImGui::SliderFloat( "##rough", &matSettings.roughness, 0.0f, 1.0f, matSettings.pictures[Materials::SLOT_ROUGH].Length() > 0 ? "from the picture" : "%.2f" );
		commit |= ImGui::IsItemDeactivatedAfterEdit();
		ImGui::EndDisabled();
		UI::Tip( "0 is smooth and shiny, like polished stone; 1 is rough and matte, like chalk" );

		UI::FieldLabel( "Metal" );
		ImGui::BeginDisabled( matSettings.pictures[Materials::SLOT_METAL].Length() > 0 );
		ImGui::SetNextItemWidth( -FLT_MIN );
		ImGui::SliderFloat( "##metal", &matSettings.metal, 0.0f, 1.0f, matSettings.pictures[Materials::SLOT_METAL].Length() > 0 ? "from the picture" : "%.2f" );
		commit |= ImGui::IsItemDeactivatedAfterEdit();
		ImGui::EndDisabled();
		UI::Tip( "1 for bare metal, 0 for everything else (painted metal included)" );

		UI::FieldLabel( "Glow" );
		ImGui::SetNextItemWidth( -FLT_MIN );
		ImGui::SliderFloat( "##glow", &matSettings.glow, 0.0f, 4.0f, matSettings.glow <= 0.0f ? "none" : "%.2f" );
		commit |= ImGui::IsItemDeactivatedAfterEdit();
		UI::Tip( "How bright it glows. It shines without light, but doesn't light the room: add a light for that." );

		UI::FieldLabel( "See-through" );
		static const char* seeLabels[] = { "No", "Cut out (from the colour picture's transparency)", "See-through all over (glass, water)" };
		ImGui::SetNextItemWidth( -FLT_MIN );
		if( ImGui::BeginCombo( "##see", seeLabels[matSettings.transparency] ) )
		{
			for( int i = 0; i < 3; i++ )
			{
				if( ImGui::Selectable( seeLabels[i], matSettings.transparency == i ) )
				{
					matSettings.transparency = i;
					changed = true;
					label = "Change material transparency";
				}
			}
			ImGui::EndCombo();
		}
		if( matSettings.transparency == Materials::SEE_THROUGH )
		{
			UI::FieldLabel( "Opacity" );
			ImGui::SetNextItemWidth( -FLT_MIN );
			ImGui::SliderFloat( "##opacity", &matSettings.opacity, 0.0f, 1.0f, "%.2f" );
			commit |= ImGui::IsItemDeactivatedAfterEdit();
			UI::Hint( "See-through materials aren't lit by lights, so keep them for glass, water and the like." );
		}
	}
	UI::EndCard();

	// how it behaves
	if( UI::BeginCardAt( 1, "matbehave", ICON_SETTINGS, "Behaviour", "What happens when things touch it, and how it casts shadows." ) )
	{
		int surfaceCount = 0;
		const char** surfaces = Materials::SurfaceTypes( surfaceCount );
		UI::FieldLabel( "Sounds like" );
		ImGui::SetNextItemWidth( -FLT_MIN );
		if( ImGui::BeginCombo( "##surface", matSettings.surface.c_str() ) )
		{
			for( int i = 0; i < surfaceCount; i++ )
			{
				if( ImGui::Selectable( surfaces[i], matSettings.surface.Icmp( surfaces[i] ) == 0 ) )
				{
					matSettings.surface = surfaces[i];
					changed = true;
					label = "Change material surface";
				}
			}
			ImGui::EndCombo();
		}
		UI::Tip( "The surface type: which footsteps and impact effects the game uses on it" );
		changed |= ImGui::Checkbox( "Both sides", &matSettings.twoSided );
		UI::Tip( "Draw the back of the surface too, for thin things like leaves, fences and cloth" );
		changed |= ImGui::Checkbox( "Casts shadows", &matSettings.shadows );
		changed |= ImGui::Checkbox( "Solid", &matSettings.solid );
		UI::Tip( "The player and things bump into it. Untick for things you can walk through, like curtains or water" );
	}
	UI::EndCard();

	// Control: the file this material writes, as the engine reads it
	if( UI::BeginCardAt( 2, "matfile", ICON_MAP_FILE, "File", "The material file Daedalus writes from these settings. The first line inside holds the settings; the rest is made from them." ) )
	{
		const idStr path = Materials::FilePath( matName );
		char* text = NULL;
		if( fileSystem->ReadFile( path, ( void** )&text ) > 0 && text != NULL )
		{
			ImGui::InputTextMultiline( "##mtr", text, strlen( text ) + 1, ImVec2( -FLT_MIN, 220.0f * UI::Scale() ), ImGuiInputTextFlags_ReadOnly );
			fileSystem->FreeFile( text );
		}
		if( ImGui::Button( ICON_OPEN "  Show the file" ) )
		{
			idStr folder = fileSystem->RelativePathToOSPath( path, "fs_basepath" );
			folder.StripFilename();
			Sys_OpenFolder( folder );
		}
	}
	UI::EndCard();

	if( materialError.Length() > 0 )
	{
		ImGui::TextColored( UI::Bad(), ICON_ERROR );
		ImGui::SameLine();
		ImGui::TextWrapped( "%s", materialError.c_str() );
	}
	UI::Hint( va( "Saved in %s. Changes show in the level as soon as you let go, and undo puts them back.", Materials::FilePath( matName ).c_str() ) );
	ImGui::EndChild();

	if( changed || commit )
	{
		SaveMaterial( label );		// the preset name stays: it's where the material started
	}
	ImGui::End();
}

/*
-------------------------------------------------------------------------------
	material console commands (for tests and scripting)
-------------------------------------------------------------------------------
*/

// editorNewMaterial <name> [preset words...]
static void EditorNewMaterial_f( const idCmdArgs& args )
{
	if( args.Argc() < 2 )
	{
		common->Printf( "usage: editorNewMaterial <name> [preset]\n" );
		return;
	}
	const idStr name = Materials::UniqueName( args.Argv( 1 ) );
	idStr error;
	if( Materials::Create( name, args.Argc() > 2 ? args.Args( 2 ) : "Stone", error ) )
	{
		OpenMaterialPanel( Materials::DeclName( name ) );
		common->Printf( "made material %s\n", name.c_str() );
	}
	else
	{
		common->Printf( "couldn't make material: %s\n", error.c_str() );
	}
}

// editorEditMaterial <material>: show it in the panel
static void EditorEditMaterial_f( const idCmdArgs& args )
{
	if( args.Argc() != 2 )
	{
		common->Printf( "usage: editorEditMaterial <material>\n" );
		return;
	}
	OpenMaterialPanel( args.Argv( 1 ) );
	common->Printf( "editing %s (%s)\n", matDecl.c_str(), matName.Length() > 0 ? "editable" : "not editable" );
}

// editorMaterialSetting <setting> <value>: change the panel's material, as the panel would
static void EditorMaterialSetting_f( const idCmdArgs& args )
{
	if( args.Argc() < 3 || matName.Length() == 0 )
	{
		common->Printf( "usage: editorMaterialSetting <preset|tint|roughness|metal|glow|seethrough|opacity|twosided|shadows|solid|surface> <value>, with an editable material in the panel\n" );
		return;
	}
	const idStr key = args.Argv( 1 );
	const idStr value = args.Args( 2 );
	Materials::settings_t& s = matSettings;
	if( key.Icmp( "preset" ) == 0 )
	{
		Materials::ApplyPreset( s, value );
	}
	else if( key.Icmp( "tint" ) == 0 )
	{
		sscanf( value.c_str(), "%f %f %f", &s.tint.x, &s.tint.y, &s.tint.z );
	}
	else if( key.Icmp( "roughness" ) == 0 )
	{
		s.roughness = atof( value );
	}
	else if( key.Icmp( "metal" ) == 0 )
	{
		s.metal = atof( value );
	}
	else if( key.Icmp( "glow" ) == 0 )
	{
		s.glow = atof( value );
	}
	else if( key.Icmp( "seethrough" ) == 0 )
	{
		s.transparency = idMath::ClampInt( 0, 2, atoi( value ) );
	}
	else if( key.Icmp( "opacity" ) == 0 )
	{
		s.opacity = atof( value );
	}
	else if( key.Icmp( "twosided" ) == 0 )
	{
		s.twoSided = atoi( value ) != 0;
	}
	else if( key.Icmp( "shadows" ) == 0 )
	{
		s.shadows = atoi( value ) != 0;
	}
	else if( key.Icmp( "solid" ) == 0 )
	{
		s.solid = atoi( value ) != 0;
	}
	else if( key.Icmp( "surface" ) == 0 )
	{
		s.surface = value;
	}
	else
	{
		common->Printf( "no setting called %s\n", key.c_str() );
		return;
	}
	common->Printf( SaveMaterial( "Change material" ) ? "material %s: %s set\n" : "material %s: couldn't set %s\n", matName.c_str(), key.c_str() );
}

// editorMaterialPicture <slot|auto> <file>|none: put a picture into the panel's material
static void EditorMaterialPicture_f( const idCmdArgs& args )
{
	if( args.Argc() < 3 || matName.Length() == 0 )
	{
		common->Printf( "usage: editorMaterialPicture <color|normal|rough|metal|ao|glow|auto> <file>|none, with an editable material in the panel\n" );
		return;
	}
	const idStr file = args.Args( 2 );
	if( idStr::Icmp( args.Argv( 1 ), "auto" ) == 0 )
	{
		idStrList files;
		files.Append( file );
		FillSlotsFromPictures( files );
		return;
	}
	for( int i = 0; i < Materials::SLOT_COUNT; i++ )
	{
		if( idStr::Icmp( args.Argv( 1 ), Materials::SlotKey( ( Materials::slot_t )i ) ) == 0 )
		{
			idStr error;
			if( !Materials::SetPicture( matName, matSettings, ( Materials::slot_t )i, file.Icmp( "none" ) == 0 ? "" : file.c_str(), error ) )
			{
				common->Printf( "couldn't set the picture: %s\n", error.c_str() );
				return;
			}
			common->Printf( SaveMaterial( "Choose material picture" ) ? "material %s: %s picture set\n" : "material %s: couldn't save\n", matName.c_str(), args.Argv( 1 ) );
			return;
		}
	}
	common->Printf( "no picture slot called %s\n", args.Argv( 1 ) );
}

// editorMaterialFromPictures <file> [file...]: as if the pictures were dropped on the editor
static void EditorMaterialFromPictures_f( const idCmdArgs& args )
{
	for( int i = 1; i < args.Argc(); i++ )
	{
		Materials::QueueDroppedPicture( args.Argv( i ) );
	}
}

// editorMaterialInfo [material]: what the editor knows about a material
static void EditorMaterialInfo_f( const idCmdArgs& args )
{
	const idStr decl = args.Argc() > 1 ? idStr( args.Argv( 1 ) ) : matDecl;
	const idMaterial* mat = declManager->FindMaterial( decl, false );
	const idStr name = Materials::EditableName( decl );
	common->Printf( "material %s: %s, %s, %d stages, %d uses\n", decl.c_str(), mat == NULL ? "missing" : ( mat->GetState() == DS_DEFAULTED ? "defaulted" : "loaded" ),
					name.Length() > 0 ? "editable" : "not editable", mat != NULL ? mat->GetNumStages() : 0, Materials::CountUses( document.Map(), decl ) );
	if( name.Length() > 0 )
	{
		Materials::settings_t s;
		Materials::Load( name, s );
		common->Printf( "  preset %s, tint %.2f %.2f %.2f, roughness %.2f, metal %.2f, glow %.2f, seethrough %d, opacity %.2f, twosided %d, shadows %d, solid %d, surface %s\n",
						s.preset.c_str(), s.tint.x, s.tint.y, s.tint.z, s.roughness, s.metal, s.glow, s.transparency, s.opacity, s.twoSided, s.shadows, s.solid, s.surface.c_str() );
		for( int i = 0; i < Materials::SLOT_COUNT; i++ )
		{
			const idStr& pic = s.pictures[i];
			common->Printf( "  %s picture: %s%s\n", Materials::SlotKey( ( Materials::slot_t )i ), pic.Length() > 0 ? pic.c_str() : "none",
							pic.Length() > 0 && fileSystem->ReadFile( pic, NULL, NULL ) <= 0 ? " (MISSING)" : "" );
		}
	}
	if( mat != NULL )
	{
		common->Printf( "  surface type %d, two-sided %d, casts shadows %d, translucent %d\n", ( int )mat->GetSurfaceType(), mat->GetCullType() == CT_TWO_SIDED, mat->SurfaceCastsShadow(), mat->Coverage() == MC_TRANSLUCENT );
	}
}

static void EditorMaterialCopy_f( const idCmdArgs& args )
{
	if( args.Argc() != 2 )
	{
		common->Printf( "usage: editorMaterialCopy <material>\n" );
		return;
	}
	idStr name, error;
	if( Materials::MakeEditableCopy( args.Argv( 1 ), name, error ) )
	{
		OpenMaterialPanel( Materials::DeclName( name ) );
		common->Printf( "made material %s, a copy of %s\n", name.c_str(), args.Argv( 1 ) );
	}
	else
	{
		common->Printf( "couldn't copy: %s\n", error.c_str() );
	}
}

// editorMaterialMake [bumps 0..1]: make the open material's bumps and roughness from its colour picture
static void EditorMaterialMake_f( const idCmdArgs& args )
{
	if( matName.Length() == 0 )
	{
		common->Printf( "No editable material is open.\n" );
		return;
	}
	idStr error;
	if( !Materials::MakeFromColor( matName, matSettings, args.Argc() > 1 ? atof( args.Argv( 1 ) ) : matMakeBumps, matSettings.roughness, true, true, error ) )
	{
		common->Printf( "Couldn't make the pictures: %s\n", error.c_str() );
		return;
	}
	SaveMaterial( "Make pictures from colour" );
	common->Printf( "Made from colour: %s %s\n", matSettings.pictures[Materials::SLOT_NORMAL].c_str(), matSettings.pictures[Materials::SLOT_ROUGH].c_str() );
}

static void RegisterMaterialCommands()
{
	cmdSystem->AddCommand( "editorMaterialMake", EditorMaterialMake_f, CMD_FL_TOOL, "makes the open material's bumps and roughness pictures from its colour picture" );
	cmdSystem->AddCommand( "editorNewMaterial", EditorNewMaterial_f, CMD_FL_SYSTEM, "makes a material: editorNewMaterial <name> [preset]" );
	cmdSystem->AddCommand( "editorEditMaterial", EditorEditMaterial_f, CMD_FL_SYSTEM, "shows a material in the Material panel" );
	cmdSystem->AddCommand( "editorMaterialSetting", EditorMaterialSetting_f, CMD_FL_SYSTEM, "changes a setting of the panel's material" );
	cmdSystem->AddCommand( "editorMaterialPicture", EditorMaterialPicture_f, CMD_FL_SYSTEM, "puts a picture into the panel's material: editorMaterialPicture <slot|auto> <file>|none" );
	cmdSystem->AddCommand( "editorMaterialFromPictures", EditorMaterialFromPictures_f, CMD_FL_SYSTEM, "makes materials from pictures, as if they were dropped on the editor" );
	cmdSystem->AddCommand( "editorMaterialInfo", EditorMaterialInfo_f, CMD_FL_SYSTEM, "prints what the editor knows about a material" );
	cmdSystem->AddCommand( "editorMaterialCopy", EditorMaterialCopy_f, CMD_FL_SYSTEM, "makes an editable copy of a material" );
}

/*
===============================================================================

	Live-edit status

	While Play is paused, a strip over the view says what the changes made so
	far did to the running game, and the Inspector says it for the selection.

===============================================================================
*/

static void DrawLiveBanner( const ImGuiDockNode* central )
{
	if( !IsPlaying() || !playPaused || playReloading )
	{
		return;
	}
	const float pad = 10.0f * UI::Scale();
	float y = central->Pos.y + pad + ImGui::GetFrameHeight() + 34.0f * UI::Scale() + ( IsEditingPiece() ? 52.0f * UI::Scale() : 0.0f );
	// below the lesson card, when one is showing
	const ImGuiWindow* lesson = ImGui::FindWindowByName( "##lessonstrip" );
	if( lesson != NULL && lesson->WasActive )
	{
		y = Max( y, lesson->Pos.y + lesson->Size.y + pad * 0.6f );
	}
	if( BeginOverlay( "##live_banner", ImVec2( central->Pos.x + central->Size.x * 0.5f, y ), ImVec2( 0.5f, 0.0f ), 10.0f * UI::Scale() ) )
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored( UI::Warn(), ICON_PAUSE );
		ImGui::SameLine();
		ImGui::TextUnformatted( "Paused" );
		int inGame, nextPlay, rebuild;
		LiveCounts( inGame, nextPlay, rebuild );
		if( liveChanges.Num() == 0 )
		{
			ImGui::SameLine( 0.0f, 12.0f * UI::Scale() );
			ImGui::TextDisabled( "Changes you make go straight into the game where they can." );
		}
		else
		{
			const int counts[3] = { inGame, nextPlay, rebuild };
			const liveTag_t tags[3] = { LIVE_IN_GAME, LIVE_NEXT_PLAY, LIVE_REBUILD };
			for( int i = 0; i < 3; i++ )
			{
				if( counts[i] == 0 )
				{
					continue;
				}
				ImGui::SameLine( 0.0f, 14.0f * UI::Scale() );
				ImGui::TextColored( LiveTagColor( tags[i] ), "%s", LiveTagIcon( tags[i] ) );
				ImGui::SameLine();
				static const char* phrases[3] = { "in the game", "next time you play", "rebuild when you resume" };
				ImGui::Text( "%d %s", counts[i], phrases[i] );
			}
			ImGui::SameLine( 0.0f, 12.0f * UI::Scale() );
			if( ImGui::SmallButton( liveChangesOpen ? ICON_CHEVRON_DOWN "  Hide" : ICON_CHEVRON_RIGHT "  Changes" ) )
			{
				liveChangesOpen = !liveChangesOpen;
			}
			UI::Tip( "What each change did to the running game" );
			if( liveChangesOpen && ImGui::BeginTable( "livechanges", 3, ImGuiTableFlags_SizingFixedFit ) )
			{
				for( int i = 0; i < liveChanges.Num() && i < 12; i++ )
				{
					const liveChange_t& c = liveChanges[i];
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex( 0 );
					ImGui::TextColored( LiveTagColor( c.tag ), "%s", LiveTagIcon( c.tag ) );
					ImGui::TableSetColumnIndex( 1 );
					ImGui::TextUnformatted( c.name == "worldspawn" ? "The level" : c.name.c_str() );
					ImGui::TableSetColumnIndex( 2 );
					ImGui::TextDisabled( "%s: %s", LiveTagLabel( c.tag ), c.what.c_str() );
				}
				ImGui::EndTable();
				if( liveChanges.Num() > 12 )
				{
					ImGui::TextDisabled( "...and %d more", liveChanges.Num() - 12 );
				}
			}
			if( rebuild > 0 )
			{
				ImGui::TextDisabled( "Resume (F5) rebuilds the level and puts you back where you were." );
			}
		}
	}
	EndOverlay();
}

// the Inspector's line for the selection, while paused
static void DrawLiveSelection()
{
	if( !IsPlaying() || !playPaused )
	{
		return;
	}
	const Selection sel = document.selection.Primary();
	const idMapFile* map = document.Map();
	if( map == NULL || sel.entity < 0 || sel.entity >= map->GetNumEntities() )
	{
		return;
	}
	const idStr name = sel.entity == 0 ? idStr( "worldspawn" ) : idStr( map->GetEntity( sel.entity )->epairs.GetString( "name" ) );
	const liveChange_t* change = FindLiveChange( name );
	if( change == NULL )
	{
		return;
	}
	ImGui::TextColored( LiveTagColor( change->tag ), "%s", LiveTagIcon( change->tag ) );
	ImGui::SameLine();
	ImGui::PushTextWrapPos( 0.0f );
	ImGui::Text( "%s: %s.", LiveTagLabel( change->tag ), change->what.c_str() );
	ImGui::PopTextWrapPos();
	ImGui::Spacing();
}

// editorLiveChanges: list what the changes made while paused did
static void EditorLiveChanges_f( const idCmdArgs& args )
{
	if( args.Argc() > 1 && idStr::Icmp( args.Argv( 1 ), "show" ) == 0 )
	{
		liveChangesOpen = true;
	}
	if( playPaused )
	{
		LiveSync();
	}
	for( int i = 0; i < liveChanges.Num(); i++ )
	{
		common->Printf( "live: %s %s: %s\n", LiveTagCode( liveChanges[i].tag ), liveChanges[i].name.c_str(), liveChanges[i].what.c_str() );
	}
	int inGame, nextPlay, rebuild;
	LiveCounts( inGame, nextPlay, rebuild );
	common->Printf( "live changes: %d in the game, %d next time you play, %d rebuild\n", inGame, nextPlay, rebuild );
}

// editorWhenPlaying <command>: run it once the game is up and the player is in (for tests)
static void EditorWhenPlaying_f( const idCmdArgs& args )
{
	whenPlayingCommand = args.Args();
}

/*
===============================================================================

	Problems (see EditorProblems.h)

	The open level is checked whenever it changes (and twice a second, so
	builds and model imports show up). Each problem names what's wrong, where,
	and what to do; a click on it selects and frames the thing. Problems also
	show in the Outliner, as markers in the view, and on the Inspector. Play
	asks first when the level has errors. "Check every level" reads every map
	in the project.

===============================================================================
*/

static idList<Problems::problem_t>	problems;		// the open level's, the last build's included
static int			problemsRevision = -1;
static int			problemsTime = 0;
static idStr		problemsLevel;
static idList<int>	entityProblem;					// worst warning or error per entity (a piece's parts mark its copy too)
static idList<int>	entityProblemOwn;				// the same, for problems with that entity itself (markers)
static idList<int>	worldProblem;					// the same for the level's own brushes
static int			problemErrors = 0, problemWarnings = 0, problemNotes = 0;

struct levelCheck_t
{
	idStr							map;
	idList<Problems::problem_t>		problems;
};
static idList<levelCheck_t>	projectCheck;
static bool			projectChecked = false;
static bool			playErrorsAsk = false;			// open the "Play with errors?" question
static bool			playErrorsFromCamera = false;

static ImVec4 SeverityColor( int severity )
{
	return severity == Problems::SEV_ERROR ? UI::Bad() : ( severity == Problems::SEV_WARNING ? UI::Warn() : UI::Muted() );
}

static void WorstOf( idList<int>& list, int index, int severity )
{
	if( index >= 0 && index < list.Num() && severity > list[index] )
	{
		list[index] = severity;
	}
}

static void RefreshProblems( bool force )
{
	if( !document.IsOpen() )
	{
		problems.Clear();
		entityProblem.Clear();
		entityProblemOwn.Clear();
		worldProblem.Clear();
		problemErrors = problemWarnings = problemNotes = 0;
		problemsLevel.Clear();
		return;
	}
	const int now = Sys_Milliseconds();
	if( !force && document.Revision() == problemsRevision && problemsLevel.Icmp( document.Name() ) == 0 && now - problemsTime < 500 )
	{
		return;
	}
	problemsRevision = document.Revision();
	problemsTime = now;
	problemsLevel = document.Name();

	const idMapFile* map = document.Map();
	const bool isPiece = idStr::Icmpn( document.Name(), PREFAB_FOLDER "/", idStr::Length( PREFAB_FOLDER ) + 1 ) == 0;
	Problems::CheckLevel( map, isPiece ? Problems::PIECE : Problems::LEVEL, problems );

	// what the last build said, while it's about this level
	if( buildHasResult && buildResultMap.Icmp( document.Name() ) == 0 && !BuildRunning() )
	{
		for( int i = 0; i < buildProblems.Num() && i < 50; i++ )
		{
			const bool leak = buildLeaked && idStr::Icmpn( buildProblems[i], "The level leaks", 15 ) == 0;
			Problems::problem_t& p = problems.Alloc();
			p = Problems::problem_t();
			p.severity = ( i < buildProblemIsError.Num() && buildProblemIsError[i] ) ? Problems::SEV_ERROR : Problems::SEV_WARNING;
			p.code = leak ? "leak" : "build";
			p.text = va( "Build: %s", buildProblems[i].c_str() );
			p.next = leak ? "Show draws a red line from inside the level, out through the gap: close that gap, then build again."
					 : "The build log has the details. Build again (Ctrl+B) once it's sorted.";
			p.fix = leak ? Problems::FIX_SHOW_LEAK : Problems::FIX_BUILD_LOG;
		}
		if( buildProblems.Num() > 50 )
		{
			Problems::problem_t& p = problems.Alloc();
			p = Problems::problem_t();
			p.severity = Problems::SEV_NOTE;
			p.code = "build";
			p.text = va( "Build: %d more in the build log.", buildProblems.Num() - 50 );
			p.next = "Open the build log to see them all.";
			p.fix = Problems::FIX_BUILD_LOG;
		}
	}
	if( modelImportError.Length() > 0 )
	{
		Problems::problem_t& p = problems.Alloc();
		p = Problems::problem_t();
		p.severity = Problems::SEV_WARNING;
		p.code = "import_failed";
		p.text = modelImportError;
		p.next = "Check that the file opens in Blender (or another viewer), then bring it in again.";
	}
	Problems::Sort( problems );
	Problems::Count( problems, problemErrors, problemWarnings, problemNotes );

	// where they are, for the Outliner and the markers (notes stay in the panel)
	entityProblem.SetNum( map->GetNumEntities() );
	entityProblemOwn.SetNum( map->GetNumEntities() );
	worldProblem.SetNum( map->GetEntity( 0 )->GetNumPrimitives() );
	for( int i = 0; i < entityProblem.Num(); i++ )
	{
		entityProblem[i] = entityProblemOwn[i] = -1;
	}
	for( int i = 0; i < worldProblem.Num(); i++ )
	{
		worldProblem[i] = -1;
	}
	for( int i = 0; i < problems.Num(); i++ )
	{
		const Problems::problem_t& p = problems[i];
		if( p.severity == Problems::SEV_NOTE || p.where.entity < 0 || p.where.entity >= map->GetNumEntities() )
		{
			continue;
		}
		if( p.where.entity == 0 )
		{
			WorstOf( worldProblem, p.where.primitive, p.severity );
			continue;
		}
		WorstOf( entityProblem, p.where.entity, p.severity );
		WorstOf( entityProblemOwn, p.where.entity, p.severity );
		WorstOf( entityProblem, TopAnchorOf( map, Selection( p.where.entity ) ), p.severity );
	}
}

static int EntityProblemSeverity( int entity )
{
	return ( entity >= 0 && entity < entityProblem.Num() ) ? entityProblem[entity] : -1;
}

// one of the level's own brushes; -1 for the worst of them
static int WorldProblemSeverity( int primitive )
{
	if( primitive >= 0 )
	{
		return primitive < worldProblem.Num() ? worldProblem[primitive] : -1;
	}
	int worst = -1;
	for( int i = 0; i < worldProblem.Num(); i++ )
	{
		worst = Max( worst, worldProblem[i] );
	}
	return worst;
}

static int PlayErrorCount();

static int LevelErrorCount()
{
	RefreshProblems( true );
	return PlayErrorCount();
}

// errors in the list as it stands
static int PlayErrorCount()
{
	int errors = 0;
	for( int i = 0; i < problems.Num(); i++ )
	{
		// the last build's errors don't count: Play builds again
		if( problems[i].severity == Problems::SEV_ERROR && problems[i].code != "build" && problems[i].code != "leak" )
		{
			errors++;
		}
	}
	return errors;
}

// does this problem concern the thing at (entity, primitive)? a piece copy owns its parts' problems
static bool ProblemIsAbout( const Problems::problem_t& p, int entity, int primitive )
{
	const idMapFile* map = document.Map();
	if( map == NULL || p.where.entity < 0 || entity < 0 )
	{
		return false;
	}
	if( p.where.entity == entity )
	{
		return primitive < 0 || p.where.primitive < 0 || p.where.primitive == primitive;
	}
	return primitive < 0 && entity < map->GetNumEntities() && IsPrefabAnchor( map->GetEntity( entity ) ) && TopAnchorOf( map, Selection( p.where.entity ) ) == entity;
}

static void ProblemTooltip( int entity, int primitive )
{
	ImGui::BeginTooltip();
	ImGui::PushTextWrapPos( 380.0f * UI::Scale() );
	int shown = 0;
	for( int i = 0; i < problems.Num() && shown < 4; i++ )
	{
		if( problems[i].severity != Problems::SEV_NOTE && ProblemIsAbout( problems[i], entity, primitive ) )
		{
			ImGui::TextColored( SeverityColor( problems[i].severity ), "%s", Problems::SeverityIcon( problems[i].severity ) );
			ImGui::SameLine();
			ImGui::TextUnformatted( problems[i].text );
			shown++;
		}
	}
	ImGui::TextDisabled( "Select it to see how to fix it in the Inspector." );
	ImGui::PopTextWrapPos();
	ImGui::EndTooltip();
}

// a row's type, right-aligned, with a problem icon in front when the thing has one;
// when the name leaves no room, the type gives way and the icon stays
static void RowTypeProblem( const char* label, const char* text, int severity, int entity, int primitive )
{
	const float gap = 6.0f * UI::Scale();
	const float labelEnd = ImGui::GetItemRectMin().x - ImGui::GetWindowPos().x + ImGui::GetScrollX() + ImGui::CalcTextSize( label, NULL, true ).x + gap;
	const float right = ImGui::GetWindowContentRegionMax().x - 4.0f * UI::Scale();
	const char* icon = severity >= 0 ? Problems::SeverityIcon( ( Problems::severity_t )severity ) : NULL;
	const float iconWidth = icon != NULL ? ImGui::CalcTextSize( icon ).x : 0.0f;
	bool showText = text[0] != '\0';
	float width = iconWidth + ( showText ? ImGui::CalcTextSize( text ).x + ( icon != NULL ? gap : 0.0f ) : 0.0f );
	if( showText && right - width < labelEnd )
	{
		showText = false;
		width = iconWidth;
	}
	if( width <= 0.0f )
	{
		return;
	}
	ImGui::SameLine( Max( labelEnd, right - width ) );
	if( icon != NULL )
	{
		ImGui::TextColored( SeverityColor( severity ), "%s", icon );
		if( ImGui::IsItemHovered() )
		{
			ProblemTooltip( entity, primitive );
		}
		if( showText )
		{
			ImGui::SameLine( 0.0f, gap );
		}
	}
	if( showText )
	{
		ImGui::TextDisabled( "%s", text );
	}
}

static idStr FixLabel( const Problems::problem_t& p )
{
	switch( p.fix )
	{
		case Problems::FIX_ADD_START:
			return ICON_PLUS "  Add a player start";
		case Problems::FIX_MODEL_NAME:
			return ICON_OK "  Fix";
		case Problems::FIX_RENAME:
			return ICON_OK "  Rename it";
		case Problems::FIX_REMOVE_KEY:
			return ICON_DELETE "  Remove the link";
		case Problems::FIX_USE_MATERIAL:
			return va( ICON_PAINT "  Paint with %s", ShortMaterialName( currentMaterial ).c_str() );
		case Problems::FIX_UNLINK_PIECE:
			return ICON_UNGROUP "  Unlink";
		case Problems::FIX_SHOW_LEAK:
			return ICON_FOCUS "  Show the leak";
		case Problems::FIX_BUILD_LOG:
			return ICON_BOOK "  Build log";
		default:
			return "";
	}
}

static bool ProblemEntityValid( const Problems::problem_t& p )
{
	const idMapFile* map = document.Map();
	return map != NULL && p.where.entity >= 0 && p.where.entity < map->GetNumEntities() &&
		   ( p.where.primitive < 0 || p.where.primitive < map->GetEntity( p.where.entity )->GetNumPrimitives() );
}

// carry out a problem's fix; true when the level changed
static bool ApplyFix( const Problems::problem_t problem )		// a copy: fixing changes the list
{
	idMapFile* map = document.Map();
	if( map == NULL )
	{
		return false;
	}
	idMapEntity* ent = ProblemEntityValid( problem ) ? map->GetEntity( problem.where.entity ) : NULL;
	switch( problem.fix )
	{
		case Problems::FIX_ADD_START:
			PlaceThing( "info_player_start" );
			break;
		case Problems::FIX_MODEL_NAME:
			if( ent == NULL )
			{
				return false;
			}
			document.BeginChange( "Fix model name" );
			ent->epairs.Set( "model", ent->epairs.GetString( "name" ) );
			document.EndChange();
			break;
		case Problems::FIX_RENAME:
		{
			if( ent == NULL )
			{
				return false;
			}
			const idStr name = map->GetUniqueEntityName( problem.subject );
			document.BeginChange( "Rename" );
			const bool brushModel = ent->GetNumPrimitives() > 0 && idStr::Icmp( ent->epairs.GetString( "model" ), ent->epairs.GetString( "name" ) ) == 0;
			ent->epairs.Set( "name", name );
			if( brushModel )
			{
				ent->epairs.Set( "model", name );
			}
			document.EndChange();
			Status( va( "Renamed it %s", name.c_str() ) );
			break;
		}
		case Problems::FIX_REMOVE_KEY:
			if( ent == NULL )
			{
				return false;
			}
			document.BeginChange( "Remove link" );
			ent->epairs.Delete( problem.subject );
			document.EndChange();
			break;
		case Problems::FIX_USE_MATERIAL:
		{
			int faces = 0;
			document.BeginChange( "Paint over a missing material" );
			for( int e = 0; e < map->GetNumEntities(); e++ )
			{
				idMapEntity* owner = map->GetEntity( e );
				for( int p = 0; p < owner->GetNumPrimitives(); p++ )
				{
					idMapPrimitive* prim = owner->GetPrimitive( p );
					if( prim->GetType() == idMapPrimitive::TYPE_BRUSH )
					{
						idMapBrush* brush = static_cast<idMapBrush*>( prim );
						for( int s = 0; s < brush->GetNumSides(); s++ )
						{
							if( idStr::Icmp( brush->GetSide( s )->GetMaterial(), problem.subject ) == 0 )
							{
								brush->GetSide( s )->SetMaterial( currentMaterial );
								faces++;
							}
						}
					}
					else if( prim->GetType() == idMapPrimitive::TYPE_PATCH && idStr::Icmp( static_cast<idMapPatch*>( prim )->GetMaterial(), problem.subject ) == 0 )
					{
						static_cast<idMapPatch*>( prim )->SetMaterial( currentMaterial );
						faces++;
					}
				}
			}
			if( faces == 0 )
			{
				document.CancelChange();
				return false;
			}
			document.EndChange();
			view.Rebuild();
			Status( va( "Painted %d face%s with %s", faces, faces == 1 ? "" : "s", ShortMaterialName( currentMaterial ).c_str() ) );
			break;
		}
		case Problems::FIX_UNLINK_PIECE:
			if( ent == NULL )
			{
				return false;
			}
			document.BeginChange( "Unlink piece" );
			UnlinkPiece( map, problem.where.entity );
			document.selection.Clear();
			document.EndChange();
			break;
		case Problems::FIX_SHOW_LEAK:
			ShowLeak();
			return false;
		case Problems::FIX_BUILD_LOG:
			showBuildLog = true;
			return false;
		default:
			return false;
	}
	RefreshProblems( true );
	return true;
}

// select the thing at fault and look at it
static void ShowProblem( const Problems::problem_t& p )
{
	if( p.fix == Problems::FIX_SHOW_LEAK )
	{
		ShowLeak();
		return;
	}
	if( !ProblemEntityValid( p ) || ( p.where.entity == 0 && p.where.primitive < 0 ) )
	{
		return;
	}
	document.selection.Set( p.where );
	FocusSelection();
}

static bool ProblemCanShow( const Problems::problem_t& p )
{
	return p.fix == Problems::FIX_SHOW_LEAK || ( ProblemEntityValid( p ) && !( p.where.entity == 0 && p.where.primitive < 0 ) );
}

// one problem: icon, what's wrong (click to show it), what to do, and the buttons
static bool DrawProblemRow( int index, const Problems::problem_t& p, bool live )
{
	bool changed = false;
	ImGui::PushID( index );
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex( 0 );
	ImGui::TextColored( SeverityColor( p.severity ), "%s", Problems::SeverityIcon( p.severity ) );
	if( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "%s", p.severity == Problems::SEV_ERROR ? "Error: the game will break, or this won't be there at all" :
						   p.severity == Problems::SEV_WARNING ? "Warning: it'll look or behave wrong" : "Note: worth knowing; nothing is broken" );
	}
	ImGui::TableSetColumnIndex( 1 );
	ImGui::PushTextWrapPos( 0.0f );
	const bool canShow = live && ProblemCanShow( p );
	ImGui::TextUnformatted( p.text );
	if( canShow && ImGui::IsItemHovered() )
	{
		ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
		ImGui::GetWindowDrawList()->AddLine( ImVec2( ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y ), ImGui::GetItemRectMax(), ImGui::GetColorU32( UI::Muted() ) );
		if( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
		{
			ShowProblem( p );
		}
	}
	ImGui::PushStyleColor( ImGuiCol_Text, UI::Muted() );
	ImGui::TextUnformatted( p.next );
	ImGui::PopStyleColor();
	ImGui::PopTextWrapPos();
	if( live )
	{
		bool sameLine = false;
		if( canShow && p.fix != Problems::FIX_SHOW_LEAK )
		{
			if( ImGui::SmallButton( ICON_FOCUS "  Show" ) )
			{
				ShowProblem( p );
			}
			UI::Tip( "Select it and look at it" );
			sameLine = true;
		}
		const idStr fix = FixLabel( p );
		if( fix.Length() > 0 )
		{
			if( sameLine )
			{
				ImGui::SameLine();
			}
			if( ImGui::SmallButton( fix ) )
			{
				changed = ApplyFix( p );
			}
		}
	}
	ImGui::PopID();
	return changed;
}

static void DrawProblemCounts( int errors, int warnings, int notes )
{
	if( errors + warnings + notes == 0 )
	{
		ImGui::TextColored( UI::Good(), ICON_OK );
		ImGui::SameLine();
		ImGui::TextUnformatted( "No problems found." );
		return;
	}
	const int counts[3] = { errors, warnings, notes };
	const char* names[3][2] = { { "error", "errors" }, { "warning", "warnings" }, { "note", "notes" } };
	const Problems::severity_t sev[3] = { Problems::SEV_ERROR, Problems::SEV_WARNING, Problems::SEV_NOTE };
	bool first = true;
	for( int i = 0; i < 3; i++ )
	{
		if( counts[i] == 0 )
		{
			continue;
		}
		if( !first )
		{
			ImGui::SameLine( 0.0f, 14.0f * UI::Scale() );
		}
		first = false;
		ImGui::TextColored( SeverityColor( sev[i] ), "%s", Problems::SeverityIcon( sev[i] ) );
		ImGui::SameLine();
		ImGui::Text( "%d %s", counts[i], names[i][counts[i] == 1 ? 0 : 1] );
	}
}

// read every level in the project and check it; the open one as it is now, unsaved changes included
static void CheckEveryLevel()
{
	idStrList maps;
	Problems::ListLevels( maps );
	projectCheck.Clear();
	int errors = 0, warnings = 0, notes = 0;
	for( int i = 0; i < maps.Num(); i++ )
	{
		levelCheck_t& check = projectCheck.Alloc();
		check.map = maps[i];
		const Problems::levelKind_t kind = idStr::Icmpn( maps[i], PREFAB_FOLDER "/", idStr::Length( PREFAB_FOLDER ) + 1 ) == 0 ? Problems::PIECE : Problems::LEVEL;
		if( document.IsOpen() && maps[i].Icmp( document.Name() ) == 0 )
		{
			Problems::CheckLevel( document.Map(), kind, check.problems );
		}
		else
		{
			idMapFile map;
			if( map.Parse( va( "maps/%s", maps[i].c_str() ), true ) )
			{
				Problems::CheckLevel( &map, kind, check.problems );
			}
			else
			{
				Problems::problem_t& p = check.problems.Alloc();
				p.severity = Problems::SEV_ERROR;
				p.code = "unreadable";
				p.text = "The level's file couldn't be read.";
				p.next = "Open it in a text editor to see what's wrong, or restore it from a backup.";
			}
		}
		int e, w, n;
		Problems::Count( check.problems, e, w, n );
		errors += e;
		warnings += w;
		notes += n;
	}
	projectChecked = true;
	common->Printf( "Problems: checked %d levels: %d errors, %d warnings, %d notes\n", projectCheck.Num(), errors, warnings, notes );
	for( int i = 0; i < projectCheck.Num(); i++ )
	{
		common->Printf( "  level %s: %s\n", projectCheck[i].map.c_str(), Problems::Summary( projectCheck[i].problems ).c_str() );
		for( int j = 0; j < projectCheck[i].problems.Num(); j++ )
		{
			const Problems::problem_t& p = projectCheck[i].problems[j];
			common->Printf( "    %s %s: %s\n", Problems::SeverityName( p.severity ), p.code.c_str(), p.text.c_str() );
		}
	}
	Status( va( "Checked %d levels", projectCheck.Num() ) );
}

static void DrawProjectCheck()
{
	if( !projectChecked )
	{
		return;
	}
	int clean = 0;
	for( int i = 0; i < projectCheck.Num(); i++ )
	{
		if( projectCheck[i].problems.Num() == 0 )
		{
			clean++;
		}
	}
	ImGui::Spacing();
	if( !ImGui::CollapsingHeader( va( "Every level: %d checked###everylevel", projectCheck.Num() ), ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		return;
	}
	ImGui::SameLine();
	if( ImGui::SmallButton( ICON_CLOSE "##closecheck" ) )
	{
		projectChecked = false;
		projectCheck.Clear();
		return;
	}
	UI::Tip( "Put the results away" );
	for( int i = 0; i < projectCheck.Num(); i++ )
	{
		const levelCheck_t& check = projectCheck[i];
		if( check.problems.Num() == 0 )
		{
			continue;
		}
		ImGui::PushID( i );
		const bool open = ImGui::TreeNodeEx( "level", ImGuiTreeNodeFlags_SpanAvailWidth, ICON_MAP_FILE "  %s", check.map.c_str() );
		ImGui::SameLine();
		int e, w, n;
		Problems::Count( check.problems, e, w, n );
		const int worst = e > 0 ? Problems::SEV_ERROR : ( w > 0 ? Problems::SEV_WARNING : Problems::SEV_NOTE );
		ImGui::TextColored( SeverityColor( worst ), "%s", Problems::Summary( check.problems ).c_str() );
		const bool isOpenLevel = document.IsOpen() && check.map.Icmp( document.Name() ) == 0;
		if( !isOpenLevel )
		{
			ImGui::SameLine();
			if( ImGui::SmallButton( ICON_OPEN "  Open" ) )
			{
				OpenMap( check.map );
			}
			UI::Tip( "Open this level to show and fix its problems" );
		}
		if( open )
		{
			if( ImGui::BeginTable( "levelproblems", 2, ImGuiTableFlags_PadOuterX ) )
			{
				ImGui::TableSetupColumn( "icon", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() );
				ImGui::TableSetupColumn( "text", ImGuiTableColumnFlags_WidthStretch );
				for( int j = 0; j < check.problems.Num(); j++ )
				{
					DrawProblemRow( j, check.problems[j], false );
				}
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if( clean > 0 )
	{
		ImGui::TextColored( UI::Good(), ICON_OK );
		ImGui::SameLine();
		ImGui::TextDisabled( "%s", clean == projectCheck.Num() ? "Every level is fine." : ( clean == 1 ? "1 other level is fine." : va( "%d other levels are fine.", clean ) ) );
	}
}

static void DrawProblems()
{
	if( focusProblems )
	{
		focusProblems = false;
		ImGui::SetNextWindowFocus();
	}
	// the tab says how many, with the worst one's icon
	const int total = problemErrors + problemWarnings + problemNotes;
	const char* icon = problemErrors > 0 ? ICON_ERROR : ( problemWarnings > 0 ? ICON_WARNING : ICON_PROBLEMS );
	const idStr title = total > 0 ? idStr( va( "%s  Problems (%d)###Problems", icon, total ) ) : idStr( ICON_PROBLEMS "  Problems###Problems" );
	if( !ImGui::Begin( title, &showProblems ) )
	{
		ImGui::End();
		return;
	}

	// the counts, Check every level, help
	ImGui::AlignTextToFramePadding();
	DrawProblemCounts( problemErrors, problemWarnings, problemNotes );
	{
		const char* label = ICON_SEARCH "  Check every level";
		const float buttonWidth = ImGui::CalcTextSize( label ).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SameLine( Max( ImGui::GetCursorPosX() + 12.0f * UI::Scale(), ImGui::GetContentRegionMax().x - buttonWidth - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x ) );
		if( ImGui::Button( label ) )
		{
			CheckEveryLevel();
		}
		UI::Tip( "Read every level and piece in the project and check them all. Levels with problems are listed below; open one to fix it." );
		ImGui::SameLine();
		UI::HelpButton( "panels/problems", "About Problems" );
	}
	ImGui::Separator();

	ImGui::BeginChild( "problem_list" );
	if( document.IsOpen() && problems.Num() > 0 && ImGui::BeginTable( "problems", 2, ImGuiTableFlags_PadOuterX | ImGuiTableFlags_RowBg ) )
	{
		ImGui::TableSetupColumn( "icon", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() );
		ImGui::TableSetupColumn( "text", ImGuiTableColumnFlags_WidthStretch );
		for( int i = 0; i < problems.Num(); i++ )
		{
			if( DrawProblemRow( i, problems[i], true ) )
			{
				break;		// the list has changed
			}
		}
		ImGui::EndTable();
	}
	else if( document.IsOpen() )
	{
		const bool built = buildHasResult && buildResultMap.Icmp( document.Name() ) == 0 && !BuildRunning();
		Hint( built ? va( "The last build took %d s and had nothing to say.", buildSeconds ) : "Building the level (Ctrl+B, or Play) also checks it, for leaks and other trouble." );
	}
	if( buildLog.Length() > 0 )
	{
		ImGui::Spacing();
		if( ImGui::SmallButton( ICON_BOOK "  Build log" ) )
		{
			showBuildLog = true;
		}
		UI::Tip( "Everything the level compiler printed during the last build" );
	}
	DrawProjectCheck();
	ImGui::EndChild();

	ImGui::End();
}

// the Inspector's card for the selection's problems
static void DrawSelectionProblems()
{
	const Selection sel = document.selection.Primary();
	if( sel.entity < 0 || document.selection.IsFaces() )
	{
		return;
	}
	idList<int> mine;
	for( int i = 0; i < problems.Num(); i++ )
	{
		if( ProblemIsAbout( problems[i], sel.entity, sel.primitive ) )
		{
			mine.Append( i );
		}
	}
	if( mine.Num() == 0 )
	{
		return;
	}
	int worst = Problems::SEV_NOTE;
	for( int i = 0; i < mine.Num(); i++ )
	{
		worst = Max( worst, ( int )problems[mine[i]].severity );
	}
	if( UI::BeginCard( "problems", Problems::SeverityIcon( ( Problems::severity_t )worst ), mine.Num() == 1 ? "1 problem" : va( "%d problems", mine.Num() ), "What's wrong with this, and what to do about it. The Problems panel lists the whole level's." ) )
	{
		if( ImGui::BeginTable( "selproblems", 2 ) )
		{
			ImGui::TableSetupColumn( "icon", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 0.8f );
			ImGui::TableSetupColumn( "text", ImGuiTableColumnFlags_WidthStretch );
			for( int i = 0; i < mine.Num(); i++ )
			{
				const Problems::problem_t& p = problems[mine[i]];
				ImGui::PushID( mine[i] );
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex( 0 );
				ImGui::TextColored( SeverityColor( p.severity ), "%s", Problems::SeverityIcon( p.severity ) );
				ImGui::TableSetColumnIndex( 1 );
				ImGui::PushTextWrapPos( 0.0f );
				ImGui::TextUnformatted( p.text );
				ImGui::PushStyleColor( ImGuiCol_Text, UI::Muted() );
				ImGui::TextUnformatted( p.next );
				ImGui::PopStyleColor();
				ImGui::PopTextWrapPos();
				const idStr fix = FixLabel( p );
				bool changed = false;
				if( fix.Length() > 0 && ImGui::SmallButton( fix ) )
				{
					changed = ApplyFix( p );
				}
				ImGui::PopID();
				if( changed )
				{
					break;
				}
			}
			ImGui::EndTable();
		}
	}
	UI::EndCard();
}

// a marker over everything with a warning or an error, in the 3D view
static void DrawProblemMarkers( const ImGuiDockNode* central )
{
	const idMapFile* map = document.Map();
	if( planView || map == NULL || problemErrors + problemWarnings == 0 || IsPlaying() )
	{
		return;
	}
	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	const ImVec2 lo = central->Pos;
	const ImVec2 hi( central->Pos.x + central->Size.x, central->Pos.y + central->Size.y );
	const float radius = 9.0f * UI::Scale();
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const bool mouseFree = !ImGui::GetIO().WantCaptureMouse;
	int hoverEntity = -1, hoverPrimitive = -1;
	int drawn = 0;

	auto marker = [&]( const idBounds& bounds, int severity, int entity, int primitive )
	{
		idVec3 top = bounds.GetCenter();
		top.z = bounds[1].z + 12.0f;
		idVec2 sp;
		if( !view.WorldToScreen( top, sp ) || sp.x < lo.x + radius || sp.y < lo.y + radius || sp.x > hi.x - radius || sp.y > hi.y - radius )
		{
			return;
		}
		const ImVec2 at( sp.x, sp.y );
		draw->AddCircleFilled( at, radius, IM_COL32( 20, 24, 29, 220 ) );
		draw->AddCircle( at, radius, ImGui::GetColorU32( SeverityColor( severity ) ), 0, 1.5f );
		const char* icon = Problems::SeverityIcon( ( Problems::severity_t )severity );
		const ImVec2 size = ImGui::CalcTextSize( icon );
		draw->AddText( ImVec2( at.x - size.x * 0.5f, at.y - size.y * 0.5f ), ImGui::GetColorU32( SeverityColor( severity ) ), icon );
		if( mouseFree && ( mouse.x - at.x ) * ( mouse.x - at.x ) + ( mouse.y - at.y ) * ( mouse.y - at.y ) < radius * radius * 1.6f )
		{
			hoverEntity = entity;
			hoverPrimitive = primitive;
		}
		drawn++;
	};

	for( int e = 1; e < map->GetNumEntities() && e < entityProblemOwn.Num() && drawn < 200; e++ )
	{
		if( entityProblemOwn[e] < Problems::SEV_WARNING )
		{
			continue;
		}
		const idMapEntity* ent = map->GetEntity( e );
		idBounds bounds;
		if( !EntityBounds( ent, bounds ) )
		{
			const idVec3 origin = ent->epairs.GetVector( "origin" );
			bounds = idBounds( origin - idVec3( 8, 8, 8 ), origin + idVec3( 8, 8, 8 ) );
		}
		marker( bounds, entityProblemOwn[e], e, -1 );
	}
	const idMapEntity* world = map->GetEntity( 0 );
	for( int p = 0; p < worldProblem.Num() && p < world->GetNumPrimitives() && drawn < 200; p++ )
	{
		idBounds bounds;
		if( worldProblem[p] >= Problems::SEV_WARNING && PrimitiveBounds( world->GetPrimitive( p ), bounds ) )
		{
			marker( bounds, worldProblem[p], 0, p );
		}
	}
	if( hoverEntity >= 0 )
	{
		ProblemTooltip( hoverEntity, hoverPrimitive );
	}
}

// Play when the level has errors: say so first
static void RequestPlayWithErrors( bool fromCamera )
{
	playErrorsAsk = true;
	playErrorsFromCamera = fromCamera;
	const int errors = PlayErrorCount();
	common->Printf( "Editor: Play is waiting: the level has %d error%s\n", errors, errors == 1 ? "" : "s" );
}

static void DrawPlayErrorsDialog()
{
	if( playErrorsAsk )
	{
		playErrorsAsk = false;
		ImGui::OpenPopup( "Play with errors?" );
	}
	ImGui::SetNextWindowSize( ImVec2( 480.0f * UI::Scale(), 0.0f ) );
	if( !ImGui::BeginPopupModal( "Play with errors?", NULL, ImGuiWindowFlags_NoResize ) )
	{
		return;
	}
	const int errors = PlayErrorCount();
	ImGui::TextWrapped( "%s", errors == 1 ? "The level has an error, so part of it may not work when you play:" : va( "The level has %d errors, so parts of it may not work when you play:", errors ) );
	ImGui::Spacing();
	int shown = 0, first = -1;
	ImGui::PushTextWrapPos( 0.0f );
	for( int i = 0; i < problems.Num() && shown < 5; i++ )
	{
		if( problems[i].severity != Problems::SEV_ERROR || problems[i].code == "build" || problems[i].code == "leak" )
		{
			continue;
		}
		if( first < 0 )
		{
			first = i;
		}
		ImGui::TextColored( UI::Bad(), ICON_ERROR );
		ImGui::SameLine();
		ImGui::TextUnformatted( problems[i].text );
		shown++;
	}
	ImGui::PopTextWrapPos();
	if( errors > shown )
	{
		Hint( va( "...and %d more in Problems.", errors - shown ) );
	}
	ImGui::Spacing();
	if( UI::ColorButton( ICON_PLAY "  Play anyway", UI::Good() ) )
	{
		ImGui::CloseCurrentPopup();
		playWithErrors = true;
		PlayDocument( playErrorsFromCamera );
	}
	ImGui::SameLine();
	if( ImGui::Button( ICON_FOCUS "  Show me" ) )
	{
		ImGui::CloseCurrentPopup();
		showProblems = true;
		focusProblems = true;
		if( first >= 0 )
		{
			ShowProblem( problems[first] );
		}
	}
	UI::Tip( "Open Problems and look at the first error" );
	ImGui::SameLine();
	if( ImGui::Button( "Cancel" ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) )
	{
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

/*
-------------------------------------------------------------------------------
	problem console commands (for tests and scripting)
-------------------------------------------------------------------------------
*/

// editorProblems [label]: list the open level's problems
static void EditorProblems_f( const idCmdArgs& args )
{
	RefreshProblems( true );
	showProblems = true;
	focusProblems = true;
	const idStr prefix = args.Argc() > 1 ? va( "%s: ", args.Argv( 1 ) ) : "";
	const idMapFile* map = document.Map();
	for( int i = 0; i < problems.Num(); i++ )
	{
		const Problems::problem_t& p = problems[i];
		const char* thing = ( map != NULL && ProblemEntityValid( p ) && p.where.entity > 0 ) ? map->GetEntity( p.where.entity )->epairs.GetString( "name" ) : "";
		common->Printf( "%sproblem: %s %s [%s] %s\n", prefix.c_str(), Problems::SeverityName( p.severity ), p.code.c_str(), thing, p.text.c_str() );
	}
	common->Printf( "%sproblems: %d errors, %d warnings, %d notes\n", prefix.c_str(), problemErrors, problemWarnings, problemNotes );
}

// editorFixProblem <code>: apply the fix of the first problem of that kind
static void EditorFixProblem_f( const idCmdArgs& args )
{
	if( args.Argc() != 2 )
	{
		common->Printf( "usage: editorFixProblem <code>\n" );
		return;
	}
	RefreshProblems( true );
	for( int i = 0; i < problems.Num(); i++ )
	{
		if( problems[i].code.Icmp( args.Argv( 1 ) ) == 0 )
		{
			common->Printf( ApplyFix( problems[i] ) ? "fixed %s\n" : "couldn't fix %s\n", args.Argv( 1 ) );
			return;
		}
	}
	common->Printf( "no %s problem\n", args.Argv( 1 ) );
}

static void EditorCheckProject_f( const idCmdArgs& args )
{
	CheckEveryLevel();
}

// editorPlace <classname> <x> <y> <z> [key value]...: add a thing (selected)
static void EditorPlace_f( const idCmdArgs& args )
{
	if( args.Argc() < 5 || ( args.Argc() - 5 ) % 2 != 0 || !document.IsOpen() )
	{
		common->Printf( "usage: editorPlace <classname> <x> <y> <z> [key value]...\n" );
		return;
	}
	idDict keys;
	for( int i = 5; i + 1 < args.Argc(); i += 2 )
	{
		keys.Set( args.Argv( i ), args.Argv( i + 1 ) );
	}
	const idVec3 origin( atof( args.Argv( 2 ) ), atof( args.Argv( 3 ) ), atof( args.Argv( 4 ) ) );
	const int index = document.AddEntity( args.Argv( 1 ), origin, &keys );
	if( index >= 0 )
	{
		common->Printf( "placed %s\n", document.Map()->GetEntity( index )->epairs.GetString( "name" ) );
	}
}

// editorSetKey <key> [value]: change a setting of the selected thing (no value removes it)
static void EditorSetKey_f( const idCmdArgs& args )
{
	idMapEntity* ent = document.SelectedEntity();
	if( args.Argc() < 2 || ent == NULL )
	{
		common->Printf( "usage: editorSetKey <key> [value], with something selected\n" );
		return;
	}
	document.BeginChange( "Change setting" );
	if( args.Argc() >= 3 )
	{
		ent->epairs.Set( args.Argv( 1 ), args.Argv( 2 ) );
	}
	else
	{
		ent->epairs.Delete( args.Argv( 1 ) );
	}
	document.EndChange();
}

static void RegisterProblemCommands()
{
	cmdSystem->AddCommand( "editorProblems", EditorProblems_f, CMD_FL_SYSTEM, "lists the open level's problems" );
	cmdSystem->AddCommand( "editorFixProblem", EditorFixProblem_f, CMD_FL_SYSTEM, "applies the fix of the first problem with the given code" );
	cmdSystem->AddCommand( "editorCheckProject", EditorCheckProject_f, CMD_FL_SYSTEM, "checks every level in the project for problems" );
	cmdSystem->AddCommand( "editorPlace", EditorPlace_f, CMD_FL_SYSTEM, "adds a thing: editorPlace <classname> <x> <y> <z> [key value]..." );
	cmdSystem->AddCommand( "editorSetKey", EditorSetKey_f, CMD_FL_SYSTEM, "changes a setting of the selected thing" );
	cmdSystem->AddCommand( "editorLiveChanges", EditorLiveChanges_f, CMD_FL_SYSTEM, "lists what the changes made while paused did to the running game (show: open the list over the view)" );
	cmdSystem->AddCommand( "editorWhenPlaying", EditorWhenPlaying_f, CMD_FL_SYSTEM, "runs a command once the game is up and the player is in" );
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
	const bool paused = playing && playPaused;

	ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 10.0f * UI::Scale(), 8.0f * UI::Scale() ) );
	const bool barOpen = ImGui::BeginMainMenuBar();
	ImGui::PopStyleVar();
	if( !barOpen )
	{
		return;
	}

	// wordmark: the labyrinth and the name
	{
		// leave room for the emblem, write the name, then centre the emblem on the name
		const float size = ImGui::GetFontSize() * 0.9f;
		const float space = 6.0f * UI::Scale();
		ImGui::SetCursorPosX( ImGui::GetCursorPosX() + size + space );
		ImGui::PushFont( UI::Font( UI::FONT_BOLD ) );
		ImGui::TextUnformatted( "Daedalus" );
		ImGui::PopFont();
		const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
		UI::EmblemAt( ImGui::GetWindowDrawList(), ImVec2( lo.x - space - size, ( lo.y + hi.y - size ) * 0.5f ), size );
	}
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
			if( IsEditingPiece() )
			{
				FinishPieceEdit( PIECE_SAVE_ALL );
			}
			if( document.Save() )
			{
				Status( "Saved" );
			}
			mapListDirty = true;
		}
		if( ImGui::MenuItem( ICON_BUILD "  Build", "Ctrl+B", false, document.IsOpen() ) )
		{
			BuildDocument();
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

	if( ImGui::BeginMenu( "Edit", !playing || paused ) )
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

	if( ImGui::BeginMenu( "View", !playing || paused ) )
	{
		ImGui::MenuItem( WINDOW_OUTLINER, NULL, &showOutliner );
		ImGui::MenuItem( WINDOW_INSPECTOR, NULL, &showInspector );
		ImGui::MenuItem( Help::WINDOW_HELP, "F1", &Help::Visible() );
		ImGui::MenuItem( WINDOW_CONTENT_BROWSER, NULL, &showContentBrowser );
		ImGui::MenuItem( WINDOW_PROBLEMS, NULL, &showProblems );
		ImGui::MenuItem( WINDOW_MATERIAL, NULL, &showMaterialPanel );
		ImGui::MenuItem( WINDOW_GAMES, NULL, &showGames );
		{
			bool toolsPanel = editor_toolsPanel.GetBool();
			if( ImGui::MenuItem( ICON_BUILD "  Tools as a panel", NULL, &toolsPanel ) )
			{
				editor_toolsPanel.SetBool( toolsPanel );
			}
		}
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
		if( ImGui::MenuItem( ICON_HELP "  Help", "F1" ) )
		{
			Help::Open( NULL );
		}
		if( ImGui::MenuItem( ICON_LESSONS "  Lessons" ) )
		{
			Help::Open( "lessons/getting-started" );
		}
		if( ImGui::MenuItem( ICON_SPARKLES "  Welcome" ) )
		{
			showWelcome = true;
		}
		if( ImGui::MenuItem( ICON_KEYBOARD "  About Daedalus and controls" ) )
		{
			showAbout = true;
		}
		ImGui::EndMenu();
	}

	// how much the editor shows
	ImGui::SetCursorPosX( ImGui::GetCursorPosX() + 14.0f * UI::Scale() );
	if( UI::DepthSwitch() )
	{
		Status( va( "Showing %s: %s", UI::DepthName( UI::Depth() ), UI::Depth() == 0 ? "just what you need to make things." : UI::Depth() == 1 ? "the settings behind things, folded." : "everything, open." ) );
		Help::Event( va( "depth.%d", UI::Depth() ) );
	}

	// Play / Stop sits in the middle of the bar, like other editors
	const float buttonWidth = 104.0f * UI::Scale();
	const float stopWidth = 80.0f * UI::Scale();
	const float pillHeightForGroup = ImGui::GetFontSize() + 10.0f * UI::Scale();
	const float groupWidth = paused ? buttonWidth + stopWidth + 6.0f * UI::Scale() : playing ? buttonWidth : buttonWidth + pillHeightForGroup + 2.0f * UI::Scale();
	ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 20.0f * UI::Scale() );
	ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 10.0f * UI::Scale(), 4.0f * UI::Scale() ) );
	const float barHeight = ImGui::GetWindowHeight();
	const float pillHeight = pillHeightForGroup;
	const float pillY = idMath::Rint( ( barHeight - pillHeight ) * 0.5f );

	// the build sits just left of Play: progress while it runs, a Build button otherwise.
	// Every control here is placed on the same centre line and drawn with its icon centred on
	// the icon's own shape, so circles and pills line up.
	const bool building = BuildRunning();
	const bool showBuildButton = !building && ( !playing || paused );
	const float buildBarWidth = 210.0f * UI::Scale();
	const float gapSmall = 4.0f * UI::Scale();
	const float buildWidth = building ? buildBarWidth + gapSmall + pillHeight : showBuildButton ? pillHeight : 0.0f;
	const float playX = ( ImGui::GetWindowWidth() - groupWidth ) * 0.5f;
	if( buildWidth > 0.0f )
	{
		const float buildX = playX - buildWidth - 8.0f * UI::Scale();
		ImGui::SetCursorPos( ImVec2( buildX, pillY ) );
		if( building )
		{
			const float progress = BuildProgress();
			ImGui::PushStyleColor( ImGuiCol_PlotHistogram, UI::Accent() );
			ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 10.0f * UI::Scale(), 0.0f ) );
			ImGui::ProgressBar( progress, ImVec2( buildBarWidth, pillHeight ), va( ICON_BUILD "  %s  %d%%", buildStage.c_str(), ( int )( progress * 100.0f ) ) );
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
			UI::Tip( buildThen == BUILD_THEN_PLAY ? "Building the level. It plays as soon as it's built; you can keep editing meanwhile." :
					 buildThen == BUILD_THEN_RELOAD ? "Rebuilding the level with your brush changes. The game carries on, where you were, when it's done." :
					 "Building the level. You can keep editing meanwhile." );
			ImGui::SetCursorPos( ImVec2( buildX + buildBarWidth + gapSmall, pillY ) );
			if( UI::PillButton( "cancelbuild", ICON_CLOSE, NULL, UI::Bad(), 0.0f, pillHeight ) )
			{
				CancelBuild();
				Status( "Build cancelled" );
			}
			UI::Tip( "Stop building" );
		}
		else
		{
			if( UI::PillButton( "build", ICON_BUILD, NULL, ImGui::GetStyleColorVec4( ImGuiCol_Button ), 0.0f, pillHeight, document.IsOpen(), false ) )
			{
				BuildDocument();
			}
			UI::Tip( "Build the level (Ctrl+B): turn it into what the game loads. Play does this for you when it's needed; it runs in the background, so you can keep editing." );
		}
	}

	ImGui::SetCursorPos( ImVec2( playX, pillY ) );
	if( paused )
	{
		const bool rebuilding = building && buildThen == BUILD_THEN_RELOAD;
		if( UI::PillButton( "resume", ( rebuilding || liveGeometryChanged ) ? ICON_BUILD : ICON_PLAY, rebuilding ? "Building" : "Resume", UI::Good(), buttonWidth, pillHeight, !rebuilding ) )
		{
			ResumePlay();
		}
		UI::Tip( liveGeometryChanged ? "Rebuild and carry on (F5): some changes need the level rebuilt, so it's built first and then reloads around you." :
				 "Carry on playing from where you were, with your changes (F5)" );
		ImGui::SetCursorPos( ImVec2( playX + buttonWidth + 6.0f * UI::Scale(), pillY ) );
		if( UI::PillButton( "stop", ICON_STOP, "Stop", UI::Bad(), stopWidth, pillHeight ) )
		{
			Stop();
		}
		UI::Tip( "End this play session (F10)" );
	}
	else if( playing )
	{
		if( UI::PillButton( "pause", ICON_PAUSE, "Pause", UI::Warn(), buttonWidth, pillHeight ) )
		{
			PausePlay();
		}
		UI::Tip( "Pause and edit (Esc): changes go into the game. F10 stops." );
	}
	else
	{
		const bool fromCamera = editor_playFrom.GetInteger() == 1;
		if( UI::PillButton( "play", ICON_PLAY, fromCamera ? "Play here" : "Play", UI::Good(), buttonWidth, pillHeight, document.IsOpen() ) )
		{
			PlayDocument();
		}
		UI::Tip( fromCamera ? "Save and play, starting where the camera is (F5). Shift+click starts at the player start. Esc pauses to edit." :
				 "Save and play from the player start (F5). Shift+click starts where the camera is. Esc pauses to edit." );
		ImGui::SetCursorPos( ImVec2( playX + buttonWidth + 2.0f * UI::Scale(), pillY ) );
		if( UI::PillButton( "playfrom", ICON_CHEVRON_DOWN, NULL, UI::Good(), 0.0f, pillHeight, document.IsOpen() ) )
		{
			ImGui::OpenPopup( "##playmenu" );
		}
		UI::Tip( "Where Play starts you" );
		if( ImGui::BeginPopup( "##playmenu" ) )
		{
			if( ImGui::MenuItem( ICON_PLAYER "  Play from the player start", NULL, !fromCamera ) )
			{
				editor_playFrom.SetInteger( 0 );
			}
			if( ImGui::MenuItem( ICON_EYE "  Play from here (the camera)", NULL, fromCamera ) )
			{
				editor_playFrom.SetInteger( 1 );
			}
			ImGui::Separator();
			Hint( "Shift+click Play to use the other one once." );
			ImGui::EndPopup();
		}
	}
	ImGui::PopStyleVar( 2 );

	// right side: status, the level and the project
	const char* game = cvarSystem->GetCVarString( "fs_game" );
	const idStr level = document.IsOpen() ? va( ICON_MAP_FILE "  %s%s", document.Name(), document.IsDirty() ? "  \xe2\x80\xa2" : "" ) : "No level open";
	const idStr project = ( game != NULL && game[0] != '\0' ) ? va( ICON_PROJECT "  %s", game ) : ICON_PROJECT "  Engine content";
	const bool showStatus = statusMessage.Length() > 0 && Sys_Milliseconds() - statusTime < 4000;
	const float gap = 24.0f * UI::Scale();
	float rightWidth = ImGui::CalcTextSize( level.c_str() ).x + gap + ImGui::CalcTextSize( project.c_str() ).x + 16.0f * UI::Scale();
	if( !playing )
	{
		rightWidth += ImGui::GetFrameHeight() * 2.0f + gap;
	}

	// the status message gets what room is left between Play and the right side, shortened to fit
	idStr status;
	if( showStatus )
	{
		const float room = ImGui::GetWindowWidth() - rightWidth - ( playX + groupWidth ) - 2.0f * gap;
		status = statusMessage;
		if( ImGui::CalcTextSize( status.c_str() ).x > room )
		{
			while( status.Length() > 0 && ImGui::CalcTextSize( va( "%s...", status.c_str() ) ).x > room )
			{
				status.CapLength( status.Length() - 1 );
			}
			status = status.Length() > 8 ? va( "%s...", status.c_str() ) : "";
		}
		if( status.Length() > 0 )
		{
			rightWidth += ImGui::CalcTextSize( status.c_str() ).x + gap;
		}
	}
	ImGui::SetCursorPosX( ImGui::GetWindowWidth() - rightWidth );
	if( status.Length() > 0 )
	{
		ImGui::TextColored( UI::Accent(), "%s", status.c_str() );
		if( status != statusMessage )
		{
			UI::Tip( statusMessage.c_str() );
		}
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
	BuildUpdate();
	Sys_FileDropped = FileDropped;
	ModelsUpdate();
	MaterialsUpdate();
	RefreshProblems( false );
	document.afterChange = PieceAfterChange;
	document.filesRestored = PieceFilesRestored;
	prefabFileWritten = PieceFileWritten;
	UI::openHelpPage = OpenHelpPage;
	UI::helpPageExists = Help::PageExists;
	Help::showMe = ShowMe;

	// reloading: wait for the old level to go before treating the game as the new one
	const bool inGame = session != NULL && session->GetState() == idSession::INGAME && game != NULL && game->IsInGame();
	if( playReloading && ( !inGame || Sys_Milliseconds() - playReloadTime > 60000 ) )
	{
		playReloading = false;
	}

	if( !IsPlaying() )
	{
		playPaused = false;
		liveSessionActive = false;
	}
	else if( playReloading )
	{
		// wait for the old level to go before treating the game as the new one
	}
	else if( !liveSessionActive && inGame )
	{
		// a new Play session: remember the level the game has
		liveSessionActive = true;
		LiveCaptureBase();
	}

	if( IsPlaying() && playPaused )
	{
		LiveSync();
	}

	if( IsPlaying() && !playPaused )
	{
		if( !playReloading )
		{
			playStarting = false;
		}

		// Play from here: move the player once the level is up
		if( playTeleportPending && !playReloading && inGame && gameEdit->PlayerIsValid() )
		{
			playTeleportPending = false;
			LiveLog( "moved the player to", playTeleportEye.ToString( 0 ) );
			if( !gameEdit->PlayerTeleport( playTeleportEye, playTeleportAngles ) )
			{
				Status( "The camera was inside something solid, so you start at the player start." );
			}
		}
		if( whenPlayingCommand.Length() > 0 && !playReloading && inGame && gameEdit->PlayerIsValid() && !playTeleportPending )
		{
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "%s\n", whenPlayingCommand.c_str() ) );
			whenPlayingCommand.Clear();
		}
		// panels step aside while playing; the menu bar keeps Stop visible
		return;
	}
	if( !playStarting )
	{
		playTeleportPending = false;
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
	if( showMaterialPanel )
	{
		DrawMaterialPanel();
	}
	if( Help::Visible() )
	{
		// the first time, it joins the Inspector's tabs
		if( ImGui::FindWindowByName( Help::WINDOW_HELP ) == NULL )
		{
			ImGuiWindow* inspector = ImGui::FindWindowByName( WINDOW_INSPECTOR );
			if( inspector != NULL && inspector->DockId != 0 )
			{
				ImGui::SetNextWindowDockID( inspector->DockId, ImGuiCond_FirstUseEver );
			}
		}
		Help::Draw();
	}

	DrawBuildLog();
	DrawAbout();
	DrawWelcome();
	DrawGames();
	DrawNewProjectDialog();
	DrawMakePieceDialog();
	DrawImportDialog();
	DrawNewMaterialDialog();
	DrawPlayErrorsDialog();
	UI::DrawHighlights();
	DrawOpenProjectDialog();
	DrawMapNameDialog( "Save Map As", openSaveAsPopup, false );
	DrawMapNameDialog( "New Map", openNewMapPopup, true );
}

} // namespace DaedalusEditor

#endif // DAEDALUS_EDITOR
