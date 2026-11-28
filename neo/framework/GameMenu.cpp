/*
===========================================================================

Daedalus: the game menu (pause, new game, settings)

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

#include "GameMenu.h"
#include "GameLibrary.h"
#include "../libs/imgui/imgui.h"

idCVar game_menu( "game_menu", "1", CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT, "use Daedalus' own game menu when the game has none of its own" );

enum page_t
{
	PAGE_MAIN,
	PAGE_LEVELS,
	PAGE_SETTINGS
};

static bool			menuOpen = false;
static bool			autoOpened = false;		// opened because the game started without a level
static page_t		page = PAGE_MAIN;
static int			shellAvailable = -1;
static idStrList	levels;
static int			bindingAction = -1;		// waiting for a key for this action

struct action_t
{
	const char*	label;
	const char*	command;
};

static const action_t actions[] =
{
	{ "Move forward",	"_forward" },
	{ "Move back",		"_back" },
	{ "Step left",		"_moveLeft" },
	{ "Step right",		"_moveRight" },
	{ "Jump",			"_moveUp" },
	{ "Crouch",			"_moveDown" },
	{ "Run",			"_speed" },
	{ "Attack",			"_attack" },
	{ "Reload",			"_impulse13" },
	{ "Flashlight",		"_impulse11" },
	{ "Next weapon",	"_impulse14" },
	{ "Previous weapon", "_impulse15" },
	{ "Use / talk",		"_use" },
	{ "Quick save",		"savegame quick" },
	{ "Quick load",		"loadgame quick" },
};
static const int NUM_ACTIONS = sizeof( actions ) / sizeof( actions[0] );

/*
===============================================================================

	State

===============================================================================
*/

static bool InGame()
{
	return game != NULL && game->IsInGame();
}

bool GameMenu::Wanted()
{
	if( !game_menu.GetBool() || cvarSystem->GetCVarBool( "editor" ) )
	{
		return false;
	}
	if( shellAvailable < 0 )
	{
		shellAvailable = ( fileSystem->ReadFile( "generated/swf/shell.bswf", NULL, NULL ) > 0 || fileSystem->ReadFile( "swf/shell.swf", NULL, NULL ) > 0 ) ? 1 : 0;
	}
	return shellAvailable == 0;
}

bool GameMenu::IsOpen()
{
	return menuOpen;
}

void GameMenu::Open()
{
	if( menuOpen )
	{
		return;
	}
	menuOpen = true;
	page = PAGE_MAIN;
	bindingAction = -1;
	if( InGame() )
	{
		cvarSystem->SetCVarInteger( "com_pause", 1 );
	}
}

void GameMenu::Close()
{
	if( !menuOpen )
	{
		return;
	}
	menuOpen = false;
	bindingAction = -1;
	cvarSystem->SetCVarInteger( "com_pause", 0 );
}

void GameMenu::Toggle()
{
	if( menuOpen )
	{
		// a menu with nothing behind it stays up
		if( InGame() )
		{
			Close();
		}
	}
	else
	{
		Open();
	}
}

bool GameMenu::CaptureEvent( const sysEvent_t* event )
{
	if( !menuOpen || bindingAction < 0 || event->evType != SE_KEY || event->evValue2 != 1 )
	{
		return false;
	}
	const int key = event->evValue;
	if( key != K_ESCAPE )
	{
		idKeyInput::SetBinding( key, actions[bindingAction].command );
	}
	bindingAction = -1;
	return true;
}

/*
===============================================================================

	Drawing

===============================================================================
*/

static const char* Title()
{
	const gameKind_t kind = GameLibrary::ProjectGame( cvarSystem->GetCVarString( "fs_game" ) );
	if( kind != GAME_KIND_COUNT )
	{
		return GameLibrary::Name( kind );
	}
	const char* project = cvarSystem->GetCVarString( "fs_game" );
	return project[0] ? project : "Daedalus";
}

static void StartLevel( const char* level )
{
	GameMenu::Close();
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, va( "map %s\n", level ) );
}

static void RefreshLevels()
{
	levels.Clear();
	idFileList* files = fileSystem->ListFilesTree( "maps", ".map", true );
	if( files != NULL )
	{
		for( int i = 0; i < files->GetNumFiles(); i++ )
		{
			idStr name = files->GetFile( i );
			name.StripLeading( "maps/" );
			name.StripFileExtension();
			// pieces, autosaves and multiplayer arenas aren't levels to start
			if( name.Find( "prefabs/" ) == 0 || name.Find( "autosave" ) >= 0 || name.Find( "/mp/" ) >= 0 || name.Find( "mp/" ) == 0 )
			{
				continue;
			}
			levels.AddUnique( name );
		}
		fileSystem->FreeFileList( files );
	}
	levels.SortWithTemplate( idSort_PathStr() );
}

static bool BigButton( const char* label )
{
	return ImGui::Button( label, ImVec2( -FLT_MIN, ImGui::GetFontSize() * 2.2f ) );
}

