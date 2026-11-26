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

namespace GrizzEditor
{

Document document;

// undo history is capped by memory as well as by count, so big maps stay usable
static const int	MAX_UNDO_STEPS = 200;
static const int	MAX_UNDO_BYTES = 256 * 1024 * 1024;

/*
===============================================================================

	Geometry helpers

===============================================================================
*/

bool BrushSideWinding( const idMapBrush* brush, int side, idWinding& w )
{
	const idPlane& plane = brush->GetSide( side )->GetPlane();
	w.BaseForPlane( plane );

	for( int i = 0; i < brush->GetNumSides() && w.GetNumPoints() > 0; i++ )
	{
		if( i == side )
		{
			continue;
		}
		// keep the part of the face that's inside the other planes
		if( !w.ClipInPlace( -brush->GetSide( i )->GetPlane(), 0.01f ) )
		{
			return false;
		}
	}

	return w.GetNumPoints() >= 3;
}

bool BrushBounds( const idMapBrush* brush, idBounds& bounds )
{
	bounds.Clear();
	idWinding w;
	for( int i = 0; i < brush->GetNumSides(); i++ )
	{
		if( BrushSideWinding( brush, i, w ) )
		{
			for( int j = 0; j < w.GetNumPoints(); j++ )
			{
				bounds.AddPoint( w[j].ToVec3() );
			}
		}
	}
	return !bounds.IsCleared();
}

bool PrimitiveBounds( const idMapPrimitive* prim, idBounds& bounds )
{
	bounds.Clear();
	if( prim->GetType() == idMapPrimitive::TYPE_BRUSH )
	{
		return BrushBounds( static_cast<const idMapBrush*>( prim ), bounds );
	}
	if( prim->GetType() == idMapPrimitive::TYPE_PATCH )
	{
		const idMapPatch* patch = static_cast<const idMapPatch*>( prim );
		for( int i = 0; i < patch->GetWidth() * patch->GetHeight(); i++ )
		{
			bounds.AddPoint( ( *patch )[i].xyz );
		}
		return !bounds.IsCleared();
	}
	return false;
}

bool EntityBounds( const idMapEntity* ent, idBounds& bounds )
{
	bounds.Clear();
	for( int i = 0; i < ent->GetNumPrimitives(); i++ )
	{
		idBounds b;
		if( PrimitiveBounds( ent->GetPrimitive( i ), b ) )
		{
			bounds.AddBounds( b );
		}
	}

	if( bounds.IsCleared() )
	{
		// a point entity: a box around its origin, sized by its definition when it has one
		idVec3 origin = ent->epairs.GetVector( "origin" );
		idVec3 mins( -8, -8, -8 ), maxs( 8, 8, 8 );
		const idDeclEntityDef* def = static_cast<const idDeclEntityDef*>( declManager->FindType( DECL_ENTITYDEF, ent->epairs.GetString( "classname" ), false ) );
		if( def != NULL )
		{
			def->dict.GetVector( "editor_mins", "-8 -8 -8", mins );
			def->dict.GetVector( "editor_maxs", "8 8 8", maxs );
		}
		bounds[0] = origin + mins;
		bounds[1] = origin + maxs;
	}
	return true;
}

static idMapBrush* MakeBoxBrush( const idVec3& mins, const idVec3& maxs, const char* material )
{
	idMapBrush* brush = new( TAG_SYSTEM ) idMapBrush();
	const idVec3 normals[6] = { idVec3( 0, 0, 1 ), idVec3( 0, 0, -1 ), idVec3( 1, 0, 0 ), idVec3( -1, 0, 0 ), idVec3( 0, 1, 0 ), idVec3( 0, -1, 0 ) };
	const float dists[6] = { maxs.z, -mins.z, maxs.x, -mins.x, maxs.y, -mins.y };
	idVec3 texMat[2] = { idVec3( 1.0f / 128.0f, 0, 0 ), idVec3( 0, 1.0f / 128.0f, 0 ) };

	for( int i = 0; i < 6; i++ )
	{
		idMapBrushSide* side = new( TAG_SYSTEM ) idMapBrushSide();
		side->SetPlane( idPlane( normals[i], -dists[i] ) );
		side->SetMaterial( material );
		side->SetTextureMatrix( texMat );
		brush->AddSide( side );
	}
	return brush;
}

/*
===============================================================================

	Document

===============================================================================
*/

Document::Document()
{
	map = NULL;
	dirty = false;
	revision = 0;
	changeOpen = false;
}

Document::~Document()
{
	// Runs at program exit, after idLib has shut down: freeing the map now would touch
	// freed string pools, so leave it to the operating system.
}

void Document::Close()
{
	delete map;
	map = NULL;
	name.Clear();
	dirty = false;
	undoStack.Clear();
	redoStack.Clear();
	selection.Clear();
	changeOpen = false;
	revision++;
}

bool Document::New( const char* mapName )
{
	Close();

	map = new( TAG_SYSTEM ) idMapFile();
	name = mapName;

	idMapEntity* world = new( TAG_SYSTEM ) idMapEntity();
	world->epairs.Set( "classname", "worldspawn" );
	world->AddPrimitive( MakeBoxBrush( idVec3( -512, -512, -16 ), idVec3( 512, 512, 0 ), "textures/dev/grid_floor" ) );
	map->AddEntity( world );

	idMapEntity* start = new( TAG_SYSTEM ) idMapEntity();
	start->epairs.Set( "classname", "info_player_start" );
	start->epairs.Set( "name", "info_player_start_1" );
	start->epairs.Set( "origin", "0 0 1" );
	map->AddEntity( start );

	idMapEntity* light = new( TAG_SYSTEM ) idMapEntity();
	light->epairs.Set( "classname", "light" );
	light->epairs.Set( "name", "light_1" );
	light->epairs.Set( "origin", "0 0 256" );
	light->epairs.Set( "light_radius", "768 768 384" );
	map->AddEntity( light );

	dirty = true;
	revision++;
	common->Printf( "Editor: new map %s\n", name.c_str() );
	return true;
}

bool Document::Load( const char* mapName )
{
	idMapFile* loaded = new( TAG_SYSTEM ) idMapFile();
	if( !loaded->Parse( va( "maps/%s", mapName ), true, false, true ) || loaded->GetNumEntities() == 0 )
	{
		delete loaded;
		common->Warning( "Editor: couldn't open maps/%s.map", mapName );
		return false;
	}

	Close();
	map = loaded;
	name = mapName;
	revision++;
	common->Printf( "Editor: opened %s (%d entities)\n", name.c_str(), map->GetNumEntities() );
	return true;
}

bool Document::Save()
{
	if( map == NULL )
	{
		return false;
	}

	if( !map->Write( va( "maps/%s", name.c_str() ), ".map", true ) )
	{
		common->Warning( "Editor: couldn't save maps/%s.map", name.c_str() );
		return false;
	}

	dirty = false;
	common->Printf( "Editor: saved maps/%s.map\n", name.c_str() );
	return true;
}

bool Document::SaveAs( const char* mapName )
{
	idStr previous = name;
	name = mapName;
	if( !Save() )
	{
		name = previous;
		return false;
	}
	return true;
}

void Document::Snapshot( idStr& text ) const
{
	text.Clear();
	if( map == NULL )
	{
		return;
	}

	idFile_Memory file( "editor_snapshot" );
	map->WriteToFile( &file );
	text.Append( file.GetDataPtr(), file.Length() );
}

bool Document::Restore( const idStr& text )
{
	idMapFile* restored = new( TAG_SYSTEM ) idMapFile();
	if( !restored->ParseFromMemory( text.c_str(), text.Length(), name.c_str() ) )
	{
		delete restored;
		common->Warning( "Editor: couldn't restore the map for undo" );
		return false;
	}

	delete map;
	map = restored;
	dirty = true;
	revision++;
	return true;
}

void Document::TrimUndo()
{
	int bytes = 0;
	for( int i = 0; i < undoStack.Num(); i++ )
	{
		bytes += undoStack[i].text.Length();
	}

	while( undoStack.Num() > 1 && ( undoStack.Num() > MAX_UNDO_STEPS || bytes > MAX_UNDO_BYTES ) )
	{
		bytes -= undoStack[0].text.Length();
		undoStack.RemoveIndex( 0 );
	}
}

void Document::BeginChange( const char* label )
{
	if( map == NULL || changeOpen )
	{
		return;
	}

	changeOpen = true;
	pendingLabel = label;
	pendingSelection = selection;
	Snapshot( pendingText );
}

void Document::EndChange()
{
	if( !changeOpen )
	{
		// a live edit (like a drag) that already recorded its undo step
		dirty = true;
		revision++;
		return;
	}

	changeOpen = false;

	State& state = undoStack.Alloc();
	state.label = pendingLabel;
	state.text = pendingText;
	state.selection = pendingSelection;
	pendingText.Clear();

	redoStack.Clear();
	TrimUndo();

	dirty = true;
	revision++;
}

void Document::CancelChange()
{
	changeOpen = false;
	pendingText.Clear();
}

int Document::AddEntity( const char* classname, const idVec3& origin )
{
	if( map == NULL )
	{
		return -1;
	}

	BeginChange( va( "Add %s", classname ) );
	idMapEntity* ent = new( TAG_SYSTEM ) idMapEntity();
	ent->epairs.Set( "classname", classname );
	ent->epairs.Set( "name", map->GetUniqueEntityName( classname ) );
	ent->epairs.Set( "origin", origin.ToString( 2 ) );
	selection.entity = map->AddEntity( ent );
	selection.primitive = -1;
	EndChange();
	return selection.entity;
}

int Document::AddBoxBrush( const idVec3& mins, const idVec3& maxs, const char* material )
{
	if( map == NULL || map->GetNumEntities() == 0 )
	{
		return -1;
	}

	BeginChange( "Add box brush" );
	idMapEntity* world = map->GetEntity( 0 );
	world->AddPrimitive( MakeBoxBrush( mins, maxs, material ) );
	selection.entity = 0;
	selection.primitive = world->GetNumPrimitives() - 1;
	EndChange();
	return selection.primitive;
}

const char* Document::UndoLabel() const
{
	return undoStack.Num() > 0 ? undoStack[ undoStack.Num() - 1 ].label.c_str() : "";
}

const char* Document::RedoLabel() const
{
	return redoStack.Num() > 0 ? redoStack[ redoStack.Num() - 1 ].label.c_str() : "";
}

bool Document::Undo()
{
	if( undoStack.Num() == 0 )
	{
		return false;
	}

	State current;
	current.label = UndoLabel();
	current.selection = selection;
	Snapshot( current.text );

	State previous = undoStack[ undoStack.Num() - 1 ];
	undoStack.RemoveIndex( undoStack.Num() - 1 );

	if( !Restore( previous.text ) )
	{
		return false;
	}
	selection = previous.selection;
	redoStack.Append( current );
	if( !SelectionIsValid() )
	{
		selection.Clear();
	}
	return true;
}

bool Document::Redo()
{
	if( redoStack.Num() == 0 )
	{
		return false;
	}

	State current;
	current.label = RedoLabel();
	current.selection = selection;
	Snapshot( current.text );

	State next = redoStack[ redoStack.Num() - 1 ];
	redoStack.RemoveIndex( redoStack.Num() - 1 );

	if( !Restore( next.text ) )
	{
		return false;
	}
	undoStack.Append( current );
	selection = next.selection;
	if( !SelectionIsValid() )
	{
		selection.Clear();
	}
	return true;
}

/*
===============================================================================

	Selection

===============================================================================
*/

bool Document::SelectionIsValid() const
{
	if( map == NULL || selection.entity < 0 || selection.entity >= map->GetNumEntities() )
	{
		return false;
	}
	if( selection.primitive >= 0 && selection.primitive >= map->GetEntity( selection.entity )->GetNumPrimitives() )
	{
		return false;
	}
	return true;
}

idMapEntity* Document::SelectedEntity() const
{
	return SelectionIsValid() ? map->GetEntity( selection.entity ) : NULL;
}

idMapPrimitive* Document::SelectedPrimitive() const
{
	if( !SelectionIsValid() || selection.primitive < 0 )
	{
		return NULL;
	}
	return map->GetEntity( selection.entity )->GetPrimitive( selection.primitive );
}

static void TranslatePrimitive( idMapPrimitive* prim, const idVec3& delta )
{
	if( prim->GetType() == idMapPrimitive::TYPE_BRUSH )
	{
		idMapBrush* brush = static_cast<idMapBrush*>( prim );
		for( int i = 0; i < brush->GetNumSides(); i++ )
		{
			idMapBrushSide* side = brush->GetSide( i );
			idPlane plane = side->GetPlane();
			plane.TranslateSelf( delta );
			side->SetPlane( plane );
		}
	}
	else if( prim->GetType() == idMapPrimitive::TYPE_PATCH )
	{
		idMapPatch* patch = static_cast<idMapPatch*>( prim );
		for( int i = 0; i < patch->GetWidth() * patch->GetHeight(); i++ )
		{
			( *patch )[i].xyz += delta;
		}
	}
}

void Document::TranslateSelection( const idVec3& delta )
{
	idMapEntity* ent = SelectedEntity();
	if( ent == NULL )
	{
		return;
	}

	if( selection.IsPrimitive() )
	{
		TranslatePrimitive( ent->GetPrimitive( selection.primitive ), delta );
		return;
	}

	// a whole entity: its origin and every brush it owns (brushes are stored in world space)
	if( selection.entity != 0 )
	{
		if( ent->epairs.FindKey( "origin" ) != NULL || ent->GetNumPrimitives() == 0 )
		{
			idVec3 origin = ent->epairs.GetVector( "origin" ) + delta;
			ent->epairs.Set( "origin", origin.ToString( 2 ) );
		}
	}
	for( int i = 0; i < ent->GetNumPrimitives(); i++ )
	{
		TranslatePrimitive( ent->GetPrimitive( i ), delta );
	}
}

void Document::DeleteSelection()
{
	idMapEntity* ent = SelectedEntity();
	if( ent == NULL )
	{
		return;
	}

	if( selection.IsPrimitive() )
	{
		BeginChange( "Delete brush" );
		ent->RemovePrimitive( selection.primitive );
	}
	else
	{
		if( selection.entity == 0 )
		{
			common->Warning( "Editor: the level itself (worldspawn) can't be deleted" );
			return;
		}
		BeginChange( "Delete entity" );
		map->RemoveEntity( ent );
	}

	selection.Clear();
	EndChange();
}

void Document::DuplicateSelection( const idVec3& offset )
{
	idMapEntity* ent = SelectedEntity();
	if( ent == NULL || ( selection.entity == 0 && !selection.IsPrimitive() ) )
	{
		return;
	}

	BeginChange( "Duplicate" );

	// round-trip through text so every kind of primitive copies exactly
	idFile_Memory file( "editor_duplicate" );
	idMapFile scratch;
	if( selection.IsPrimitive() )
	{
		idMapEntity* holder = new( TAG_SYSTEM ) idMapEntity();
		holder->epairs.Set( "classname", "worldspawn" );
		holder->AddPrimitive( ent->GetPrimitive( selection.primitive ) );
		holder->Write( &file, 0, false );
		holder->DetachPrimitive( 0 );
		delete holder;
	}
	else
	{
		ent->Write( &file, 0, false );
	}

	idStr text = "Version 3\n";
	text.Append( file.GetDataPtr(), file.Length() );
	if( scratch.ParseFromMemory( text.c_str(), text.Length(), "duplicate" ) && scratch.GetNumEntities() == 1 )
	{
		idMapEntity* copy = scratch.GetEntity( 0 );
		if( selection.IsPrimitive() )
		{
			idMapPrimitive* prim = copy->DetachPrimitive( 0 );
			ent->AddPrimitive( prim );
			selection.primitive = ent->GetNumPrimitives() - 1;
		}
		else
		{
			idMapEntity* newEnt = new( TAG_SYSTEM ) idMapEntity();
			newEnt->epairs.Copy( copy->epairs );
			while( copy->GetNumPrimitives() > 0 )
			{
				newEnt->AddPrimitive( copy->DetachPrimitive( 0 ) );
			}
			newEnt->epairs.Set( "name", map->GetUniqueEntityName( newEnt->epairs.GetString( "classname" ) ) );
			selection.entity = map->AddEntity( newEnt );
			selection.primitive = -1;
		}
		TranslateSelection( offset );
	}

	EndChange();
}

} // namespace GrizzEditor

#endif // GRIZZ_EDITOR
