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

#ifndef __GRIZZ_EDITOR_DOCUMENT_H__
#define __GRIZZ_EDITOR_DOCUMENT_H__

/*
===============================================================================

	The level being edited.

	Holds the open map (an idMapFile), the selection and the undo history.
	Every change follows the same pattern:

		document.BeginChange( "Move" );		// snapshot for undo
		...modify document.Map()...
		document.EndChange();				// mark dirty, rebuild the view

	Undo keeps whole-map snapshots: simple and impossible to get out of step
	with the data, at the cost of memory on very large maps.

===============================================================================
*/

namespace GrizzEditor
{

// one selected thing: an entity, one brush or patch of it, or one face of a brush
struct Selection
{
	int			entity = -1;		// index into the map's entities, -1 = nothing
	int			primitive = -1;		// brush or patch within that entity, -1 = the whole entity
	int			side = -1;			// face of that brush, -1 = the whole brush

	Selection() {}
	Selection( int e, int p = -1, int s = -1 ) : entity( e ), primitive( p ), side( s ) {}

	bool		IsEmpty() const
	{
		return entity < 0;
	}
	bool		IsPrimitive() const
	{
		return entity >= 0 && primitive >= 0;
	}
	bool		IsFace() const
	{
		return entity >= 0 && primitive >= 0 && side >= 0;
	}
	void		Clear()
	{
		entity = -1;
		primitive = -1;
		side = -1;
	}
	bool		operator==( const Selection& other ) const
	{
		return entity == other.entity && primitive == other.primitive && side == other.side;
	}
	bool		operator!=( const Selection& other ) const
	{
		return !( *this == other );
	}
};

// Everything selected. Either objects (entities and brushes) or faces, never a mix.
// The last item added is the "primary" one the Inspector shows.
class SelectionSet
{
public:
	int					Num() const
	{
		return items.Num();
	}
	bool				IsEmpty() const
	{
		return items.Num() == 0;
	}
	const Selection& 	operator[]( int i ) const
	{
		return items[i];
	}
	Selection			Primary() const
	{
		return items.Num() > 0 ? items[ items.Num() - 1 ] : Selection();
	}
	bool				IsFaces() const
	{
		return items.Num() > 0 && items[0].IsFace();
	}
	bool				Contains( const Selection& s ) const
	{
		return items.FindIndex( s ) >= 0;
	}
	void				Clear()
	{
		items.Clear();
	}
	void				Set( const Selection& s )
	{
		items.Clear();
		Add( s );
	}
	void				Add( const Selection& s );		// keeps objects and faces apart
	void				Toggle( const Selection& s );
	void				Remove( const Selection& s )
	{
		items.Remove( s );
	}

	idList<Selection>	items;
};

class Document
{
public:
	Document();
	~Document();

	bool				IsOpen() const
	{
		return map != NULL;
	}
	idMapFile* 			Map() const
	{
		return map;
	}
	const char* 		Name() const
	{
		return name.c_str();
	}
	bool				IsDirty() const
	{
		return dirty;
	}

	// increases with every change, so views know when to rebuild
	int					Revision() const
	{
		return revision;
	}

	// a new level with a floor, a light and a player start
	bool				New( const char* mapName );

	// mapName is relative to maps/ and has no extension, e.g. "testmaps/gt4_test_room"
	bool				Load( const char* mapName );
	bool				Save();
	bool				SaveAs( const char* mapName );
	void				Close();

	// wrap every edit in these
	void				BeginChange( const char* label );
	void				EndChange();
	void				CancelChange();		// a change that started but altered nothing
	bool				IsChangeOpen() const
	{
		return changeOpen;
	}

	bool				CanUndo() const
	{
		return undoStack.Num() > 0;
	}
	bool				CanRedo() const
	{
		return redoStack.Num() > 0;
	}
	const char* 		UndoLabel() const;
	const char* 		RedoLabel() const;
	bool				Undo();
	bool				Redo();

	SelectionSet		selection;

	// selection helpers; these look at the primary selection
	idMapEntity* 		SelectedEntity() const;
	idMapPrimitive* 	SelectedPrimitive() const;
	idMapBrushSide* 	SelectedFace() const;
	bool				SelectionIsValid() const;		// the primary selection still exists
	void				ValidateSelection();			// drops anything that no longer exists
	bool				CanTransformSelection() const;	// something movable is selected
	bool				SelectionBounds( idBounds& bounds ) const;

