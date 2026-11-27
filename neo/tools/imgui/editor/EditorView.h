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

#ifndef __GRIZZ_EDITOR_VIEW_H__
#define __GRIZZ_EDITOR_VIEW_H__

#include "EditorDocument.h"

/*
===============================================================================

	The editor's 3D view.

	Renders the open document live, without compiling it: every brush becomes
	a render model with its real materials, lights and models are added as in
	the game, and point entities are drawn as coloured boxes. Rebuilt whenever
	the document's revision changes.

	The camera flies like other editors: hold the right mouse button to look,
	WASD/QE to move while it's held, mouse wheel to move forward and back.

===============================================================================
*/

namespace GrizzEditor
{

class EditorCamera
{
public:
	idVec3			origin = idVec3( 0, 0, 64 );
	float			yaw = 0.0f;
	float			pitch = 0.0f;
	float			speed = 384.0f;		// units per second
	float			fov = 90.0f;

	idMat3			Axis() const;
	void			Focus( const idBounds& bounds );
};

class View
{
public:
	View();

	EditorCamera	camera;

	// rebuild from the document when it has changed
	void			Sync( const Document& doc );

	// render the 3D view (fills the screen behind the editor panels)
	void			Draw( const Document& doc );

	// camera movement; call once per frame with ImGui's input state
	void			UpdateCamera( bool viewportHovered );

	// what's under the screen position (pixels), or an empty selection;
	// with faces set, the brush face itself
	Selection		Pick( const Document& doc, float x, float y, bool faces = false ) const;

	// the first brush surface under the screen position; false when the ray hits nothing
	bool			Trace( const Document& doc, float x, float y, idVec3& point, idVec3& normal ) const;

	// where a point lands on screen (pixels); false when it's behind the camera
	bool			WorldToScreen( const idVec3& point, idVec2& screen ) const;

	// a ray through the screen position (pixels)
	void			ScreenRay( float x, float y, idVec3& start, idVec3& dir ) const;

	// fills a renderView_t for the current camera and screen
	void			BuildRenderView( renderView_t& view ) const;

	void			Shutdown();

	bool			wireframe = false;		// draw triangle outlines over everything

private:
	struct Built
	{
		idRenderModel* 	model = NULL;
		qhandle_t		entityDef = -1;
		qhandle_t		lightDef = -1;
	};

	void			FreeAll();
	idRenderModel* 	BuildBrushModel( const idMapEntity* ent, int entityNum );

	idRenderWorld* 	world;
	idList<Built>	built;
	int				builtRevision;
	const idMapFile* builtMap;
};

extern View view;

}

#endif /* !__GRIZZ_EDITOR_VIEW_H__ */
