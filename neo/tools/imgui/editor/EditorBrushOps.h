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

#ifndef __DAEDALUS_EDITOR_BRUSHOPS_H__
#define __DAEDALUS_EDITOR_BRUSHOPS_H__

/*
===============================================================================

	Brush geometry operations: copying, splitting by a plane, carving one brush
	out of another, and rebuilding a brush around moved corners.

	Brushes are convex: a list of planes facing outward, each with a material
	and texture matrix. Every function here returns brushes that pass
	CleanBrush(): no sides that don't touch the solid, and real volume.

===============================================================================
*/

namespace DaedalusEditor
{

idMapBrush* 		CopyBrush( const idMapBrush* brush );

// drops sides that don't touch the brush; false when nothing solid is left
bool				CleanBrush( idMapBrush* brush );

// the brush's corners, without duplicates
void				BrushVertices( const idMapBrush* brush, idList<idVec3>& points );

// splits by a plane: back is the part behind it (Distance <= 0), front the part in front.
// Either can come back NULL when the plane misses that side. The new face gets capSide's
// material and alignment when given, otherwise the brush's first face's.
void				SplitBrush( const idMapBrush* brush, const idPlane& plane, const idMapBrushSide* capSide, idMapBrush** front, idMapBrush** back );

// the pieces of target outside cutter; false (and nothing in out) when they don't overlap
bool				SubtractBrush( const idMapBrush* target, const idMapBrush* cutter, idList<idMapBrush*>& out );

// a brush around the points, faces taking their look from the old brush's closest face;
// NULL when the points have no volume
idMapBrush* 		HullBrush( const idList<idVec3>& points, const idMapBrush* look );

}

#endif /* !__DAEDALUS_EDITOR_BRUSHOPS_H__ */
