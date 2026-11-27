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

#ifndef __EDITOR_PREFABS_H__
#define __EDITOR_PREFABS_H__

#include "EditorDocument.h"

/*
===============================================================================

	Pieces (prefabs)

	A piece is a small map in maps/prefabs/<name>.map, built around its own
	origin. Placing it in a level adds an anchor (a func_prefab point entity
	holding the piece's name and where it's placed) and a copy of everything in
	the piece, tagged so the editor knows what belongs to which copy:

		entities	"prefab_owner" <anchor name>, "prefab_part" <name in the piece>
		brushes		the same keys, on the brush itself (brushDef3 keeps them)

	The game and dmap see ordinary brushes and entities; the game skips the
	anchors and dmap doesn't flood from them ("noFlood").

	Copies stay linked: when a piece changes, every copy is rebuilt from it.
	What was changed in one copy only is an override, listed in
	"prefab_overrides" on the changed thing (setting names, or "brush" for its
	shape and paint), and survives the rebuild. Parts deleted from one copy are
	listed in the anchor's "prefab_removed". Pieces can contain pieces.

===============================================================================
*/

namespace DaedalusEditor
{

#define PREFAB_CLASS		"func_prefab"
#define PREFAB_FOLDER		"prefabs"			// under maps/
#define KEY_PREFAB			"prefab"
#define KEY_OWNER			"prefab_owner"
#define KEY_PART			"prefab_part"
#define KEY_OVERRIDES		"prefab_overrides"
#define KEY_REMOVED			"prefab_removed"
#define KEY_PLACED			"prefab_placed"

bool			IsPrefabAnchor( const idMapEntity* ent );
const char* 	PrefabOwner( const idMapFile* map, const Selection& sel );		// "" when it isn't part of a piece

// the anchor of the outermost piece copy the selection belongs to; -1 if none
int				TopAnchorOf( const idMapFile* map, const Selection& sel );
int				FindAnchor( const idMapFile* map, const char* name );

// The anchors from the selection's direct owner up to the outermost copy.
void			AnchorChain( const idMapFile* map, const Selection& sel, idList<int>& anchors );

// everything in one copy (the anchor first), nested copies included
void			PieceItems( const idMapFile* map, int anchor, idList<Selection>& items );

// What a click on sel selects. editing: the anchor of the copy being edited in place, or -1.
// Outside edit mode a click takes the whole outermost copy; while editing, the part itself or
// the nested copy it belongs to; outside the copy being edited, nothing.
void			GrabSelection( const idMapFile* map, const Selection& sel, int editing, idList<Selection>& out );

// piece files
void			ListPrefabs( idStrList& names );
bool			PrefabExists( const char* name );
const char* 	PrefabMapName( const char* name );		// "prefabs/<name>", for the document
void			ForgetPrefab( const char* name );		// drop the cached copy (it was rewritten)
void			ForgetAllPrefabs();

// Told about every piece file written (the document records it, so undo puts the old one back).
// The previous version is also kept on disk as maps/prefabs/<name>.bak.
extern void		( *prefabFileWritten )( const char* relativePath, bool existedBefore, const idStr& before, const idStr& after );

// Saves the selected items as a new piece around origin, and puts a linked copy where they were.
// No undo step of its own. Returns the anchor, or -1 (error says why).
int				MakePrefab( Document& doc, const char* name, const idVec3& origin, idStr& error );

// Adds a linked copy of a piece. No undo step of its own. Returns the anchor or -1.
int				PlacePrefab( idMapFile* map, const char* name, const idVec3& origin, const idMat3& axis );

// Rebuilds copies from their pieces, keeping overrides. Returns how many things changed.
int				SyncPrefabs( idMapFile* map, const char* onlyPrefab = NULL );
int				SyncPiece( idMapFile* map, int anchor );

// After an edit: records what was changed in copies as overrides, and rebuilds copies that
// moved as a whole. skipAnchor: a copy being edited in place, left alone.
void			TrackPrefabEdits( idMapFile* map, const char* skipAnchor, const idDict& duplicateRenames, const idList<Selection>& duplicated );

// What a copy changed before an edit in place started, so the edit can leave those changes
// this copy's own when it's saved to every copy.
struct pieceEditStart_t
{
	idStrList		keys;		// "owner|part" of things with overrides
	idStrList		states;		// what each was like
	idStrList		removed;	// the copy's deleted parts
};
void			SnapshotCopyChanges( const idMapFile* map, int anchor, pieceEditStart_t& start );

// Writes a copy back into its piece (edit in place, "Apply to all copies") and rebuilds every
// copy in the map. The copy's overrides become the piece's, except those in start that the
// edit didn't touch: they stay this copy's own.
bool			WritePrefabFromCopy( idMapFile* map, int anchor, idStr& error, const pieceEditStart_t* start = NULL );

// per-thing overrides
void			GetOverrides( const idMapFile* map, const Selection& sel, idStrList& keys );
void			RevertOverrides( idMapFile* map, const Selection& sel );		// back to the piece's values
bool			ApplyOverridesToPiece( idMapFile* map, const Selection& sel, idStr& error );	// into the piece, for every copy

// a copy's things lose their links and become ordinary level things; the anchor goes
void			UnlinkPiece( idMapFile* map, int anchor );

} // namespace DaedalusEditor

#endif // !__EDITOR_PREFABS_H__
