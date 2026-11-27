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

#include <SDL.h>

/*
===============================================================================

	Extra windows (SDL)

	Borderless windows the editor's panels move into when they're dragged out
	of the main window. ImGui draws their title bars. Keys and mouse buttons
	from them arrive in the same SDL event queue as the main window's; the
	mouse position is polled by the ImGui layer in screen coordinates.

	Based on the SDL2 platform backend of Dear ImGui (MIT license).

===============================================================================
*/

struct extraWindow_t
{
	SDL_Window* 	window;
	Uint32			id;
	bool			closeRequested;
	bool			moved;
	bool			resized;
};

static idList<extraWindow_t*>	extraWindows;

static extraWindow_t* FindExtraWindow( SDL_Window* window )
{
	for( int i = 0; i < extraWindows.Num(); i++ )
	{
		if( extraWindows[i]->window == window )
		{
			return extraWindows[i];
		}
	}
	return NULL;
}

static extraWindow_t* FindExtraWindowById( Uint32 id )
{
	for( int i = 0; i < extraWindows.Num(); i++ )
	{
		if( extraWindows[i]->id == id )
		{
			return extraWindows[i];
		}
	}
	return NULL;
}

// the event loop asks this first: window events of extra windows are handled here
bool Sys_SDL_HandleExtraWindowEvent( const SDL_Event& ev )
{
	if( ev.type != SDL_WINDOWEVENT )
	{
		return false;
	}
	extraWindow_t* window = FindExtraWindowById( ev.window.windowID );
	if( window == NULL )
	{
		return false;
	}
	switch( ev.window.event )
	{
		case SDL_WINDOWEVENT_CLOSE:
			window->closeRequested = true;
			break;
		case SDL_WINDOWEVENT_MOVED:
			window->moved = true;
			break;
		case SDL_WINDOWEVENT_RESIZED:
		case SDL_WINDOWEVENT_SIZE_CHANGED:
			window->resized = true;
			break;
		case SDL_WINDOWEVENT_FOCUS_GAINED:
			// the app stays active (no pause, no background throttling) while a panel has focus
			cvarSystem->SetCVarBool( "com_pause", false );
			cvarSystem->SetCVarBool( "com_activeApp", true );
			break;
	}
	return true;
}

// mouse motion over extra windows is in their own coordinates; the ImGui layer polls instead
bool Sys_SDL_IsExtraWindow( Uint32 windowID )
{
	return FindExtraWindowById( windowID ) != NULL;
}

bool Sys_MainWindowRect( sysScreenRect_t& rect )
{
	SDL_Window* main = ( SDL_Window* )Sys_MainWindowHandle();
	if( main == NULL )
	{
		return false;
	}
	SDL_GetWindowPosition( main, &rect.x, &rect.y );
	SDL_GetWindowSize( main, &rect.w, &rect.h );
	return true;
}

// the mouse in screen coordinates as motion events last reported it (window position + event position)
static int	eventMouseX = 0, eventMouseY = 0;
static bool	eventMouseValid = false;

void Sys_SDL_TrackMouseMotion( const SDL_Event& ev )
{
	SDL_Window* window = SDL_GetWindowFromID( ev.motion.windowID );
	if( window != NULL )
	{
		int wx, wy;
		SDL_GetWindowPosition( window, &wx, &wy );
		eventMouseX = wx + ev.motion.x;
		eventMouseY = wy + ev.motion.y;
		eventMouseValid = true;
	}
}

bool Sys_GetCursorScreenPos( int& x, int& y )
{
	// The global state is right when it changes, but on X11 it stops updating while the mouse is
	// captured for a drag, so motion events (in the coordinates of their own window) fill in then.
	static int lastGlobalX = INT_MIN, lastGlobalY = INT_MIN;
	int gx, gy;
	SDL_GetGlobalMouseState( &gx, &gy );
	const bool globalChanged = gx != lastGlobalX || gy != lastGlobalY;
	lastGlobalX = gx;
	lastGlobalY = gy;
	if( !globalChanged && eventMouseValid && SDL_GetMouseState( NULL, NULL ) != 0 )
	{
		x = eventMouseX;
		y = eventMouseY;
	}
	else
	{
		x = gx;
		y = gy;
	}
	return true;
}

void* Sys_WindowUnderCursor()
{
	SDL_Window* focus = SDL_GetMouseFocus();
	if( focus != NULL && ( focus == Sys_MainWindowHandle() || FindExtraWindow( focus ) != NULL ) )
	{
		return focus;
	}
	return NULL;
}

