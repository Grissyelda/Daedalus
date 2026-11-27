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

#if defined( GRIZZ_EDITOR )

#include "EditorDocument.h"
#include "EditorBrushOps.h"

namespace GrizzEditor
{

static const float PLANE_EPSILON = 0.01f;
static const float POINT_EPSILON = 0.1f;

static void CopySideLook( idMapBrushSide* to, const idMapBrushSide* from )
{
	idVec3 m0, m1;
	const_cast<idMapBrushSide*>( from )->GetTextureMatrix( m0, m1 );
	idVec3 mat[2] = { m0, m1 };
	to->SetMaterial( from->GetMaterial() );
	to->SetTextureMatrix( mat );
}

idMapBrush* CopyBrush( const idMapBrush* brush )
{
	idMapBrush* copy = new( TAG_SYSTEM ) idMapBrush();
	copy->epairs.Copy( brush->epairs );
	for( int i = 0; i < brush->GetNumSides(); i++ )
	{
		idMapBrushSide* side = new( TAG_SYSTEM ) idMapBrushSide();
		side->SetPlane( brush->GetSide( i )->GetPlane() );
		CopySideLook( side, brush->GetSide( i ) );
		copy->AddSide( side );
	}
	return copy;
}

bool CleanBrush( idMapBrush* brush )
{
	idList<int> keep;
	idBounds bounds;
	bounds.Clear();
	for( int i = 0; i < brush->GetNumSides(); i++ )
	{
		idWinding w;
		if( BrushSideWinding( brush, i, w ) && w.GetArea() > 0.1f )
		{
			keep.Append( i );
			for( int j = 0; j < w.GetNumPoints(); j++ )
			{
				bounds.AddPoint( w[j].ToVec3() );
			}
		}
	}

	if( keep.Num() < 4 || bounds.IsCleared() )
	{
		return false;
	}
	const idVec3 size = bounds[1] - bounds[0];
	if( size.x < POINT_EPSILON || size.y < POINT_EPSILON || size.z < POINT_EPSILON )
	{
		return false;
	}

	for( int i = brush->GetNumSides() - 1; i >= 0; i-- )
	{
		if( keep.FindIndex( i ) < 0 )
		{
			brush->RemoveSide( i );
		}
	}
	return true;
}

void BrushVertices( const idMapBrush* brush, idList<idVec3>& points )
{
	points.Clear();
	for( int i = 0; i < brush->GetNumSides(); i++ )
	{
		idWinding w;
		if( !BrushSideWinding( brush, i, w ) )
		{
			continue;
		}
		for( int j = 0; j < w.GetNumPoints(); j++ )
		{
			const idVec3 p = w[j].ToVec3();
			bool found = false;
			for( int k = 0; k < points.Num() && !found; k++ )
			{
				found = points[k].Compare( p, POINT_EPSILON );
			}
			if( !found )
			{
				points.Append( p );
			}
		}
	}
}

// the brush with one more face; NULL when nothing solid is left
static idMapBrush* BrushWithPlane( const idMapBrush* brush, const idPlane& plane, const idMapBrushSide* look )
{
	idMapBrush* piece = CopyBrush( brush );
	idMapBrushSide* cap = new( TAG_SYSTEM ) idMapBrushSide();
	cap->SetPlane( plane );
	CopySideLook( cap, look );
	piece->AddSide( cap );
	if( !CleanBrush( piece ) )
	{
		delete piece;
		return NULL;
	}
	return piece;
}

void SplitBrush( const idMapBrush* brush, const idPlane& plane, const idMapBrushSide* capSide, idMapBrush** front, idMapBrush** back )
{
	const idMapBrushSide* look = ( capSide != NULL ) ? capSide : brush->GetSide( 0 );

	// does the plane cross the brush at all?
	idList<idVec3> points;
	BrushVertices( brush, points );
	bool anyFront = false, anyBack = false;
	for( int i = 0; i < points.Num(); i++ )
	{
		const float d = plane.Distance( points[i] );
		anyFront |= ( d > PLANE_EPSILON );
		anyBack |= ( d < -PLANE_EPSILON );
	}

	*front = NULL;
	*back = NULL;
	if( !anyFront )
	{
		*back = CopyBrush( brush );
		return;
	}
	if( !anyBack )
	{
		*front = CopyBrush( brush );
		return;
	}

	*back = BrushWithPlane( brush, plane, look );
	*front = BrushWithPlane( brush, -plane, look );
}

bool SubtractBrush( const idMapBrush* target, const idMapBrush* cutter, idList<idMapBrush*>& out )
{
	idBounds targetBounds, cutterBounds;
	if( !BrushBounds( target, targetBounds ) || !BrushBounds( cutter, cutterBounds ) )
	{
		return false;
	}
	if( !targetBounds.IntersectsBounds( cutterBounds ) )
	{
		return false;
	}

	// peel off the part of the target outside each of the cutter's faces in turn;
	// what's left at the end is inside the cutter and goes away
	idList<idMapBrush*> pieces;
	idMapBrush* rest = CopyBrush( target );
	for( int i = 0; i < cutter->GetNumSides(); i++ )
	{
		const idMapBrushSide* side = cutter->GetSide( i );
		idMapBrush* front = NULL;
		idMapBrush* back = NULL;
		SplitBrush( rest, side->GetPlane(), side, &front, &back );
		delete rest;

		if( back == NULL )
		{
			// entirely outside this face: they don't overlap after all
			delete front;
			pieces.DeleteContents( true );
			return false;
		}
		if( front != NULL )
		{
			pieces.Append( front );
		}
		rest = back;
	}
	delete rest;

	out.Append( pieces );
	return true;
}

idMapBrush* HullBrush( const idList<idVec3>& points, const idMapBrush* look )
{
	if( points.Num() < 4 )
	{
		return NULL;
	}

	// every plane through three points with all the others behind it is a face
	idList<idPlane> planes;
	for( int a = 0; a < points.Num(); a++ )
	{
		for( int b = a + 1; b < points.Num(); b++ )
		{
			for( int c = b + 1; c < points.Num(); c++ )
			{
				idVec3 normal = ( points[b] - points[a] ).Cross( points[c] - points[a] );
				if( normal.Normalize() < 1e-6f )
				{
					continue;
				}
				idPlane plane;
				plane.SetNormal( normal );
				plane[3] = -( normal * points[a] );

				bool anyFront = false, anyBack = false;
				for( int k = 0; k < points.Num() && !( anyFront && anyBack ); k++ )
				{
					const float d = plane.Distance( points[k] );
					anyFront |= ( d > PLANE_EPSILON );
					anyBack |= ( d < -PLANE_EPSILON );
				}
				if( anyFront && anyBack )
				{
					continue;
				}
				if( anyFront )
				{
					plane = -plane;
				}

				bool duplicate = false;
				for( int k = 0; k < planes.Num() && !duplicate; k++ )
				{
					duplicate = planes[k].Compare( plane, 0.001f, 0.05f );
				}
				if( !duplicate )
				{
					planes.Append( plane );
				}
			}
		}
	}

	idMapBrush* brush = new( TAG_SYSTEM ) idMapBrush();
	for( int i = 0; i < planes.Num(); i++ )
	{
		idMapBrushSide* side = new( TAG_SYSTEM ) idMapBrushSide();
		side->SetPlane( planes[i] );

		// take the look of the old face pointing the most the same way
		int best = -1;
		float bestDot = -2.0f;
		for( int s = 0; look != NULL && s < look->GetNumSides(); s++ )
		{
			const float dot = look->GetSide( s )->GetPlane().Normal() * planes[i].Normal();
			if( dot > bestDot )
			{
				bestDot = dot;
				best = s;
			}
		}
		if( best >= 0 )
		{
			CopySideLook( side, look->GetSide( best ) );
		}
		else
		{
			side->SetMaterial( "textures/dev/grid_wall" );
			SetSideTextureDefault( side );
		}
		brush->AddSide( side );
	}

	if( !CleanBrush( brush ) )
	{
		delete brush;
		return NULL;
	}
	return brush;
}

}

#endif // GRIZZ_EDITOR