static void DrawMain()
{
	const gameKind_t kind = GameLibrary::ProjectGame( cvarSystem->GetCVarString( "fs_game" ) );
	if( InGame() && BigButton( "Carry on" ) )
	{
		GameMenu::Close();
	}
	const char* first = kind != GAME_KIND_COUNT ? GameLibrary::FirstLevel( kind ) : "";
	if( first[0] && BigButton( "New game" ) )
	{
		StartLevel( first );
	}
	if( BigButton( "Choose a level" ) )
	{
		RefreshLevels();
		page = PAGE_LEVELS;
	}
	if( InGame() && BigButton( "Quick save" ) )
	{
		GameMenu::Close();
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "savegame quick\n" );
	}
	if( BigButton( "Quick load" ) )
	{
		GameMenu::Close();
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "loadgame quick\n" );
	}
	if( BigButton( "Settings" ) )
	{
		page = PAGE_SETTINGS;
	}
	if( BigButton( "Quit" ) )
	{
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
	}
}

static void DrawLevels()
{
	if( ImGui::Button( "< Back" ) )
	{
		page = PAGE_MAIN;
	}
	ImGui::Separator();
	ImGui::BeginChild( "##levels", ImVec2( 0, ImGui::GetFontSize() * 22.0f ) );
	if( levels.Num() == 0 )
	{
		ImGui::TextUnformatted( "No levels found." );
	}
	for( int i = 0; i < levels.Num(); i++ )
	{
		if( ImGui::Selectable( levels[i].c_str() ) )
		{
			StartLevel( levels[i] );
		}
	}
	ImGui::EndChild();
}

static void CvarSliderFloat( const char* label, const char* cvar, float lo, float hi, const char* format = "%.2f" )
{
	float v = cvarSystem->GetCVarFloat( cvar );
	ImGui::SetNextItemWidth( -FLT_MIN );
	if( ImGui::SliderFloat( va( "##%s", cvar ), &v, lo, hi, va( "%s: %s", label, format ) ) )
	{
		cvarSystem->SetCVarFloat( cvar, v );
	}
}

static void CvarCheckbox( const char* label, const char* cvar )
{
	bool v = cvarSystem->GetCVarBool( cvar );
	if( ImGui::Checkbox( label, &v ) )
	{
		cvarSystem->SetCVarBool( cvar, v );
	}
}

