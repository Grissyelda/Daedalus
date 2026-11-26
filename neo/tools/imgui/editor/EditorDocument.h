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

struct Selection
{
	int			entity = -1;		// index into the map's entities, -1 = nothing
	int			primitive = -1;		// brush or patch within that entity, -1 = the whole entity

	bool		IsEmpty() const
	{
		return entity < 0;
	}
	bool		IsPrimitive() const
	{
		return entity >= 0 && primitive >= 0;
	}
	void		Clear()
	{
		entity = -1;
		primitive = -1;
	}
	bool		operator==( const Selection& other ) const
	{
		return entity == other.entity && primitive == other.primitive;
	}
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

	Selection			selection;

	// selection helpers
	idMapEntity* 		SelectedEntity() const;
	idMapPrimitive* 	SelectedPrimitive() const;
	bool				SelectionIsValid() const;

	// common edits (each records its own undo step)
	void				DeleteSelection();
	void				DuplicateSelection( const idVec3& offset );
	void				TranslateSelection( const idVec3& delta );	// no undo step: callers wrap it
	int					AddEntity( const char* classname, const idVec3& origin );	// records an undo step
	int					AddBoxBrush( const idVec3& mins, const idVec3& maxs, const char* material );	// into the level; records an undo step

private:
	struct State
	{
		idStr			label;
		idStr			text;
		Selection		selection;
	};

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
	Selection			pendingSelection;
	bool				changeOpen;
};

extern Document document;

// helpers shared by the editor
bool				BrushBounds( const idMapBrush* brush, idBounds& bounds );
bool				BrushSideWinding( const idMapBrush* brush, int side, idWinding& w );
bool				PrimitiveBounds( const idMapPrimitive* prim, idBounds& bounds );
bool				EntityBounds( const idMapEntity* ent, idBounds& bounds );

}

#endif /* !__GRIZZ_EDITOR_DOCUMENT_H__ */
