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

#include "EditorDocument.h"
#include "EditorShapes.h"

namespace DaedalusEditor
{

const char* ShapeName( shapeType_t type )
{
	switch( type )
	{
		case SHAPE_BOX:
			return "Box";
		case SHAPE_ROOM:
			return "Room";
		case SHAPE_WEDGE:
			return "Ramp";
		case SHAPE_CYLINDER:
			return "Cylinder";
		case SHAPE_CONE:
			return "Cone";
		case SHAPE_STAIRS:
			return "Stairs";
		case SHAPE_ARCH:
			return "Arch";
		default:
			return "?";
	}
}

const char* ShapeHelp( shapeType_t type )
{
	switch( type )
	{
		case SHAPE_BOX:
			return "A solid block. Walls, floors and platforms start here.";
		case SHAPE_ROOM:
			return "A hollow box: floor, ceiling and four walls, closed so it doesn't leak.";
		case SHAPE_WEDGE:
			return "A slope rising toward +X.";
		case SHAPE_CYLINDER:
			return "An upright pillar with flat sides.";
		case SHAPE_CONE:
			return "A pointed pillar.";
		case SHAPE_STAIRS:
			return "Steps rising toward +X, one brush per step.";
		case SHAPE_ARCH:
			return "A half-round arch spanning X, one brush per segment.";
		default:
			return "";
	}
}

idVec3 ShapeDefaultSize( shapeType_t type )
{
	switch( type )
	{
		case SHAPE_ROOM:
			return idVec3( 512, 512, 256 );
		case SHAPE_WEDGE:
			return idVec3( 256, 128, 64 );
		case SHAPE_STAIRS:
			return idVec3( 256, 128, 128 );
		case SHAPE_ARCH:
			return idVec3( 256, 32, 192 );
		default:
			return idVec3( 128, 128, 128 );
	}
}

// a plane through three points, facing away from a point inside the shape
static idPlane PlaneAway( const idVec3& a, const idVec3& b, const idVec3& c, const idVec3& inside )
{
	idVec3 normal = ( b - a ).Cross( c - a );
	normal.Normalize();
	idPlane plane;
	plane.SetNormal( normal );
	plane[3] = -( normal * a );
	if( plane.Distance( inside ) > 0.0f )
	{
		plane = -plane;
	}
	return plane;
}

static void AddBox( const idVec3& mins, const idVec3& maxs, const char* material, idList<idMapBrush*>& out )
{
	idList<idPlane> planes;
	planes.Append( idPlane( idVec3( 0, 0, 1 ), maxs.z ) );
	planes.Append( idPlane( idVec3( 0, 0, -1 ), -mins.z ) );
	planes.Append( idPlane( idVec3( 1, 0, 0 ), maxs.x ) );
	planes.Append( idPlane( idVec3( -1, 0, 0 ), -mins.x ) );
	planes.Append( idPlane( idVec3( 0, 1, 0 ), maxs.y ) );
	planes.Append( idPlane( idVec3( 0, -1, 0 ), -mins.y ) );
	out.Append( BrushFromPlanes( planes, material ) );
}

// points round an ellipse in the box's XY footprint, flat sides facing the axes
static void Ring( const idBounds& box, int sides, float z, idList<idVec3>& points )
{
	const idVec3 center = box.GetCenter();
	const float rx = ( box[1].x - box[0].x ) * 0.5f;
	const float ry = ( box[1].y - box[0].y ) * 0.5f;
	for( int i = 0; i < sides; i++ )
	{
		const float angle = idMath::TWO_PI * ( i + 0.5f ) / sides;
		points.Append( idVec3( center.x + rx * cos( angle ), center.y + ry * sin( angle ), z ) );
	}
}

bool MakeShape( shapeType_t type, const ShapeSettings& settings, const idBounds& box, const char* material, idList<idMapBrush*>& out )
{
	const idVec3& mins = box[0];
	const idVec3& maxs = box[1];
	const idVec3 size = maxs - mins;
	const idVec3 center = box.GetCenter();

	if( size.x < 1.0f || size.y < 1.0f || size.z < 1.0f )
	{
		return false;
	}

	switch( type )
	{
		case SHAPE_BOX:
		{
			AddBox( mins, maxs, material, out );
			return true;
		}

		case SHAPE_ROOM:
		{
			const float t = settings.thickness;
			if( t * 2.0f >= Min( size.x, Min( size.y, size.z ) ) )
			{
				return false;
			}
			AddBox( mins, idVec3( maxs.x, maxs.y, mins.z + t ), material, out );			// floor
			AddBox( idVec3( mins.x, mins.y, maxs.z - t ), maxs, material, out );			// ceiling
			AddBox( idVec3( mins.x, mins.y, mins.z + t ), idVec3( mins.x + t, maxs.y, maxs.z - t ), material, out );
			AddBox( idVec3( maxs.x - t, mins.y, mins.z + t ), idVec3( maxs.x, maxs.y, maxs.z - t ), material, out );
			AddBox( idVec3( mins.x + t, mins.y, mins.z + t ), idVec3( maxs.x - t, mins.y + t, maxs.z - t ), material, out );
			AddBox( idVec3( mins.x + t, maxs.y - t, mins.z + t ), idVec3( maxs.x - t, maxs.y, maxs.z - t ), material, out );
			return true;
		}

		case SHAPE_WEDGE:
		{
			idList<idPlane> planes;
			const idVec3 inside( mins.x + size.x * 0.75f, center.y, mins.z + size.z * 0.25f );
			planes.Append( idPlane( idVec3( 0, 0, -1 ), -mins.z ) );
			planes.Append( idPlane( idVec3( 1, 0, 0 ), maxs.x ) );
			planes.Append( idPlane( idVec3( 0, 1, 0 ), maxs.y ) );
			planes.Append( idPlane( idVec3( 0, -1, 0 ), -mins.y ) );
			planes.Append( PlaneAway( idVec3( mins.x, mins.y, mins.z ), idVec3( maxs.x, mins.y, maxs.z ), idVec3( mins.x, maxs.y, mins.z ), inside ) );
			out.Append( BrushFromPlanes( planes, material ) );
			return true;
		}

		case SHAPE_CYLINDER:
		case SHAPE_CONE:
		{
			const int sides = idMath::ClampInt( 3, 64, settings.sides );
			idList<idVec3> ring;
			Ring( box, sides, mins.z, ring );

			const idVec3 apex( center.x, center.y, maxs.z );
			const idVec3 inside( center.x, center.y, mins.z + size.z * 0.25f );

			idList<idPlane> planes;
			planes.Append( idPlane( idVec3( 0, 0, -1 ), -mins.z ) );
			if( type == SHAPE_CYLINDER )
			{
				planes.Append( idPlane( idVec3( 0, 0, 1 ), maxs.z ) );
			}
			for( int i = 0; i < sides; i++ )
			{
				const idVec3& a = ring[i];
				const idVec3& b = ring[( i + 1 ) % sides];
				const idVec3 c = ( type == SHAPE_CYLINDER ) ? a + idVec3( 0, 0, size.z ) : apex;
				planes.Append( PlaneAway( a, b, c, inside ) );
			}
			out.Append( BrushFromPlanes( planes, material ) );
			return true;
		}

		case SHAPE_STAIRS:
		{
			const int steps = idMath::ClampInt( 1, 64, settings.steps );
			const float run = size.x / steps;
			const float rise = size.z / steps;
			for( int i = 0; i < steps; i++ )
			{
				AddBox( idVec3( mins.x + run * i, mins.y, mins.z ), idVec3( mins.x + run * ( i + 1 ), maxs.y, mins.z + rise * ( i + 1 ) ), material, out );
			}
			return true;
		}

		case SHAPE_ARCH:
		{
			const int segments = idMath::ClampInt( 2, 64, settings.segments );
			const float rx = size.x * 0.5f;
			const float rz = size.z;
			const float t = settings.thickness;
			if( t <= 0.0f || t >= Min( rx, rz ) )
			{
				return false;
			}

			for( int i = 0; i < segments; i++ )
			{
				const float a0 = idMath::PI * i / segments;
				const float a1 = idMath::PI * ( i + 1 ) / segments;
				const idVec3 o0( center.x + rx * cos( a0 ), mins.y, mins.z + rz * sin( a0 ) );
				const idVec3 o1( center.x + rx * cos( a1 ), mins.y, mins.z + rz * sin( a1 ) );
				const idVec3 i0( center.x + ( rx - t ) * cos( a0 ), mins.y, mins.z + ( rz - t ) * sin( a0 ) );
				const idVec3 i1( center.x + ( rx - t ) * cos( a1 ), mins.y, mins.z + ( rz - t ) * sin( a1 ) );
				const idVec3 depth( 0, size.y, 0 );
				const idVec3 inside = ( o0 + o1 + i0 + i1 ) * 0.25f + depth * 0.5f;

				idList<idPlane> planes;
				planes.Append( idPlane( idVec3( 0, -1, 0 ), -mins.y ) );
				planes.Append( idPlane( idVec3( 0, 1, 0 ), maxs.y ) );
				planes.Append( PlaneAway( o0, o1, o0 + depth, inside ) );	// outside curve
				planes.Append( PlaneAway( i0, i1, i0 + depth, inside ) );	// underside
				planes.Append( PlaneAway( o0, i0, o0 + depth, inside ) );	// joint with the previous segment
				planes.Append( PlaneAway( o1, i1, o1 + depth, inside ) );	// joint with the next
				out.Append( BrushFromPlanes( planes, material ) );
			}
			return true;
		}

		default:
			return false;
	}
}

}

#endif // DAEDALUS_EDITOR