int Sys_GetMonitors( sysScreenRect_t* full, sysScreenRect_t* work, int max )
{
	const int count = Min( max, SDL_GetNumVideoDisplays() );
	for( int i = 0; i < count; i++ )
	{
		SDL_Rect r;
		SDL_GetDisplayBounds( i, &r );
		full[i] = { r.x, r.y, r.w, r.h };
		if( SDL_GetDisplayUsableBounds( i, &r ) == 0 && r.w > 0 && r.h > 0 )
		{
			work[i] = { r.x, r.y, r.w, r.h };
		}
		else
		{
			work[i] = full[i];
		}
	}
	return Max( 0, count );
}

void Sys_CaptureMouseAllWindows( bool capture )
{
	// a drag keeps sending motion and the release even outside our windows
	static bool captured = false;
	if( capture != captured )
	{
		SDL_CaptureMouse( capture ? SDL_TRUE : SDL_FALSE );
		captured = capture;
	}
}

void* Sys_ExtraWindowCreate( const sysScreenRect_t& rect, void* owner )
{
	// the first click on a panel window that isn't focused should still click
	SDL_SetHint( SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1" );

	const Uint32 flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_SKIP_TASKBAR | SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI;
	SDL_Window* sdlWindow = SDL_CreateWindow( "Daedalus", rect.x, rect.y, Max( 1, rect.w ), Max( 1, rect.h ), flags );
	if( sdlWindow == NULL )
	{
		common->Warning( "Couldn't open a panel window: %s", SDL_GetError() );
		return NULL;
	}

	extraWindow_t* window = new extraWindow_t();
	window->window = sdlWindow;
	window->id = SDL_GetWindowID( sdlWindow );
	window->closeRequested = false;
	window->moved = false;
	window->resized = false;
	extraWindows.Append( window );
	return sdlWindow;
}

void Sys_ExtraWindowDestroy( void* handle )
{
	// the keyboard goes back to the main window (without a window manager it would go nowhere)
	SDL_Window* main = ( SDL_Window* )Sys_MainWindowHandle();
	if( main != NULL && SDL_GetKeyboardFocus() == ( SDL_Window* )handle )
	{
		SDL_RaiseWindow( main );
		SDL_SetWindowInputFocus( main );
	}

	extraWindow_t* window = FindExtraWindow( ( SDL_Window* )handle );
	if( window != NULL )
	{
		extraWindows.Remove( window );
		delete window;
	}
	SDL_DestroyWindow( ( SDL_Window* )handle );
}

void Sys_ExtraWindowShow( void* handle, bool activate )
{
#if defined( SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN )
	SDL_SetHint( SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, activate ? "0" : "1" );
#endif
	SDL_ShowWindow( ( SDL_Window* )handle );
}

void Sys_ExtraWindowSetPos( void* handle, int x, int y )
{
	SDL_SetWindowPosition( ( SDL_Window* )handle, x, y );
}

void Sys_ExtraWindowSetSize( void* handle, int w, int h )
{
	SDL_SetWindowSize( ( SDL_Window* )handle, Max( 1, w ), Max( 1, h ) );
}

bool Sys_ExtraWindowGetRect( void* handle, sysScreenRect_t& rect )
{
	SDL_GetWindowPosition( ( SDL_Window* )handle, &rect.x, &rect.y );
	SDL_GetWindowSize( ( SDL_Window* )handle, &rect.w, &rect.h );
	return true;
}

void Sys_ExtraWindowFocus( void* handle )
{
	SDL_RaiseWindow( ( SDL_Window* )handle );
	SDL_SetWindowInputFocus( ( SDL_Window* )handle );
}

bool Sys_ExtraWindowHasFocus( void* handle )
{
	return ( SDL_GetWindowFlags( ( SDL_Window* )handle ) & SDL_WINDOW_INPUT_FOCUS ) != 0;
}

bool Sys_ExtraWindowIsMinimized( void* handle )
{
	return ( SDL_GetWindowFlags( ( SDL_Window* )handle ) & SDL_WINDOW_MINIMIZED ) != 0;
}

void Sys_ExtraWindowSetTitle( void* handle, const char* title )
{
	SDL_SetWindowTitle( ( SDL_Window* )handle, title );
}

void Sys_ExtraWindowSetNoInputs( void* handle, bool noInputs )
{
	// SDL2 can't make a window click-through; ImGui works out what's under the mouse itself
}

void Sys_ExtraWindowTakeEvents( void* handle, sysExtraWindowEvents_t& events )
{
	events.closeRequested = events.moved = events.resized = false;
	extraWindow_t* window = FindExtraWindow( ( SDL_Window* )handle );
	if( window != NULL )
	{
		events.closeRequested = window->closeRequested;
		events.moved = window->moved;
		events.resized = window->resized;
		window->closeRequested = window->moved = window->resized = false;
	}
}
