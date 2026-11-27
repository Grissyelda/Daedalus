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

#include "BFGimgui.h"
#include "../libs/imgui/imgui_internal.h"
#include "../renderer/ImGuiViewportRenderer.h"
#include "../tools/imgui/editor/EditorShell.h"

/*
===============================================================================

	ImGui multi-viewports: editor panels in windows of their own

	When a panel is dragged out of the main window, ImGui gives it a
	"viewport" and asks the platform for an OS window to show it in (on any
	monitor). These callbacks make those windows (Sys_ExtraWindow*) and draw
	them (ImGuiViewportRenderer).

	ImGui's coordinates stay relative to the main window's client area, as
	they are without viewports, so the editor's view code (picking, gizmos)
	doesn't change. Screen positions are converted at the edges here.

	Threads: ImGui::Render runs on the game thread; everything here runs on
	the main thread (NewFrame, and RenderPlatformWindows after the game
	thread has finished), as OS windows and the GPU queue need.

===============================================================================
*/

idCVar editor_detachablePanels( "editor_detachablePanels", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT,
								"let editor panels be dragged out of the main window, e.g. onto another monitor; takes effect after a restart" );

idCVar editor_debugPanels( "editor_debugPanels", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT, "print what the editor's panel windows do" );

namespace ImGuiHook
{

static bool				viewportsActive = false;
static bool				hoverFromPlatform = false;
static sysScreenRect_t	mainRect = { 0, 0, 0, 0 };
static int				monitorRefresh = 0;
extern bool				g_FrameRendered;		// set by Render()

struct viewportData_t
{
	void* 	window;
};

static ImVec2 ToImGui( int x, int y )
{
	return ImVec2( ( float )( x - mainRect.x ), ( float )( y - mainRect.y ) );
}

static void ToScreen( const ImVec2& p, int& x, int& y )
{
	x = ( int )p.x + mainRect.x;
	y = ( int )p.y + mainRect.y;
}

static void* WindowOf( ImGuiViewport* viewport )
{
	if( viewport == ImGui::GetMainViewport() )
	{
		return Sys_MainWindowHandle();
	}
	viewportData_t* data = ( viewportData_t* )viewport->PlatformUserData;
	return data != NULL ? data->window : NULL;
}

static void UpdateMonitors()
{
	ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
	platform.Monitors.resize( 0 );

	sysScreenRect_t full[16], work[16];
	const int count = Sys_GetMonitors( full, work, 16 );
	for( int i = 0; i < count; i++ )
	{
		ImGuiPlatformMonitor monitor;
		monitor.MainPos = ToImGui( full[i].x, full[i].y );
		monitor.MainSize = ImVec2( ( float )full[i].w, ( float )full[i].h );
		monitor.WorkPos = ToImGui( work[i].x, work[i].y );
		monitor.WorkSize = ImVec2( ( float )work[i].w, ( float )work[i].h );
		monitor.DpiScale = 1.0f;
		platform.Monitors.push_back( monitor );
	}
	if( platform.Monitors.Size == 0 )
	{
		// ImGui needs at least one: use the main window
		ImGuiPlatformMonitor monitor;
		monitor.MainPos = monitor.WorkPos = ImVec2( 0, 0 );
		monitor.MainSize = monitor.WorkSize = ImVec2( ( float )Max( 1, mainRect.w ), ( float )Max( 1, mainRect.h ) );
		platform.Monitors.push_back( monitor );
	}
}

/*
-------------------------------------------------------------------------------
	platform callbacks
-------------------------------------------------------------------------------
*/

static void Platform_CreateWindow( ImGuiViewport* viewport )
{
	viewportData_t* data = IM_NEW( viewportData_t )();
	sysScreenRect_t rect;
	ToScreen( viewport->Pos, rect.x, rect.y );
	rect.w = ( int )viewport->Size.x;
	rect.h = ( int )viewport->Size.y;
	data->window = Sys_ExtraWindowCreate( rect, Sys_MainWindowHandle() );
	if( editor_debugPanels.GetBool() )
	{
		common->Printf( "Panels: new window at %d,%d %dx%d -> %p\n", rect.x, rect.y, rect.w, rect.h, data->window );
	}
	viewport->PlatformUserData = data;
	viewport->PlatformHandle = viewport->PlatformHandleRaw = data->window;
}

static void Platform_DestroyWindow( ImGuiViewport* viewport )
{
	viewportData_t* data = ( viewportData_t* )viewport->PlatformUserData;
	if( data != NULL )
	{
		if( data->window != NULL )
		{
			Sys_ExtraWindowDestroy( data->window );
		}
		IM_DELETE( data );
	}
	viewport->PlatformUserData = viewport->PlatformHandle = viewport->PlatformHandleRaw = NULL;
}

static void Platform_ShowWindow( ImGuiViewport* viewport )
{
	if( void* window = WindowOf( viewport ) )
	{
		Sys_ExtraWindowShow( window, ( viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing ) == 0 );
	}
}

static void Platform_SetWindowPos( ImGuiViewport* viewport, ImVec2 pos )
{
	if( void* window = WindowOf( viewport ) )
	{
		int x, y;
		ToScreen( pos, x, y );
		Sys_ExtraWindowSetPos( window, x, y );
	}
}

static ImVec2 Platform_GetWindowPos( ImGuiViewport* viewport )
{
	if( viewport == ImGui::GetMainViewport() )
	{
		return ImVec2( 0, 0 );
	}
	sysScreenRect_t rect;
	void* window = WindowOf( viewport );
	if( window != NULL && Sys_ExtraWindowGetRect( window, rect ) )
	{
		return ToImGui( rect.x, rect.y );
	}
	return viewport->Pos;
}

static void Platform_SetWindowSize( ImGuiViewport* viewport, ImVec2 size )
{
	if( void* window = WindowOf( viewport ) )
	{
		Sys_ExtraWindowSetSize( window, ( int )size.x, ( int )size.y );
	}
}

static ImVec2 Platform_GetWindowSize( ImGuiViewport* viewport )
{
	if( viewport == ImGui::GetMainViewport() )
	{
		return ImGui::GetIO().DisplaySize;
	}
	sysScreenRect_t rect;
	void* window = WindowOf( viewport );
	if( window != NULL && Sys_ExtraWindowGetRect( window, rect ) )
	{
		return ImVec2( ( float )rect.w, ( float )rect.h );
	}
	return viewport->Size;
}

static void Platform_SetWindowFocus( ImGuiViewport* viewport )
{
	if( void* window = WindowOf( viewport ) )
	{
		Sys_ExtraWindowFocus( window );
	}
}

static bool Platform_GetWindowFocus( ImGuiViewport* viewport )
{
	void* window = WindowOf( viewport );
	return window != NULL && Sys_ExtraWindowHasFocus( window );
}

static bool Platform_GetWindowMinimized( ImGuiViewport* viewport )
{
	void* window = WindowOf( viewport );
	return window != NULL && Sys_ExtraWindowIsMinimized( window );
}

static void Platform_SetWindowTitle( ImGuiViewport* viewport, const char* title )
{
	if( viewport != ImGui::GetMainViewport() )
	{
		if( void* window = WindowOf( viewport ) )
		{
			// "Inspector###Inspector" -> "GrizzTech4 - Inspector"
			idStr text = title;
			const int hashes = text.Find( "##" );
			if( hashes >= 0 )
			{
				text = text.Left( hashes );
			}
			Sys_ExtraWindowSetTitle( window, va( "GrizzTech4 - %s", text.c_str() ) );
		}
	}
}

static void Platform_UpdateWindow( ImGuiViewport* viewport )
{
	if( viewport != ImGui::GetMainViewport() )
	{
		if( void* window = WindowOf( viewport ) )
		{
			Sys_ExtraWindowSetNoInputs( window, ( viewport->Flags & ImGuiViewportFlags_NoInputs ) != 0 );
		}
	}
}

/*
-------------------------------------------------------------------------------
	renderer callbacks
-------------------------------------------------------------------------------
*/

static void Renderer_CreateWindow( ImGuiViewport* viewport )
{
	viewport->RendererUserData = ImGuiViewportRenderer::CreateWindowTarget( viewport->PlatformHandle, ( int )viewport->Size.x, ( int )viewport->Size.y );
}

static void Renderer_DestroyWindow( ImGuiViewport* viewport )
{
	if( viewport->RendererUserData != NULL )
	{
		ImGuiViewportRenderer::DestroyWindowTarget( viewport->RendererUserData );
	}
	viewport->RendererUserData = NULL;
}

static void Renderer_SetWindowSize( ImGuiViewport* viewport, ImVec2 size )
{
	ImGuiViewportRenderer::ResizeWindowTarget( viewport->RendererUserData, ( int )size.x, ( int )size.y );
}

static void Renderer_RenderWindow( ImGuiViewport* viewport, void* )
{
	ImGuiViewportRenderer::RenderWindow( viewport->RendererUserData, viewport->DrawData );
}

/*
-------------------------------------------------------------------------------
	frame
-------------------------------------------------------------------------------
*/

void InitViewports()
{
	viewportsActive = false;
	if( !GrizzEditor::IsEditorMode() || !editor_detachablePanels.GetBool() || !ImGuiViewportRenderer::Supported() ||
			Sys_MainWindowHandle() == NULL || !Sys_MainWindowRect( mainRect ) )
	{
		return;
	}

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;
#if defined( _WIN32 )
	// Windows reports the window under the mouse, skipping panels being dragged
	io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport;
	hoverFromPlatform = true;
#endif
	io.ConfigViewportsNoTaskBarIcon = true;
	io.ConfigViewportsNoDecoration = true;
	io.ConfigViewportsNoAutoMerge = false;

	ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
	platform.Platform_CreateWindow = Platform_CreateWindow;
	platform.Platform_DestroyWindow = Platform_DestroyWindow;
	platform.Platform_ShowWindow = Platform_ShowWindow;
	platform.Platform_SetWindowPos = Platform_SetWindowPos;
	platform.Platform_GetWindowPos = Platform_GetWindowPos;
	platform.Platform_SetWindowSize = Platform_SetWindowSize;
	platform.Platform_GetWindowSize = Platform_GetWindowSize;
	platform.Platform_SetWindowFocus = Platform_SetWindowFocus;
	platform.Platform_GetWindowFocus = Platform_GetWindowFocus;
	platform.Platform_GetWindowMinimized = Platform_GetWindowMinimized;
	platform.Platform_SetWindowTitle = Platform_SetWindowTitle;
	platform.Platform_UpdateWindow = Platform_UpdateWindow;
	platform.Renderer_CreateWindow = Renderer_CreateWindow;
	platform.Renderer_DestroyWindow = Renderer_DestroyWindow;
	platform.Renderer_SetWindowSize = Renderer_SetWindowSize;
	platform.Renderer_RenderWindow = Renderer_RenderWindow;

	ImGuiViewport* main = ImGui::GetMainViewport();
	main->PlatformHandle = main->PlatformHandleRaw = Sys_MainWindowHandle();

	UpdateMonitors();
	viewportsActive = true;
	if( editor_debugPanels.GetBool() )
	{
		common->Printf( "Panels: on; main window at %d,%d %dx%d, %d monitors\n", mainRect.x, mainRect.y, mainRect.w, mainRect.h, platform.Monitors.Size );
	}
}

bool ViewportsActive()
{
	return viewportsActive;
}

// before ImGui::NewFrame: follow the main window, pass on window events, and say where the mouse is
void ViewportsNewFrame( bool pollMouse, ImVec2& mousePos )
{
	if( !viewportsActive )
	{
		return;
	}

	ImGuiPlatformIO& platform = ImGui::GetPlatformIO();

	// the main window moved: everything else is now somewhere else relative to it
	sysScreenRect_t rect;
	if( Sys_MainWindowRect( rect ) )
	{
		const bool moved = rect.x != mainRect.x || rect.y != mainRect.y;
		mainRect = rect;
		if( moved )
		{
			for( int i = 1; i < platform.Viewports.Size; i++ )
			{
				platform.Viewports[i]->PlatformRequestMove = true;
			}
			UpdateMonitors();
		}
	}
	if( ++monitorRefresh >= 120 )
	{
		monitorRefresh = 0;
		UpdateMonitors();
	}

	for( int i = 1; i < platform.Viewports.Size; i++ )
	{
		ImGuiViewport* viewport = platform.Viewports[i];
		void* window = WindowOf( viewport );
		if( window == NULL )
		{
			continue;
		}
		sysExtraWindowEvents_t events;
		Sys_ExtraWindowTakeEvents( window, events );
		viewport->PlatformRequestClose |= events.closeRequested;
		viewport->PlatformRequestMove |= events.moved;
		viewport->PlatformRequestResize |= events.resized;
	}

	ImGuiIO& io = ImGui::GetIO();
	if( pollMouse )
	{
		int x, y;
		if( Sys_GetCursorScreenPos( x, y ) )
		{
			mousePos = ToImGui( x, y );
		}
	}
	if( hoverFromPlatform )
	{
		void* window = Sys_WindowUnderCursor();
		ImGuiViewport* viewport = window != NULL ? ImGui::FindViewportByPlatformHandle( window ) : NULL;
		io.MouseHoveredViewport = viewport != NULL ? viewport->ID : 0;
	}

	// a drag that leaves our windows still ends where it should
	Sys_CaptureMouseAllWindows( ImGui::IsAnyMouseDown() );

	if( editor_debugPanels.GetBool() )
	{
		common->Printf( "Panels: mouse %.0f,%.0f down %d, %d viewports, main at %d,%d\n", mousePos.x, mousePos.y, ImGui::IsAnyMouseDown() ? 1 : 0, platform.Viewports.Size, mainRect.x, mainRect.y );
	}
}

void RenderPlatformWindows()
{
	if( !viewportsActive || !g_FrameRendered )
	{
		return;
	}
	g_FrameRendered = false;
	ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault();
}

void ShutdownViewports()
{
	if( viewportsActive && ImGui::GetCurrentContext() != NULL )
	{
		ImGui::DestroyPlatformWindows();
		ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
	}
	viewportsActive = false;
	ImGuiViewportRenderer::Shutdown();
}

} // namespace ImGuiHook
