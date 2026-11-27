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

#include "win_local.h"
#include "rc/doom_resource.h"

/*
===============================================================================

	Extra windows (Win32)

	Borderless windows the editor's panels move into when they're dragged out
	of the main window. ImGui draws their title bars; input that lands on them
	is handed to the main window procedure, so the rest of the engine sees it
	as if it came from the main window. The mouse position itself is polled
	by the ImGui layer in screen coordinates.

	Based on the Win32 platform backend of Dear ImGui (MIT license).

===============================================================================
*/

#define EXTRA_WINDOW_CLASS_NAME "GrizzTech4Panel"

struct extraWindow_t
{
	HWND		hwnd;
	bool		noInputs;
	bool		closeRequested;
	bool		moved;
	bool		resized;
};

static idList<extraWindow_t*>	extraWindows;
static bool						classRegistered = false;

static extraWindow_t* FindExtraWindow( HWND hwnd )
{
	for( int i = 0; i < extraWindows.Num(); i++ )
	{
		if( extraWindows[i]->hwnd == hwnd )
		{
			return extraWindows[i];
		}
	}
	return NULL;
}

// used by the main window procedure: focus moving to one of these doesn't deactivate the app
bool Win_IsExtraWindow( HWND hwnd )
{
	return hwnd != NULL && FindExtraWindow( hwnd ) != NULL;
}

static LRESULT CALLBACK ExtraWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	extraWindow_t* window = FindExtraWindow( hWnd );
	if( window == NULL )
	{
		return DefWindowProc( hWnd, uMsg, wParam, lParam );
	}

	switch( uMsg )
	{
		case WM_CLOSE:
			window->closeRequested = true;
			return 0;

		case WM_MOVE:
			window->moved = true;
			break;

		case WM_SIZE:
			window->resized = true;
			break;

		case WM_NCHITTEST:
			// a panel being dragged lets the mouse through, so what's under it can be found for docking
			if( window->noInputs )
			{
				return HTTRANSPARENT;
			}
			break;

		case WM_MOUSEACTIVATE:
			break;

		case WM_ACTIVATE:
			if( LOWORD( wParam ) != WA_INACTIVE )
			{
				// the app stays active (no pause, no background throttling) while a panel has focus
				win32.activeApp = true;
				cvarSystem->SetCVarBool( "com_pause", false );
				cvarSystem->SetCVarBool( "com_activeApp", true );
				soundSystem->SetMute( false );
			}
			break;

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_XBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDBLCLK:
		case WM_MBUTTONDBLCLK:
			// keep getting the mouse while a button is held, even outside the window
			SetCapture( hWnd );
			return MainWndProc( win32.hWnd, uMsg == WM_LBUTTONDBLCLK ? WM_LBUTTONDOWN : uMsg == WM_RBUTTONDBLCLK ? WM_RBUTTONDOWN : uMsg == WM_MBUTTONDBLCLK ? WM_MBUTTONDOWN : uMsg, wParam, lParam );

		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		case WM_MBUTTONUP:
		case WM_XBUTTONUP:
		{
			const LRESULT result = MainWndProc( win32.hWnd, uMsg, wParam, lParam );
			if( ( wParam & ( MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2 ) ) == 0 && GetCapture() == hWnd )
			{
				ReleaseCapture();
			}
			return result;
		}

		case WM_MOUSEWHEEL:
		case WM_MOUSEHWHEEL:
		case WM_KEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP:
		case WM_CHAR:
		case WM_UNICHAR:
			return MainWndProc( win32.hWnd, uMsg, wParam, lParam );

		case WM_SETCURSOR:
			// ImGui draws the cursor
			if( LOWORD( lParam ) == HTCLIENT )
			{
				SetCursor( NULL );
				return TRUE;
			}
			break;

		case WM_ERASEBKGND:
			return 1;
	}
	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

static void RegisterExtraWindowClass()
{
	if( classRegistered )
	{
		return;
	}
	WNDCLASSEX wc = {};
	wc.cbSize = sizeof( WNDCLASSEX );
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = ExtraWndProc;
	wc.hInstance = win32.hInstance;
	wc.hIcon = LoadIcon( win32.hInstance, MAKEINTRESOURCE( IDI_ICON1 ) );
	wc.hCursor = LoadCursor( NULL, IDC_ARROW );
	wc.hbrBackground = ( HBRUSH )GetStockObject( BLACK_BRUSH );
	wc.lpszClassName = EXTRA_WINDOW_CLASS_NAME;
	RegisterClassEx( &wc );
	classRegistered = true;
}

static const DWORD EXTRA_STYLE = WS_POPUP;
static const DWORD EXTRA_EX_STYLE = WS_EX_TOOLWINDOW;

void* Sys_MainWindowHandle()
{
	return win32.hWnd;
}

bool Sys_MainWindowRect( sysScreenRect_t& rect )
{
	if( win32.hWnd == NULL )
	{
		return false;
	}
	RECT client;
	if( !GetClientRect( win32.hWnd, &client ) )
	{
		return false;
	}
	POINT origin = { 0, 0 };
	ClientToScreen( win32.hWnd, &origin );
	rect.x = origin.x;
	rect.y = origin.y;
	rect.w = client.right - client.left;
	rect.h = client.bottom - client.top;
	return true;
}

bool Sys_GetCursorScreenPos( int& x, int& y )
{
	POINT p;
	if( !GetCursorPos( &p ) )
	{
		return false;
	}
	x = p.x;
	y = p.y;
	return true;
}

void* Sys_WindowUnderCursor()
{
	POINT p;
	if( !GetCursorPos( &p ) )
	{
		return NULL;
	}
	HWND hwnd = WindowFromPoint( p );
	if( hwnd == win32.hWnd || Win_IsExtraWindow( hwnd ) )
	{
		return hwnd;
	}
	return NULL;
}