static void DrawSettings()
{
	if( ImGui::Button( "< Back" ) )
	{
		page = PAGE_MAIN;
		bindingAction = -1;
	}
	ImGui::Separator();
	if( !ImGui::BeginTabBar( "##settings" ) )
	{
		return;
	}
	if( ImGui::BeginTabItem( "Display" ) )
	{
		bool fullscreen = cvarSystem->GetCVarInteger( "r_fullscreen" ) != 0;
		if( ImGui::Checkbox( "Full screen", &fullscreen ) )
		{
			cvarSystem->SetCVarInteger( "r_fullscreen", fullscreen ? 1 : 0 );
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "vid_restart\n" );
		}
		static const int sizes[][2] = { { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3440, 1440 }, { 3840, 2160 } };
		const int w = cvarSystem->GetCVarInteger( "r_windowWidth" );
		const int h = cvarSystem->GetCVarInteger( "r_windowHeight" );
		ImGui::SetNextItemWidth( -FLT_MIN );
		if( ImGui::BeginCombo( "##size", va( "Window size: %d x %d", w, h ) ) )
		{
			for( int i = 0; i < ( int )( sizeof( sizes ) / sizeof( sizes[0] ) ); i++ )
			{
				if( ImGui::Selectable( va( "%d x %d", sizes[i][0], sizes[i][1] ), w == sizes[i][0] && h == sizes[i][1] ) )
				{
					cvarSystem->SetCVarInteger( "r_windowWidth", sizes[i][0] );
					cvarSystem->SetCVarInteger( "r_windowHeight", sizes[i][1] );
					cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "vid_restart\n" );
				}
			}
			ImGui::EndCombo();
		}
		CvarSliderFloat( "Field of view", "g_fov", 70.0f, 120.0f, "%.0f" );
		ImGui::TextDisabled( "Widescreen keeps this horizontal field of view at any shape of screen." );
		bool vsync = cvarSystem->GetCVarInteger( "r_swapInterval" ) != 0;
		if( ImGui::Checkbox( "Vertical sync (no tearing)", &vsync ) )
		{
			cvarSystem->SetCVarInteger( "r_swapInterval", vsync ? 1 : 0 );
		}
		int hz = cvarSystem->GetCVarInteger( "com_engineHz" );
		ImGui::SetNextItemWidth( -FLT_MIN );
		if( ImGui::SliderInt( "##hz", &hz, 60, 240, "Frame rate: %d" ) )
		{
			cvarSystem->SetCVarInteger( "com_engineHz", hz );
		}
		CvarSliderFloat( "Brightness", "r_exposure", 0.1f, 1.0f );
		CvarCheckbox( "Show frame rate", "com_showFPS" );
		ImGui::EndTabItem();
	}
	if( ImGui::BeginTabItem( "Controls" ) )
	{
		CvarSliderFloat( "Mouse speed", "sensitivity", 1.0f, 20.0f, "%.1f" );
		CvarCheckbox( "Invert mouse look", "in_mouseInvertLook" );
		CvarCheckbox( "Always run", "in_alwaysRun" );
		CvarCheckbox( "Crouch toggles (press once)", "in_toggleCrouch" );
		CvarCheckbox( "Run toggles (press once)", "in_toggleRun" );
		CvarCheckbox( "Gamepad", "in_useJoystick" );
		CvarSliderFloat( "Gamepad dead zone", "joy_deadZone", 0.0f, 0.5f );
		ImGui::Separator();
		ImGui::TextUnformatted( "Keys" );
		if( ImGui::BeginTable( "##keys", 2, ImGuiTableFlags_RowBg ) )
		{
			for( int i = 0; i < NUM_ACTIONS; i++ )
			{
				ImGui::PushID( i );
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted( actions[i].label );
				ImGui::TableNextColumn();
				const char* keys = idKeyInput::KeysFromBinding( actions[i].command );
				const char* shown = bindingAction == i ? "Press a key (Esc cancels)" : ( keys != NULL && keys[0] ? keys : "(none)" );
				if( ImGui::Button( shown, ImVec2( -FLT_MIN, 0 ) ) )
				{
					bindingAction = i;
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::EndTabItem();
	}
	if( ImGui::BeginTabItem( "Sound" ) )
	{
		CvarSliderFloat( "Volume (dB)", "s_volume_dB", -40.0f, 0.0f, "%.0f" );
		ImGui::EndTabItem();
	}
	ImGui::EndTabBar();
}

void GameMenu::Draw()
{
	// a game with no menus of its own and no level running opens on this menu
	if( !menuOpen && Wanted() && !InGame() && !autoOpened && cvarSystem->GetCVarString( "fs_game" )[0] )
	{
		autoOpened = true;
		Open();
	}
	if( !menuOpen )
	{
		return;
	}
	ImGuiIO& io = ImGui::GetIO();
	io.MouseDrawCursor = true;

	// dim the game behind
	ImGui::GetBackgroundDrawList()->AddRectFilled( ImVec2( 0, 0 ), io.DisplaySize, IM_COL32( 0, 0, 0, 150 ) );

	const float width = Min( 520.0f, io.DisplaySize.x * 0.9f );
	ImGui::SetNextWindowPos( ImVec2( io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f ), ImGuiCond_Always, ImVec2( 0.5f, 0.5f ) );
	ImGui::SetNextWindowSize( ImVec2( width, 0 ), ImGuiCond_Always );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 24, 20 ) );
	ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 8, 8 ) );
	ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 6.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 10.0f );
	if( ImGui::Begin( "##gamemenu", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize ) )
	{
		ImGui::SetWindowFontScale( 1.6f );
		ImGui::TextUnformatted( Title() );
		ImGui::SetWindowFontScale( 1.0f );
		ImGui::TextDisabled( InGame() ? "Paused" : "Played with Daedalus" );
		ImGui::Spacing();
		switch( page )
		{
			case PAGE_LEVELS:
				DrawLevels();
				break;
			case PAGE_SETTINGS:
				DrawSettings();
				break;
			default:
				DrawMain();
				break;
		}
	}
	ImGui::End();
	ImGui::PopStyleVar( 4 );
}

/*
===============================================================================

	Commands

===============================================================================
*/

static void GameMenu_f( const idCmdArgs& args )
{
	const char* what = args.Argc() > 1 ? args.Argv( 1 ) : "toggle";
	if( !idStr::Icmp( what, "open" ) )
	{
		GameMenu::Open();
	}
	else if( !idStr::Icmp( what, "close" ) )
	{
		GameMenu::Close();
	}
	else if( !idStr::Icmp( what, "settings" ) )
	{
		GameMenu::Open();
		page = PAGE_SETTINGS;
	}
	else if( !idStr::Icmp( what, "levels" ) )
	{
		GameMenu::Open();
		RefreshLevels();
		page = PAGE_LEVELS;
		common->Printf( "Levels: %d\n", levels.Num() );
		for( int i = 0; i < levels.Num(); i++ )
		{
			common->Printf( "  level: %s\n", levels[i].c_str() );
		}
	}
	else
	{
		GameMenu::Toggle();
	}
	common->Printf( "Game menu: %s (wanted: %s)\n", GameMenu::IsOpen() ? "open" : "closed", GameMenu::Wanted() ? "yes" : "no" );
}

void GameMenu::RegisterCommands()
{
	cmdSystem->AddCommand( "gameMenu", GameMenu_f, CMD_FL_SYSTEM, "opens or closes the game menu: open, close, settings, levels" );
}