	// common edits (each records its own undo step)
	void				DeleteSelection();
	void				DuplicateSelection( const idVec3& offset );
	int					AddEntity( const char* classname, const idVec3& origin, const idDict* keys = NULL );	// keys: extra settings
	int					AddBrushes( idList<idMapBrush*>& brushes, const char* label );	// into the level, selected
	void				SetMaterial( const char* material );	// on selected faces, or every face of selected brushes

	// brush tools (EditorDocumentOps.cpp); each records its own undo step
	void				SelectedBrushes( idList<Selection>& out ) const;	// every selected brush, entities expanded
	int					SubtractSelection();		// carves the selected brushes out of the rest; pieces made
	int					ClipSelection( const idPlane& plane, int keep );	// keep: 0 both sides, 1 behind, 2 in front
	int					GroupSelection( const char* classname );	// selected level brushes become an entity
	bool				UngroupSelection();			// a brush entity's brushes go back to the level

	// replaces a brush's faces with shape's (copies); no undo step: callers wrap it
	bool				SetBrushGeometry( int entity, int primitive, const idMapBrush* shape );

	// Moving, turning and resizing. Point p goes to linear * p + translation.
	// No undo step of their own: callers wrap them in BeginChange/EndChange.
	void				TranslateSelection( const idVec3& delta );
	void				TransformSelection( const idMat3& linear, const idVec3& translation );

	// Live drags (the gizmo): the start state is kept so every frame applies the whole
	// transform to the original, which keeps snapping exact and stops errors building up.
	void				BeginTransform();
	void				UpdateTransform( const idMat3& linear, const idVec3& translation );
	void				EndTransform();
	bool				IsTransforming() const
	{
		return transforming;
	}

	// textures keep their place on the surface when brushes move or turn
	bool				textureLock;

private:
	struct State
	{
		idStr			label;
		idStr			text;
		SelectionSet	selection;
	};

	struct SavedSide
	{
		idPlane			plane;
		idVec3			texMat[2];
	};
	struct SavedItem
	{
		int				entity;
		int				primitive;			// -1: the entity's own keys
		idList<SavedSide> sides;			// brushes
		idList<idVec3>	points;				// patches
		idVec3			origin;
		idMat3			axis;
		bool			hasOrigin;
		bool			hasAxis;
	};

	void				SaveTransformStart();
	void				ConvertValveSides();

	void				Snapshot( idStr& text ) const;
	bool				Restore( const idStr& text );
	void				TrimUndo();

	idMapFile* 			map;
	idStr				name;
	bool				dirty;
	int					revision;

	idList<State>		undoStack;
	idList<State>		redoStack;
	idStr				pendingLabel;
	idStr				pendingText;
	SelectionSet		pendingSelection;
	bool				changeOpen;

	bool				transforming;
	idList<SavedItem>	transformStart;
};

extern Document document;

// helpers shared by the editor
bool				BrushBounds( const idMapBrush* brush, idBounds& bounds );
bool				BrushSideWinding( const idMapBrush* brush, int side, idWinding& w );
bool				PrimitiveBounds( const idMapPrimitive* prim, idBounds& bounds );
bool				EntityBounds( const idMapEntity* ent, idBounds& bounds );
bool				EntityAxis( const idMapEntity* ent, idMat3& axis );	// from "rotation" or "angle"; false when it has neither

// brush faces and their textures
void				TextureAxisBase( const idVec3& normal, idVec3& texS, idVec3& texT );
idMapBrush* 		BrushFromPlanes( const idList<idPlane>& planes, const char* material );	// world-aligned 128-unit tiles
void				SetSideTextureDefault( idMapBrushSide* side );

// how a face's texture sits, in units a person can reason about
struct TextureAlign
{
	idVec2			offset;		// in map units
	idVec2			tile;		// map units per repeat; negative flips
	float			rotation;	// degrees
};
TextureAlign		GetTextureAlign( const idMapBrushSide* side );
void				SetTextureAlign( idMapBrushSide* side, const TextureAlign& align );
void				FitTexture( idMapBrush* brush, int side, float repeatsU, float repeatsV );

}

#endif /* !__GRIZZ_EDITOR_DOCUMENT_H__ */