struct monitorList_t
{
	sysScreenRect_t* 	full;
	sysScreenRect_t* 	work;
	int					max;
	int					count;
};

static BOOL CALLBACK EnumMonitor( HMONITOR monitor, HDC, LPRECT, LPARAM param )
{
	monitorList_t* list = ( monitorList_t* )param;
	if( list->count >= list->max )
	{
		return FALSE;
	}
	MONITORINFO info = {};
	info.cbSize = sizeof( MONITORINFO );
	if( !GetMonitorInfo( monitor, &info ) )
	{
		return TRUE;
	}
	int slot = list->count;
	if( ( info.dwFlags & MONITORINFOF_PRIMARY ) && slot > 0 )
	{
		// the primary monitor goes first
		list->full[slot] = list->full[0];
		list->work[slot] = list->work[0];
		slot = 0;
	}
	list->full[slot] = { info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top };
	list->work[slot] = { info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left, info.rcWork.bottom - info.rcWork.top };
	list->count++;
	return TRUE;
}

int Sys_GetMonitors( sysScreenRect_t* full, sysScreenRect_t* work, int max )
{
	monitorList_t list = { full, work, max, 0 };
	EnumDisplayMonitors( NULL, NULL, EnumMonitor, ( LPARAM )&list );
	return list.count;
}

void Sys_CaptureMouseAllWindows( bool capture )
{
	// done per window on button down/up (SetCapture), which is what Windows wants
}

void* Sys_ExtraWindowCreate( const sysScreenRect_t& rect, void* owner )
{
	RegisterExtraWindowClass();

	RECT r = { rect.x, rect.y, rect.x + rect.w, rect.y + rect.h };
	AdjustWindowRectEx( &r, EXTRA_STYLE, FALSE, EXTRA_EX_STYLE );

	extraWindow_t* window = new extraWindow_t();
	window->noInputs = false;
	window->closeRequested = false;
	window->moved = false;
	window->resized = false;
	extraWindows.Append( window );

	// owned by the main window: stays above it and minimizes with it
	window->hwnd = CreateWindowEx( EXTRA_EX_STYLE, EXTRA_WINDOW_CLASS_NAME, "GrizzTech4", EXTRA_STYLE,
								   r.left, r.top, r.right - r.left, r.bottom - r.top,
								   owner != NULL ? ( HWND )owner : win32.hWnd, NULL, win32.hInstance, NULL );
	if( window->hwnd == NULL )
	{
		extraWindows.Remove( window );
		delete window;
		return NULL;
	}
	return window->hwnd;
}

void Sys_ExtraWindowDestroy( void* handle )
{
	HWND hwnd = ( HWND )handle;
	extraWindow_t* window = FindExtraWindow( hwnd );
	if( GetCapture() == hwnd )
	{
		// a window being destroyed while it has the mouse hands it back to the main window
		ReleaseCapture();
		SetCapture( win32.hWnd );
	}
	DestroyWindow( hwnd );
	if( window != NULL )
	{
		extraWindows.Remove( window );
		delete window;
	}
}

void Sys_ExtraWindowShow( void* handle, bool activate )
{
	ShowWindow( ( HWND )handle, activate ? SW_SHOW : SW_SHOWNA );
}

void Sys_ExtraWindowSetPos( void* handle, int x, int y )
{
	RECT r = { x, y, x, y };
	AdjustWindowRectEx( &r, EXTRA_STYLE, FALSE, EXTRA_EX_STYLE );
	SetWindowPos( ( HWND )handle, NULL, r.left, r.top, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE );
}

void Sys_ExtraWindowSetSize( void* handle, int w, int h )
{
	RECT r = { 0, 0, w, h };
	AdjustWindowRectEx( &r, EXTRA_STYLE, FALSE, EXTRA_EX_STYLE );
	SetWindowPos( ( HWND )handle, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE );
}

bool Sys_ExtraWindowGetRect( void* handle, sysScreenRect_t& rect )
{
	HWND hwnd = ( HWND )handle;
	RECT client;
	if( !GetClientRect( hwnd, &client ) )
	{
		return false;
	}
	POINT origin = { 0, 0 };
	ClientToScreen( hwnd, &origin );
	rect.x = origin.x;
	rect.y = origin.y;
	rect.w = client.right - client.left;
	rect.h = client.bottom - client.top;
	return true;
}

void Sys_ExtraWindowFocus( void* handle )
{
	HWND hwnd = ( HWND )handle;
	BringWindowToTop( hwnd );
	SetForegroundWindow( hwnd );
	SetFocus( hwnd );
}

bool Sys_ExtraWindowHasFocus( void* handle )
{
	return GetForegroundWindow() == ( HWND )handle;
}

bool Sys_ExtraWindowIsMinimized( void* handle )
{
	return IsIconic( ( HWND )handle ) != 0;
}

void Sys_ExtraWindowSetTitle( void* handle, const char* title )
{
	SetWindowText( ( HWND )handle, title );
}

void Sys_ExtraWindowSetNoInputs( void* handle, bool noInputs )
{
	extraWindow_t* window = FindExtraWindow( ( HWND )handle );
	if( window != NULL )
	{
		window->noInputs = noInputs;
	}
}

void Sys_ExtraWindowTakeEvents( void* handle, sysExtraWindowEvents_t& events )
{
	events.closeRequested = events.moved = events.resized = false;
	extraWindow_t* window = FindExtraWindow( ( HWND )handle );
	if( window != NULL )
	{
		events.closeRequested = window->closeRequested;
		events.moved = window->moved;
		events.resized = window->resized;
		window->closeRequested = window->moved = window->resized = false;
	}
}
