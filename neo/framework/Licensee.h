/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2012 Robert Beckebans

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

/*
===============================================================================

	Definitions for information that is related to a licensee's game name and location.

===============================================================================
*/

#define GAME_NAME						"GrizzTech4"		// appears on window titles and errors

// GrizzTech4: own save/config locations, so nothing collides with DOOM 3 BFG or RBDOOM-3-BFG installs
#define SAVE_PATH						"\\GrizzTech4"			// Windows: Documents\My Games\GrizzTech4
#define SAVE_DIR_POSIX					"grizztech4"			// Linux: $XDG_DATA_HOME/grizztech4 or ~/.local/share/grizztech4
#define PREF_PATH_APP					"GrizzTech4"			// macOS: SDL preference path
#define SCREENSHOT_PREFIX				"grizztech4"

#define ENGINE_VERSION					"GrizzTech4 0.1.0"		// printed in console
#define UPSTREAM_VERSION				"RBDOOM-3-BFG 1.6.0"	// the engine GrizzTech4 is forked from

// DG begin
#ifdef ID_REPRODUCIBLE_BUILD
	// for reproducible builds we hardcode values that would otherwise come from __DATE__ and __TIME__
	// NOTE: remember to update esp. the date for (pre-) releases and RCs and the like
	#define ID__DATE__  "May 10 2025"
	#define ID__TIME__  "18:35:17"

#else // not reproducible build, use __DATE__ and __TIME__ macros
	#define ID__DATE__  __DATE__
	#define ID__TIME__  __TIME__
#endif
// DG end

// jmarshall
#define ENGINE_BRANCH					"master"
// jmarshall end

#ifdef STANDALONE
	#define	BASE_GAMEDIR				"content"
#else
	#define	BASE_GAMEDIR				"base"
#endif

#define CONFIG_FILE						"GrizzTech4.cfg"

// see ASYNC_PROTOCOL_VERSION
// use a different major for each game
#define ASYNC_PROTOCOL_MAJOR			1

// win32 info
#define WIN32_CONSOLE_CLASS				"GrizzTech4_WinConsole"
#define	WIN32_WINDOW_CLASS_NAME			"GrizzTech4"
#define	WIN32_FAKE_WINDOW_CLASS_NAME	"GrizzTech4_WGL_FAKE"

// RB begin
// Default base path (used only if none could be found)
#ifdef __APPLE__
	#define DEFAULT_BASEPATH				"/Applications/GrizzTech4.app/Contents/Resources"
#else
	#define DEFAULT_BASEPATH				"/usr/share/games/grizztech4"
#endif
// RB end

// GrizzTech4: no lookup of DOOM 3 BFG Steam/GOG installs. Games ship their own data.
// (STEAMPATH_APPID, STEAMPATH_NAME and GOGPATH_ID are intentionally left undefined.)

#define VR_OPTIONS						0
