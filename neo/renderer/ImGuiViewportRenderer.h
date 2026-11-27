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

#ifndef __IMGUI_VIEWPORT_RENDERER_H__
#define __IMGUI_VIEWPORT_RENDERER_H__

struct ImDrawData;

/*
===============================================================================

	Draws ImGui into the extra OS windows the editor's panels move into when
	they're dragged out of the main window (the main window's ImGui goes
	through the gui model like the rest of the 2D drawing).

	Call these from the main thread, outside the render back end.

===============================================================================
*/

namespace ImGuiViewportRenderer
{

bool	Supported();

// a swap chain and buffers for a window; the handle is opaque
void* 	CreateWindowTarget( void* windowHandle, int width, int height );
void	ResizeWindowTarget( void* target, int width, int height );
void	DestroyWindowTarget( void* target );

// draws and presents
void	RenderWindow( void* target, ImDrawData* drawData );

// frees the shared GPU objects; the targets must be gone first
void	Shutdown();

}

#endif /* !__IMGUI_VIEWPORT_RENDERER_H__ */
