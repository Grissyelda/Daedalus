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

#ifndef __DAEDALUS_EDITOR_SHAPES_H__
#define __DAEDALUS_EDITOR_SHAPES_H__

/*
===============================================================================

	Ready-made brush shapes.

	Each shape fills a box. Shapes that need more than one brush (stairs, arches,
	rooms) return several; every brush is convex, as the map compiler requires.
	Stairs and ramps rise toward +X and arches span X; turn them afterwards with
	the rotate tool.

===============================================================================
*/

namespace DaedalusEditor
{

enum shapeType_t
{
	SHAPE_BOX,
	SHAPE_ROOM,
	SHAPE_WEDGE,
	SHAPE_CYLINDER,
	SHAPE_CONE,
	SHAPE_STAIRS,
	SHAPE_ARCH,
	SHAPE_COUNT
};

struct ShapeSettings
{
	int		sides = 12;			// cylinders and cones
	int		steps = 8;			// stairs
	int		segments = 8;		// arches
	float	thickness = 16.0f;	// arch ring and room walls
};

const char* 	ShapeName( shapeType_t type );
const char* 	ShapeHelp( shapeType_t type );
idVec3			ShapeDefaultSize( shapeType_t type );

// appends new brushes (owned by the caller) to out; false if the settings can't make the shape
bool			MakeShape( shapeType_t type, const ShapeSettings& settings, const idBounds& box, const char* material, idList<idMapBrush*>& out );

}

#endif /* !__DAEDALUS_EDITOR_SHAPES_H__ */
