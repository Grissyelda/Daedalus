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

/*
===============================================================================

	Document: brush tools

	Carving, clipping and grouping change which primitives an entity holds, so
	indices shift. Each tool rebuilds the entity's primitive list in one pass
	and then selects what it made.

===============================================================================
*/

namespace GrizzEditor
{

void Document::SelectedBrushes( idList<Selection>& out ) const
{
	out.Clear();
	if( map == NULL || selection.IsFaces() )
	{
		return;
	}
	for( int i = 0; i < selection.Num(); i++ )
	{
		const Selection& sel = selection[i];
		if( sel.entity < 0 || sel.entity >= map->GetNumEntities() )
		{
			continue;
		}
		const idMapEntity* ent = map->GetEntity( sel.entity );
		for( int p = 0; p < ent->GetNumPrimitives(); p++ )
		{
			if( sel.primitive >= 0 && sel.primitive != p )
			{
				continue;
			}
			if( sel.primitive < 0 && sel.entity == 0 )
			{
				continue;	// never the whole level
			}
			if( ent->GetPrimitive( p )->GetType() == idMapPrimitive::TYPE_BRUSH )
			{
				const Selection brush( sel.entity, p );
				if( out.FindIndex( brush ) < 0 )
				{
					out.Append( brush );
				}
			}
		}
	}
}

// what to put in place of one primitive: nothing, itself, or new brushes
struct Replacement
{
	int						entity;
	int						primitive;
	idList<idMapBrush*>		with;		// empty and remove set: deleted
	bool					remove;
};

// applies the replacements and returns the new selections for the brushes that were added
static void ApplyReplacements( idMapFile* map, idList<Replacement>& replacements, SelectionSet& made )
{
	made.Clear();
	for( int e = 0; e < map->GetNumEntities(); e++ )
	{
		bool touched = false;
		for( int r = 0; r < replacements.Num() && !touched; r++ )
		{
			touched = ( replacements[r].entity == e );
		}
		if( !touched )
		{
			continue;
		}

		idMapEntity* ent = map->GetEntity( e );
		idList<idMapPrimitive*> old;
		while( ent->GetNumPrimitives() > 0 )
		{
			old.Append( ent->DetachPrimitive( 0 ) );
		}

		for( int p = 0; p < old.Num(); p++ )
		{
			int found = -1;
			for( int r = 0; r < replacements.Num(); r++ )
			{
				if( replacements[r].entity == e && replacements[r].primitive == p )
				{
					found = r;
					break;
				}
			}
			if( found < 0 )
			{
				ent->AddPrimitive( old[p] );
				continue;
			}

			Replacement& rep = replacements[found];
			for( int b = 0; b < rep.with.Num(); b++ )
			{
				ent->AddPrimitive( rep.with[b] );
				made.Add( Selection( e, ( e == 0 ) ? ent->GetNumPrimitives() - 1 : -1 ) );
			}
			rep.with.Clear();
			delete old[p];
		}
	}

	// brush entities whose brushes are all gone go too
	for( int e = map->GetNumEntities() - 1; e > 0; e-- )
	{
		idMapEntity* ent = map->GetEntity( e );
		bool hadBrushes = false;
		for( int r = 0; r < replacements.Num() && !hadBrushes; r++ )
		{
			hadBrushes = ( replacements[r].entity == e );
		}
		if( hadBrushes && ent->GetNumPrimitives() == 0 )
		{
			map->RemoveEntity( ent );
			for( int i = made.items.Num() - 1; i >= 0; i-- )
			{
				if( made.items[i].entity == e )
				{
					made.items.RemoveIndex( i );
				}
				else if( made.items[i].entity > e )
				{
					made.items[i].entity--;
				}
			}
		}
	}
}

int Document::SubtractSelection()
{
	idList<Selection> cutters;
	SelectedBrushes( cutters );
	if( cutters.Num() == 0 )
	{
		return 0;
	}

	BeginChange( "Carve" );

	idList<Replacement> replacements;
	int pieces = 0;
	int carved = 0;

	for( int e = 0; e < map->GetNumEntities(); e++ )
	{
		const idMapEntity* ent = map->GetEntity( e );
		for( int p = 0; p < ent->GetNumPrimitives(); p++ )
		{
			const Selection here( e, p );
			if( cutters.FindIndex( here ) >= 0 || ent->GetPrimitive( p )->GetType() != idMapPrimitive::TYPE_BRUSH )
			{
				continue;
			}

			// cut by each cutter in turn, keeping every piece that's left
			const idMapBrush* target = static_cast<const idMapBrush*>( ent->GetPrimitive( p ) );
			idList<idMapBrush*> current;
			current.Append( CopyBrush( target ) );
			bool changed = false;

			for( int c = 0; c < cutters.Num(); c++ )
			{
				const idMapBrush* cutter = static_cast<const idMapBrush*>( map->GetEntity( cutters[c].entity )->GetPrimitive( cutters[c].primitive ) );
				idList<idMapBrush*> next;
				for( int k = 0; k < current.Num(); k++ )
				{
					idList<idMapBrush*> outside;
					if( SubtractBrush( current[k], cutter, outside ) )
					{
						next.Append( outside );
						delete current[k];
						changed = true;
					}
					else
					{
						next.Append( current[k] );
					}
				}
				current = next;
			}

			if( !changed )
			{
				current.DeleteContents( true );
				continue;
			}

			Replacement& rep = replacements.Alloc();
			rep.entity = e;
			rep.primitive = p;
			rep.with = current;
			rep.remove = true;
			pieces += current.Num();
			carved++;
		}
	}

	if( carved == 0 )
	{
		CancelChange();
		return 0;
	}

	// the cutters go away
	for( int c = 0; c < cutters.Num(); c++ )
	{
		Replacement& rep = replacements.Alloc();
		rep.entity = cutters[c].entity;
		rep.primitive = cutters[c].primitive;
		rep.remove = true;
	}

	ApplyReplacements( map, replacements, selection );
	selection.Clear();
	EndChange();
	common->Printf( "Editor: carved %d brushes into %d pieces\n", carved, pieces );
	return pieces;
}

int Document::ClipSelection( const idPlane& plane, int keep )
{
	idList<Selection> brushes;
	SelectedBrushes( brushes );
	if( brushes.Num() == 0 )
	{
		return 0;
	}

	BeginChange( "Clip" );

	idList<Replacement> replacements;
	int made = 0;
	for( int i = 0; i < brushes.Num(); i++ )
	{
		const idMapBrush* brush = static_cast<const idMapBrush*>( map->GetEntity( brushes[i].entity )->GetPrimitive( brushes[i].primitive ) );
		idMapBrush* front = NULL;
		idMapBrush* back = NULL;

		// the new faces look like the face nearest the cut
		const idMapBrushSide* look = brush->GetSide( 0 );
		float bestDot = -2.0f;
		for( int s = 0; s < brush->GetNumSides(); s++ )
		{
			const float dot = idMath::Fabs( brush->GetSide( s )->GetPlane().Normal() * plane.Normal() );
			if( dot > bestDot )
			{
				bestDot = dot;
				look = brush->GetSide( s );
			}
		}

		SplitBrush( brush, plane, look, &front, &back );
		if( front == NULL || back == NULL )
		{
			// the cut misses this brush
			delete front;
			delete back;
			continue;
		}

		Replacement& rep = replacements.Alloc();
		rep.entity = brushes[i].entity;
		rep.primitive = brushes[i].primitive;
		rep.remove = true;
		if( keep != 2 )
		{
			rep.with.Append( back );
		}
		else
		{
			delete back;
		}
		if( keep != 1 )
		{
			rep.with.Append( front );
		}
		else
		{
			delete front;
		}
		made += rep.with.Num();
	}

	if( replacements.Num() == 0 )
	{
		CancelChange();
		return 0;
	}

	ApplyReplacements( map, replacements, selection );
	EndChange();
	return made;
}

bool Document::SetBrushGeometry( int entity, int primitive, const idMapBrush* shape )
{
	if( map == NULL || entity < 0 || entity >= map->GetNumEntities() )
	{
		return false;
	}
	idMapEntity* ent = map->GetEntity( entity );
	if( primitive < 0 || primitive >= ent->GetNumPrimitives() || ent->GetPrimitive( primitive )->GetType() != idMapPrimitive::TYPE_BRUSH )
	{
		return false;
	}

	idMapBrush* brush = static_cast<idMapBrush*>( ent->GetPrimitive( primitive ) );
	brush->ClearSides();
	idMapBrush* copy = CopyBrush( shape );
	for( int i = 0; i < copy->GetNumSides(); i++ )
	{
		idMapBrushSide* side = new( TAG_SYSTEM ) idMapBrushSide();
		side->SetPlane( copy->GetSide( i )->GetPlane() );
		side->SetMaterial( copy->GetSide( i )->GetMaterial() );
		idVec3 m0, m1;
		copy->GetSide( i )->GetTextureMatrix( m0, m1 );
		idVec3 mat[2] = { m0, m1 };
		side->SetTextureMatrix( mat );
		brush->AddSide( side );
	}
	delete copy;
	revision++;
	dirty = true;
	return true;
}

int Document::GroupSelection( const char* classname )
{
	if( map == NULL || selection.IsFaces() )
	{
		return -1;
	}

	// only the level's own brushes can be grouped
	idList<int> prims;
	for( int i = 0; i < selection.Num(); i++ )
	{
		if( selection[i].entity == 0 && selection[i].primitive >= 0 && selection[i].primitive < map->GetEntity( 0 )->GetNumPrimitives() )
		{
			prims.AddUnique( selection[i].primitive );
		}
	}
	if( prims.Num() == 0 )
	{
		return -1;
	}

	BeginChange( va( "Make %s", classname ) );

	idMapEntity* world = map->GetEntity( 0 );
	idBounds bounds;
	bounds.Clear();
	for( int i = 0; i < prims.Num(); i++ )
	{
		idBounds b;
		if( PrimitiveBounds( world->GetPrimitive( prims[i] ), b ) )
		{
			bounds.AddBounds( b );
		}
	}

	// highest index first so the rest keep their places
	prims.SortWithTemplate( idSort_QuickDefault<int>() );
	idList<idMapPrimitive*> moved;
	for( int i = prims.Num() - 1; i >= 0; i-- )
	{
		moved.Insert( world->DetachPrimitive( prims[i] ), 0 );
	}

	idMapEntity* ent = new( TAG_SYSTEM ) idMapEntity();
	const idStr name = map->GetUniqueEntityName( classname );
	ent->epairs.Set( "classname", classname );
	ent->epairs.Set( "name", name );
	ent->epairs.Set( "model", name );	// brush entities are drawn from the model dmap names after them

	idVec3 origin = bounds.IsCleared() ? vec3_origin : bounds.GetCenter();
	for( int i = 0; i < 3; i++ )
	{
		origin[i] = idMath::Rint( origin[i] );
	}
	ent->epairs.SetVector( "origin", origin );

	for( int i = 0; i < moved.Num(); i++ )
	{
		ent->AddPrimitive( moved[i] );
	}

	const int index = map->AddEntity( ent );
	selection.Set( Selection( index ) );
	EndChange();
	return index;
}

bool Document::UngroupSelection()
{
	const Selection sel = selection.Primary();
	if( map == NULL || sel.entity <= 0 || sel.primitive >= 0 || sel.entity >= map->GetNumEntities() )
	{
		return false;
	}
	idMapEntity* ent = map->GetEntity( sel.entity );
	if( ent->GetNumPrimitives() == 0 )
	{
		return false;
	}

	BeginChange( "Return brushes to the level" );
	idMapEntity* world = map->GetEntity( 0 );
	SelectionSet made;
	while( ent->GetNumPrimitives() > 0 )
	{
		world->AddPrimitive( ent->DetachPrimitive( 0 ) );
		made.Add( Selection( 0, world->GetNumPrimitives() - 1 ) );
	}
	map->RemoveEntity( ent );
	selection = made;
	EndChange();
	return true;
}

}

#endif // GRIZZ_EDITOR
