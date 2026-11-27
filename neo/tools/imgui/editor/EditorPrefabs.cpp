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

#include "EditorPrefabs.h"

namespace GrizzEditor
{

/*
===============================================================================

	Small helpers

===============================================================================
*/

static const int MAX_NESTING = 8;

bool IsPrefabAnchor( const idMapEntity* ent )
{
	return ent != NULL && idStr::Icmp( ent->epairs.GetString( "classname" ), PREFAB_CLASS ) == 0;
}

// the keys of a thing: an entity's, or a brush's own
static idDict* ItemDict( idMapFile* map, const Selection& sel )
{
	if( map == NULL || sel.entity < 0 || sel.entity >= map->GetNumEntities() )
	{
		return NULL;
	}
	idMapEntity* ent = map->GetEntity( sel.entity );
	if( sel.entity == 0 && sel.primitive >= 0 && sel.primitive < ent->GetNumPrimitives() )
	{
		return &ent->GetPrimitive( sel.primitive )->epairs;
	}
	if( sel.entity > 0 )
	{
		return &ent->epairs;
	}
	return NULL;
}

static const idDict* ItemDict( const idMapFile* map, const Selection& sel )
{
	return ItemDict( const_cast<idMapFile*>( map ), sel );
}

const char* PrefabOwner( const idMapFile* map, const Selection& sel )
{
	const idDict* dict = ItemDict( map, sel );
	return dict != NULL ? dict->GetString( KEY_OWNER ) : "";
}

static void Tokens( const char* list, idStrList& out )
{
	out.Clear();
	idStr token;
	for( const char* c = list; ; c++ )
	{
		if( *c == ' ' || *c == '\0' )
		{
			if( token.Length() > 0 )
			{
				out.AddUnique( token );
				token.Clear();
			}
			if( *c == '\0' )
			{
				break;
			}
			continue;
		}
		token += *c;
	}
}

static idStr JoinTokens( const idStrList& list )
{
	idStr out;
	for( int i = 0; i < list.Num(); i++ )
	{
		if( i > 0 )
		{
			out += " ";
		}
		out += list[i];
	}
	return out;
}

static bool HasToken( const char* list, const char* token )
{
	idStrList tokens;
	Tokens( list, tokens );
	return tokens.FindIndex( token ) >= 0;
}

static void AddToken( idDict& dict, const char* key, const char* token )
{
	idStrList tokens;
	Tokens( dict.GetString( key ), tokens );
	tokens.AddUnique( token );
	dict.Set( key, JoinTokens( tokens ) );
}

// keys the link itself uses, never copied from a piece or counted as overrides
static bool IsLinkKey( const char* key )
{
	return idStr::Icmp( key, "name" ) == 0 || idStr::Icmp( key, KEY_OWNER ) == 0 || idStr::Icmp( key, KEY_PART ) == 0 ||
		   idStr::Icmp( key, KEY_OVERRIDES ) == 0 || idStr::Icmp( key, KEY_PLACED ) == 0;
}

// the same setting, allowing for rounding in numbers ("128 0 0" and "128.001 0 0")
static bool SameValue( const char* a, const char* b )
{
	if( idStr::Cmp( a, b ) == 0 )
	{
		return true;
	}
	float va[16], vb[16];
	int na = 0, nb = 0;
	idLexer la( a, idStr::Length( a ), "value", LEXFL_NOERRORS | LEXFL_NOWARNINGS );
	idLexer lb( b, idStr::Length( b ), "value", LEXFL_NOERRORS | LEXFL_NOWARNINGS );
	idToken token;
	while( na < 16 && la.ReadToken( &token ) )
	{
		if( token.type == TT_PUNCTUATION && token == "-" && la.ReadToken( &token ) && token.type == TT_NUMBER )
		{
			va[na++] = -token.GetFloatValue();
			continue;
		}
		if( token.type != TT_NUMBER )
		{
			return false;
		}
		va[na++] = token.GetFloatValue();
	}
	while( nb < 16 && lb.ReadToken( &token ) )
	{
		if( token.type == TT_PUNCTUATION && token == "-" && lb.ReadToken( &token ) && token.type == TT_NUMBER )
		{
			vb[nb++] = -token.GetFloatValue();
			continue;
		}
		if( token.type != TT_NUMBER )
		{
			return false;
		}
		vb[nb++] = token.GetFloatValue();
	}
	if( na == 0 || na != nb )
	{
		return false;
	}
	for( int i = 0; i < na; i++ )
	{
		if( idMath::Fabs( va[i] - vb[i] ) > 0.05f )
		{
			return false;
		}
	}
	return true;
}

static bool SamePrimitive( const idMapPrimitive* a, const idMapPrimitive* b )
{
	if( a->GetType() != b->GetType() )
	{
		return false;
	}
	if( a->GetType() == idMapPrimitive::TYPE_BRUSH )
	{
		const idMapBrush* ba = static_cast<const idMapBrush*>( a );
		const idMapBrush* bb = static_cast<const idMapBrush*>( b );
		if( ba->GetNumSides() != bb->GetNumSides() )
		{
			return false;
		}
		for( int i = 0; i < ba->GetNumSides(); i++ )
		{
			idMapBrushSide* sa = ba->GetSide( i );
			idMapBrushSide* sb = bb->GetSide( i );
			if( idStr::Icmp( sa->GetMaterial(), sb->GetMaterial() ) != 0 )
			{
				return false;
			}
			if( !sa->GetPlane().Normal().Compare( sb->GetPlane().Normal(), 0.001f ) || idMath::Fabs( sa->GetPlane().Dist() - sb->GetPlane().Dist() ) > 0.05f )
			{
				return false;
			}
			idVec3 ma[2], mb[2];
			sa->GetTextureMatrix( ma[0], ma[1] );
			sb->GetTextureMatrix( mb[0], mb[1] );
			if( !ma[0].Compare( mb[0], 0.002f ) || !ma[1].Compare( mb[1], 0.002f ) )
			{
				return false;
			}
		}
		return true;
	}
	if( a->GetType() == idMapPrimitive::TYPE_PATCH )
	{
		const idMapPatch* pa = static_cast<const idMapPatch*>( a );
		const idMapPatch* pb = static_cast<const idMapPatch*>( b );
		if( pa->GetWidth() != pb->GetWidth() || pa->GetHeight() != pb->GetHeight() || idStr::Icmp( pa->GetMaterial(), pb->GetMaterial() ) != 0 )
		{
			return false;
		}
		for( int i = 0; i < pa->GetWidth() * pa->GetHeight(); i++ )
		{
			if( !( *pa )[i].xyz.Compare( ( *pb )[i].xyz, 0.05f ) )
			{
				return false;
			}
		}
		return true;
	}
	return true;
}

static bool SamePrimitives( const idMapEntity* a, const idMapEntity* b )
{
	if( a->GetNumPrimitives() != b->GetNumPrimitives() )
	{
		return false;
	}
	for( int i = 0; i < a->GetNumPrimitives(); i++ )
	{
		if( !SamePrimitive( a->GetPrimitive( i ), b->GetPrimitive( i ) ) )
		{
			return false;
		}
	}
	return true;
}

static idMapEntity* CopyEntity( const idMapEntity* ent )
{
	idMapEntity* copy = new( TAG_SYSTEM ) idMapEntity();
	copy->epairs = ent->epairs;
	for( int p = 0; p < ent->GetNumPrimitives(); p++ )
	{
		idMapPrimitive* prim = CopyPrimitive( ent->GetPrimitive( p ) );
		if( prim != NULL )
		{
			copy->AddPrimitive( prim );
		}
	}
	return copy;
}

// Where a copy sits. A point in the piece goes to linear * p + origin, with linear the transpose
// of the anchor's axis (an entity's axis rows are its forward, left and up directions).
static void AnchorPlacement( const idMapEntity* anchor, idVec3& origin, idMat3& linear )
{
	origin = anchor->epairs.GetVector( "origin" );
	idMat3 axis;
	EntityAxis( anchor, axis );
	linear = axis.Transpose();
}

// and back: from the level into the piece
static void AnchorInverse( const idMapEntity* anchor, idMat3& linear, idVec3& translation )
{
	const idVec3 origin = anchor->epairs.GetVector( "origin" );
	EntityAxis( anchor, linear );
	translation = -idVec3( linear[0] * origin, linear[1] * origin, linear[2] * origin );
}

// where an anchor was when its copy was last rebuilt, to tell when the whole copy moved
static idStr PlacedString( const idMapEntity* anchor )
{
	return va( "%s|%s|%s", anchor->epairs.GetString( "origin" ), anchor->epairs.GetString( "angle" ), anchor->epairs.GetString( "rotation" ) );
}

static idStr StripPrefix( const char* name, const char* prefix )
{
	const int len = idStr::Length( prefix );
	if( idStr::Cmpn( name, prefix, len ) == 0 && name[len] != '\0' )
	{
		return name + len;
	}
	return name;
}

int FindAnchor( const idMapFile* map, const char* name )
{
	if( map == NULL || name == NULL || name[0] == '\0' )
	{
		return -1;
	}
	for( int e = 1; e < map->GetNumEntities(); e++ )
	{
		const idMapEntity* ent = map->GetEntity( e );
		if( IsPrefabAnchor( ent ) && idStr::Cmp( ent->epairs.GetString( "name" ), name ) == 0 )
		{
			return e;
		}
	}
	return -1;
}

void AnchorChain( const idMapFile* map, const Selection& sel, idList<int>& anchors )
{
	anchors.Clear();
	idStr owner = PrefabOwner( map, sel );
	for( int guard = 0; guard < 32 && owner.Length() > 0; guard++ )
	{
		const int a = FindAnchor( map, owner );
		if( a < 0 || anchors.FindIndex( a ) >= 0 )
		{
			break;
		}
		anchors.Append( a );
		owner = map->GetEntity( a )->epairs.GetString( KEY_OWNER );
	}
}

// the anchors of a selected anchor's own copy and the copies around it
static void ChainIncludingSelf( const idMapFile* map, const Selection& sel, idList<int>& chain )
{
	idList<int> above;
	AnchorChain( map, sel, above );
	chain.Clear();
	if( sel.entity > 0 && sel.primitive < 0 && sel.entity < map->GetNumEntities() && IsPrefabAnchor( map->GetEntity( sel.entity ) ) )
	{
		chain.Append( sel.entity );
	}
	chain.Append( above );
}

int TopAnchorOf( const idMapFile* map, const Selection& sel )
{
	if( map == NULL )
	{
		return -1;
	}
	idList<int> chain;
	ChainIncludingSelf( map, sel, chain );
	return chain.Num() > 0 ? chain[chain.Num() - 1] : -1;
}

// the names of a copy's anchor and every anchor nested in it
static void TreeOwners( const idMapFile* map, const char* anchorName, idStrList& owners )
{
	owners.Clear();
	owners.Append( anchorName );
	bool grew = true;
	while( grew )
	{
		grew = false;
		for( int e = 1; e < map->GetNumEntities(); e++ )
		{
			const idMapEntity* ent = map->GetEntity( e );
			if( IsPrefabAnchor( ent ) && owners.FindIndex( ent->epairs.GetString( KEY_OWNER ) ) >= 0 && owners.FindIndex( ent->epairs.GetString( "name" ) ) < 0 )
			{
				owners.Append( ent->epairs.GetString( "name" ) );
				grew = true;
			}
		}
	}
}

void PieceItems( const idMapFile* map, int anchor, idList<Selection>& items )
{
	items.Clear();
	if( map == NULL || anchor <= 0 || anchor >= map->GetNumEntities() )
	{
		return;
	}
	idStrList owners;
	TreeOwners( map, map->GetEntity( anchor )->epairs.GetString( "name" ), owners );
	items.Append( Selection( anchor ) );
	for( int e = 1; e < map->GetNumEntities(); e++ )
	{
		if( e != anchor && owners.FindIndex( map->GetEntity( e )->epairs.GetString( KEY_OWNER ) ) >= 0 )
		{
			items.Append( Selection( e ) );
		}
	}
	const idMapEntity* world = map->GetEntity( 0 );
	for( int p = 0; p < world->GetNumPrimitives(); p++ )
	{
		if( owners.FindIndex( world->GetPrimitive( p )->epairs.GetString( KEY_OWNER ) ) >= 0 )
		{
			items.Append( Selection( 0, p ) );
		}
	}
}

void GrabSelection( const idMapFile* map, const Selection& sel, int editing, idList<Selection>& out )
{
	out.Clear();
	if( map == NULL || sel.IsEmpty() )
	{
		return;
	}
	if( sel.IsFace() )
	{
		// faces are picked one by one, but only inside the copy being edited
		if( editing < 0 )
		{
			out.Append( sel );
			return;
		}
	}

	idList<int> chain;
	ChainIncludingSelf( map, Selection( sel.entity, sel.primitive ), chain );
	if( chain.Num() == 0 )
	{
		if( editing < 0 )
		{
			out.Append( sel );
		}
		return;		// while editing a piece, the rest of the level stays put
	}
	if( editing < 0 )
	{
		PieceItems( map, chain[chain.Num() - 1], out );
		return;
	}
	const int k = chain.FindIndex( editing );
	if( k < 0 )
	{
		return;
	}
	if( k == 0 )
	{
		if( !( sel.entity == editing && sel.primitive < 0 ) )
		{
			out.Append( sel );		// a part of the piece being edited
		}
		return;
	}
	PieceItems( map, chain[k - 1], out );	// a piece inside it, as a whole
}

/*
===============================================================================

	Piece files

===============================================================================
*/

struct cachedPrefab_t
{
	idStr		name;
	idMapFile* 	map;
	ID_TIME_T	time;
};
static idList<cachedPrefab_t> prefabCache;

const char* PrefabMapName( const char* name )
{
	static idStr mapName;
	mapName = va( "%s/%s", PREFAB_FOLDER, name );
	return mapName.c_str();
}

void ForgetPrefab( const char* name )
{
	for( int i = 0; i < prefabCache.Num(); i++ )
	{
		if( prefabCache[i].name.Icmp( name ) == 0 )
		{
			delete prefabCache[i].map;
			prefabCache.RemoveIndex( i );
			return;
		}
	}
}

void ForgetAllPrefabs()
{
	for( int i = 0; i < prefabCache.Num(); i++ )
	{
		delete prefabCache[i].map;
	}
	prefabCache.Clear();
}

void ( *prefabFileWritten )( const char* relativePath, bool existedBefore, const idStr& before, const idStr& after ) = NULL;

static bool ReadText( const char* relativePath, idStr& text )
{
	void* buffer = NULL;
	const int length = fileSystem->ReadFile( relativePath, &buffer );
	text.Clear();
	if( length < 0 || buffer == NULL )
	{
		return false;
	}
	text.Append( ( const char* )buffer, length );
	fileSystem->FreeFile( buffer );
	return true;
}

static const idMapFile* LoadPrefab( const char* name )
{
	ID_TIME_T time = FILE_NOT_FOUND_TIMESTAMP;
	fileSystem->ReadFile( va( "maps/%s.map", PrefabMapName( name ) ), NULL, &time );
	if( time == FILE_NOT_FOUND_TIMESTAMP )
	{
		return NULL;
	}
	for( int i = 0; i < prefabCache.Num(); i++ )
	{
		if( prefabCache[i].name.Icmp( name ) == 0 )
		{
			if( prefabCache[i].time == time )
			{
				return prefabCache[i].map;
			}
			ForgetPrefab( name );
			break;
		}
	}
	idMapFile* map = ReadMapForEditing( PrefabMapName( name ) );
	if( map == NULL )
	{
		return NULL;
	}
	cachedPrefab_t& entry = prefabCache.Alloc();
	entry.name = name;
	entry.map = map;
	entry.time = time;
	return map;
}

bool PrefabExists( const char* name )
{
	return LoadPrefab( name ) != NULL;
}

void ListPrefabs( idStrList& names )
{
	names.Clear();
	idFileList* files = fileSystem->ListFilesTree( va( "maps/%s", PREFAB_FOLDER ), ".map", true );
	const int skip = idStr::Length( va( "maps/%s/", PREFAB_FOLDER ) );
	for( int i = 0; i < files->GetNumFiles(); i++ )
	{
		idStr name = files->GetFile( i );
		if( name.Length() <= skip )
		{
			continue;
		}
		name = name.Right( name.Length() - skip );
		name.StripFileExtension();
		names.AddUnique( name );
	}
	fileSystem->FreeFileList( files );
}

/*
===============================================================================

	Building a copy from its piece

===============================================================================
*/

// a copy's things, fresh from the piece, in level space and named for the level
struct freshPiece_t
{
	idList<idMapPrimitive*>	brushes;
	idList<idMapEntity*>	entities;
	~freshPiece_t()
	{
		brushes.DeleteContents( true );
		entities.DeleteContents( true );
	}
};

static bool ExpandAnchor( const idMapEntity* anchor, freshPiece_t& out, int depth );

// a piece's own changes to a copy nested in it apply to the fresh nested things as plain values
static void ApplyPieceOverrides( const idMapFile* piece, const char* prefix, const idMat3& axis, const idVec3& origin, freshPiece_t& nested )
{
	for( int i = 0; i < nested.entities.Num(); i++ )
	{
		idMapEntity* f = nested.entities[i];
		const idStr fileOwner = StripPrefix( f->epairs.GetString( KEY_OWNER ), prefix );
		const char* part = f->epairs.GetString( KEY_PART );
		for( int e = 1; e < piece->GetNumEntities(); e++ )
		{
			const idMapEntity* fe = piece->GetEntity( e );
			if( fileOwner != fe->epairs.GetString( KEY_OWNER ) || idStr::Cmp( part, fe->epairs.GetString( KEY_PART ) ) != 0 )
			{
				continue;
			}
			idStrList overrides;
			Tokens( fe->epairs.GetString( KEY_OVERRIDES ), overrides );
			if( overrides.Num() == 0 )
			{
				break;
			}
			idMapEntity* placed = CopyEntity( fe );
			TransformEntityBy( placed, axis, origin, true );
			for( int k = 0; k < overrides.Num(); k++ )
			{
				if( overrides[k] == "brush" )
				{
					while( f->GetNumPrimitives() > 0 )
					{
						f->RemovePrimitive( 0 );
					}
					while( placed->GetNumPrimitives() > 0 )
					{
						f->AddPrimitive( placed->DetachPrimitive( 0 ) );
					}
				}
				else if( placed->epairs.FindKey( overrides[k] ) != NULL )
				{
					f->epairs.Set( overrides[k], placed->epairs.GetString( overrides[k] ) );
				}
				else
				{
					f->epairs.Delete( overrides[k] );
				}
			}
			delete placed;
			break;
		}
	}

	const idMapEntity* world = piece->GetEntity( 0 );
	for( int i = 0; i < nested.brushes.Num(); i++ )
	{
		idMapPrimitive* fb = nested.brushes[i];
		const idStr fileOwner = StripPrefix( fb->epairs.GetString( KEY_OWNER ), prefix );
		const char* part = fb->epairs.GetString( KEY_PART );
		for( int p = 0; p < world->GetNumPrimitives(); p++ )
		{
			const idMapPrimitive* pb = world->GetPrimitive( p );
			if( fileOwner != pb->epairs.GetString( KEY_OWNER ) || idStr::Cmp( part, pb->epairs.GetString( KEY_PART ) ) != 0 )
			{
				continue;
			}
			if( HasToken( pb->epairs.GetString( KEY_OVERRIDES ), "brush" ) )
			{
				idMapPrimitive* copy = CopyPrimitive( pb );
				if( copy != NULL )
				{
					TransformPrimitiveBy( copy, axis, origin, true );
					copy->epairs = fb->epairs;
					delete nested.brushes[i];
					nested.brushes[i] = copy;
				}
			}
			break;
		}
	}
}

static bool ExpandAnchor( const idMapEntity* anchor, freshPiece_t& out, int depth )
{
	if( depth > MAX_NESTING )
	{
		common->Warning( "Editor: pieces nested too deep at %s (a piece inside itself?)", anchor->epairs.GetString( "name" ) );
		return false;
	}
	const idMapFile* piece = LoadPrefab( anchor->epairs.GetString( KEY_PREFAB ) );
	if( piece == NULL )
	{
		return false;
	}

	const idStr name = anchor->epairs.GetString( "name" );
	const idStr prefix = name + "_";
	idVec3 origin;
	idMat3 axis;
	AnchorPlacement( anchor, origin, axis );
	const char* removed = anchor->epairs.GetString( KEY_REMOVED );

	const idMapEntity* world = piece->GetEntity( 0 );
	for( int p = 0; p < world->GetNumPrimitives(); p++ )
	{
		const idMapPrimitive* prim = world->GetPrimitive( p );
		if( prim->epairs.GetString( KEY_OWNER )[0] != '\0' )
		{
			continue;	// part of a piece nested in this one: comes from that piece
		}
		const idStr part = prim->epairs.GetString( KEY_PART, va( "b%d", p ) );
		if( HasToken( removed, part ) )
		{
			continue;
		}
		idMapPrimitive* copy = CopyPrimitive( prim );
		if( copy == NULL )
		{
			continue;
		}
		TransformPrimitiveBy( copy, axis, origin, true );
		copy->epairs.Set( KEY_OWNER, name );
		copy->epairs.Set( KEY_PART, part );
		copy->epairs.Delete( KEY_OVERRIDES );
		out.brushes.Append( copy );
	}

	for( int e = 1; e < piece->GetNumEntities(); e++ )
	{
		const idMapEntity* ent = piece->GetEntity( e );
		if( ent->epairs.GetString( KEY_OWNER )[0] != '\0' )
		{
			continue;
		}
		const idStr part = ent->epairs.GetString( "name", va( "e%d", e ) );
		if( HasToken( removed, part ) )
		{
			continue;
		}
		idMapEntity* copy = CopyEntity( ent );
		TransformEntityBy( copy, axis, origin, true );

		// links between things in the piece (targets, a brush entity's model) follow the new names
		for( int k = 0; k < copy->epairs.GetNumKeyVals(); k++ )
		{
			const idKeyValue* kv = copy->epairs.GetKeyVal( k );
			if( IsLinkKey( kv->GetKey() ) || idStr::Icmp( kv->GetKey(), "classname" ) == 0 || idStr::Icmpn( kv->GetKey(), "prefab", 6 ) == 0 )
			{
				continue;
			}
			if( kv->GetValue().Length() > 0 && piece->FindEntity( kv->GetValue() ) != NULL )
			{
				copy->epairs.Set( kv->GetKey(), prefix + kv->GetValue() );
			}
		}
		copy->epairs.Set( "name", prefix + part );
		copy->epairs.Set( KEY_OWNER, name );
		copy->epairs.Set( KEY_PART, part );
		copy->epairs.Delete( KEY_OVERRIDES );
		copy->epairs.Delete( KEY_PLACED );
		out.entities.Append( copy );

		if( IsPrefabAnchor( copy ) )
		{
			freshPiece_t nested;
			ExpandAnchor( copy, nested, depth + 1 );
			ApplyPieceOverrides( piece, prefix, axis, origin, nested );
			out.brushes.Append( nested.brushes );
			out.entities.Append( nested.entities );
			nested.brushes.Clear();
			nested.entities.Clear();
		}
	}
	return true;
}

enum syncMode_t
{
	SYNC_APPLY,		// make the copy match its piece, keeping overrides
	SYNC_TRACK		// record where the copy differs from its piece as overrides
};

static int MergeEntity( idMapEntity* e, idMapEntity* f, syncMode_t mode )
{
	idStrList overrides;
	Tokens( e->epairs.GetString( KEY_OVERRIDES ), overrides );
	const int before = overrides.Num();
	int changes = 0;

	for( int k = 0; k < f->epairs.GetNumKeyVals(); k++ )
	{
		const idKeyValue* kv = f->epairs.GetKeyVal( k );
		if( IsLinkKey( kv->GetKey() ) || overrides.FindIndex( kv->GetKey() ) >= 0 )
		{
			continue;
		}
		const idKeyValue* cur = e->epairs.FindKey( kv->GetKey() );
		if( cur != NULL && SameValue( cur->GetValue(), kv->GetValue() ) )
		{
			continue;
		}
		if( mode == SYNC_APPLY )
		{
			e->epairs.Set( kv->GetKey(), kv->GetValue() );
		}
		else
		{
			overrides.AddUnique( kv->GetKey() );
		}
		changes++;
	}

	idStrList extra;
	for( int k = 0; k < e->epairs.GetNumKeyVals(); k++ )
	{
		const idKeyValue* kv = e->epairs.GetKeyVal( k );
		if( !IsLinkKey( kv->GetKey() ) && overrides.FindIndex( kv->GetKey() ) < 0 && f->epairs.FindKey( kv->GetKey() ) == NULL )
		{
			extra.Append( kv->GetKey() );
		}
	}
	for( int k = 0; k < extra.Num(); k++ )
	{
		if( mode == SYNC_APPLY )
		{
			e->epairs.Delete( extra[k] );
		}
		else
		{
			overrides.AddUnique( extra[k] );
		}
		changes++;
	}

	if( overrides.FindIndex( "brush" ) < 0 && !SamePrimitives( e, f ) )
	{
		if( mode == SYNC_APPLY )
		{
			while( e->GetNumPrimitives() > 0 )
			{
				e->RemovePrimitive( 0 );
			}
			while( f->GetNumPrimitives() > 0 )
			{
				e->AddPrimitive( f->DetachPrimitive( 0 ) );
			}
		}
		else
		{
			overrides.AddUnique( "brush" );
		}
		changes++;
	}

	if( overrides.Num() != before )
	{
		e->epairs.Set( KEY_OVERRIDES, JoinTokens( overrides ) );
	}
	if( e->epairs.GetString( KEY_OVERRIDES )[0] == '\0' )
	{
		e->epairs.Delete( KEY_OVERRIDES );
	}
	return changes;
}

static int SyncAnchor( idMapFile* map, int anchorIndex, syncMode_t mode )
{
	idMapEntity* anchor = map->GetEntity( anchorIndex );
	freshPiece_t fresh;
	if( !ExpandAnchor( anchor, fresh, 0 ) )
	{
		return 0;
	}
	const idStr name = anchor->epairs.GetString( "name" );
	if( mode == SYNC_APPLY )
	{
		anchor->epairs.Set( KEY_PLACED, PlacedString( anchor ) );
	}

	idStrList owners;
	TreeOwners( map, name, owners );
	for( int i = 0; i < fresh.entities.Num(); i++ )
	{
		if( IsPrefabAnchor( fresh.entities[i] ) )
		{
			owners.AddUnique( fresh.entities[i]->epairs.GetString( "name" ) );
		}
	}

	idMapEntity* world = map->GetEntity( 0 );
	idList<bool> usedEntity, usedBrush;
	usedEntity.SetNum( map->GetNumEntities() );
	usedBrush.SetNum( world->GetNumPrimitives() );
	memset( usedEntity.Ptr(), 0, usedEntity.Num() * sizeof( bool ) );
	memset( usedBrush.Ptr(), 0, usedBrush.Num() * sizeof( bool ) );
	const int oldEntities = map->GetNumEntities();
	const int oldBrushes = world->GetNumPrimitives();

	int changes = 0;
	idStrList removedTo;	// parts deleted from a copy (TRACK): "owner|part"

	for( int i = 0; i < fresh.entities.Num(); i++ )
	{
		idMapEntity* f = fresh.entities[i];
		const char* owner = f->epairs.GetString( KEY_OWNER );
		const char* part = f->epairs.GetString( KEY_PART );
		int match = -1;
		for( int e = 1; e < oldEntities; e++ )
		{
			const idMapEntity* ent = map->GetEntity( e );
			if( !usedEntity[e] && idStr::Cmp( ent->epairs.GetString( KEY_OWNER ), owner ) == 0 && idStr::Cmp( ent->epairs.GetString( KEY_PART ), part ) == 0 )
			{
				match = e;
				break;
			}
		}
		if( match >= 0 )
		{
			usedEntity[match] = true;
			idMapEntity* existing = map->GetEntity( match );
			changes += MergeEntity( existing, f, mode );
			const char* freshName = f->epairs.GetString( "name" );
			if( mode == SYNC_APPLY && idStr::Cmp( existing->epairs.GetString( "name" ), freshName ) != 0 && map->FindEntity( freshName ) == NULL )
			{
				// named after its copy, so links between the copy's things hold
				const idStr oldName = existing->epairs.GetString( "name" );
				existing->epairs.Set( "name", freshName );
				if( IsPrefabAnchor( existing ) )
				{
					for( int e = 1; e < map->GetNumEntities(); e++ )
					{
						if( oldName == map->GetEntity( e )->epairs.GetString( KEY_OWNER ) )
						{
							map->GetEntity( e )->epairs.Set( KEY_OWNER, freshName );
						}
					}
					for( int p = 0; p < world->GetNumPrimitives(); p++ )
					{
						if( oldName == world->GetPrimitive( p )->epairs.GetString( KEY_OWNER ) )
						{
							world->GetPrimitive( p )->epairs.Set( KEY_OWNER, freshName );
						}
					}
				}
				changes++;
			}
			continue;
		}

		const int ownerAnchor = FindAnchor( map, owner );
		const char* removed = ownerAnchor >= 0 ? map->GetEntity( ownerAnchor )->epairs.GetString( KEY_REMOVED ) : "";
		if( HasToken( removed, part ) )
		{
			continue;
		}
		if( mode == SYNC_APPLY )
		{
			map->AddEntity( f );
			fresh.entities[i] = NULL;
			changes++;
		}
		else if( ownerAnchor >= 0 )
		{
			removedTo.Append( va( "%s|%s", owner, part ) );
		}
	}

	for( int i = 0; i < fresh.brushes.Num(); i++ )
	{
		idMapPrimitive* f = fresh.brushes[i];
		const char* owner = f->epairs.GetString( KEY_OWNER );
		const char* part = f->epairs.GetString( KEY_PART );
		int match = -1;
		for( int p = 0; p < oldBrushes; p++ )
		{
			const idMapPrimitive* prim = world->GetPrimitive( p );
			if( !usedBrush[p] && idStr::Cmp( prim->epairs.GetString( KEY_OWNER ), owner ) == 0 && idStr::Cmp( prim->epairs.GetString( KEY_PART ), part ) == 0 )
			{
				match = p;
				break;
			}
		}
		if( match >= 0 )
		{
			usedBrush[match] = true;
			idMapPrimitive* prim = world->GetPrimitive( match );
			if( HasToken( prim->epairs.GetString( KEY_OVERRIDES ), "brush" ) || SamePrimitive( prim, f ) )
			{
				continue;
			}
			if( mode == SYNC_APPLY )
			{
				world->ReplacePrimitive( match, f );
				fresh.brushes[i] = NULL;
			}
			else
			{
				AddToken( prim->epairs, KEY_OVERRIDES, "brush" );
			}
			changes++;
			continue;
		}

		const int ownerAnchor = FindAnchor( map, owner );
		const char* removed = ownerAnchor >= 0 ? map->GetEntity( ownerAnchor )->epairs.GetString( KEY_REMOVED ) : "";
		if( HasToken( removed, part ) )
		{
			continue;
		}
		if( mode == SYNC_APPLY )
		{
			world->AddPrimitive( f );
			fresh.brushes[i] = NULL;
			changes++;
		}
		else if( ownerAnchor >= 0 )
		{
			removedTo.Append( va( "%s|%s", owner, part ) );
		}
	}

	if( mode == SYNC_TRACK )
	{
		// parts deleted from this copy only: the copy remembers, so rebuilding doesn't bring them back
		for( int i = 0; i < removedTo.Num(); i++ )
		{
			const int bar = removedTo[i].Find( '|' );
			const idStr owner = removedTo[i].Left( bar );
			const idStr part = removedTo[i].Right( removedTo[i].Length() - bar - 1 );
			const int a = FindAnchor( map, owner );
			if( a < 0 )
			{
				continue;
			}
			idMapEntity* ownerEnt = map->GetEntity( a );
			AddToken( ownerEnt->epairs, KEY_REMOVED, part );
			if( a != anchorIndex )
			{
				AddToken( ownerEnt->epairs, KEY_OVERRIDES, KEY_REMOVED );	// a nested copy's list differs from its piece's
			}
			changes++;
		}
		return changes;
	}

	// things of this copy the piece no longer has
	for( int p = oldBrushes - 1; p >= 0; p-- )
	{
		if( !usedBrush[p] && owners.FindIndex( world->GetPrimitive( p )->epairs.GetString( KEY_OWNER ) ) >= 0 )
		{
			world->RemovePrimitive( p );
			changes++;
		}
	}
	for( int e = oldEntities - 1; e >= 1; e-- )
	{
		if( !usedEntity[e] && e != anchorIndex && owners.FindIndex( map->GetEntity( e )->epairs.GetString( KEY_OWNER ) ) >= 0 )
		{
			map->RemoveEntity( map->GetEntity( e ) );
			changes++;
		}
	}
	return changes;
}

int SyncPiece( idMapFile* map, int anchor )
{
	if( map == NULL || anchor <= 0 || anchor >= map->GetNumEntities() || !IsPrefabAnchor( map->GetEntity( anchor ) ) )
	{
		return 0;
	}
	return SyncAnchor( map, anchor, SYNC_APPLY );
}

// the outermost copies, by name (indices move while copies are rebuilt)
static void TopAnchors( const idMapFile* map, idStrList& names )
{
	names.Clear();
	for( int e = 1; e < map->GetNumEntities(); e++ )
	{
		const idMapEntity* ent = map->GetEntity( e );
		if( IsPrefabAnchor( ent ) && FindAnchor( map, ent->epairs.GetString( KEY_OWNER ) ) < 0 )
		{
			names.Append( ent->epairs.GetString( "name" ) );
		}
	}
}

// does this copy use the piece, itself or anywhere inside?
static bool UsesPrefab( const idMapFile* map, const char* anchorName, const char* prefab )
{
	idStrList owners;
	TreeOwners( map, anchorName, owners );
	for( int i = 0; i < owners.Num(); i++ )
	{
		const int a = FindAnchor( map, owners[i] );
		if( a >= 0 && idStr::Icmp( map->GetEntity( a )->epairs.GetString( KEY_PREFAB ), prefab ) == 0 )
		{
			return true;
		}
	}
	return false;
}

int SyncPrefabs( idMapFile* map, const char* onlyPrefab )
{
	if( map == NULL )
	{
		return 0;
	}
	idStrList names;
	TopAnchors( map, names );
	int changes = 0;
	for( int i = 0; i < names.Num(); i++ )
	{
		if( onlyPrefab != NULL && !UsesPrefab( map, names[i], onlyPrefab ) )
		{
			continue;
		}
		const int a = FindAnchor( map, names[i] );
		if( a >= 0 )
		{
			changes += SyncAnchor( map, a, SYNC_APPLY );
		}
	}
	return changes;
}

/*
===============================================================================

	Keeping links up to date after edits

===============================================================================
*/

// Duplicating a copy gives its things new names but leaves them pointing at the original's
// anchor; point them at the new anchor instead (rebuilding then names them after it).
static void FixDuplicatedCopies( idMapFile* map, const idDict& renames, const idList<Selection>& duplicated )
{
	idDict owners;		// old anchor name -> new anchor name
	for( int i = 0; i < renames.GetNumKeyVals(); i++ )
	{
		const idKeyValue* kv = renames.GetKeyVal( i );
		const int a = FindAnchor( map, kv->GetValue() );
		if( a < 0 )
		{
			continue;
		}
		// a copied copy is named after its piece, like any other copy
		idMapEntity* anchor = map->GetEntity( a );
		idStr base = anchor->epairs.GetString( KEY_PREFAB );
		base.StripPath();
		const idStr name = map->GetUniqueEntityName( base );
		anchor->epairs.Set( "name", name );
		owners.Set( kv->GetKey(), name );
	}
	if( owners.GetNumKeyVals() == 0 )
	{
		return;
	}
	for( int i = 0; i < duplicated.Num(); i++ )
	{
		idDict* dict = ItemDict( map, duplicated[i] );
		if( dict == NULL )
		{
			continue;
		}
		const char* newOwner = owners.GetString( dict->GetString( KEY_OWNER ), "" );
		if( newOwner[0] != '\0' )
		{
			dict->Set( KEY_OWNER, newOwner );
		}
	}
}

void TrackPrefabEdits( idMapFile* map, const char* skipAnchor, const idDict& duplicateRenames, const idList<Selection>& duplicated )
{
	if( map == NULL )
	{
		return;
	}
	if( duplicateRenames.GetNumKeyVals() > 0 )
	{
		FixDuplicatedCopies( map, duplicateRenames, duplicated );
	}

	idStrList names;
	TopAnchors( map, names );
	for( int i = 0; i < names.Num(); i++ )
	{
		if( skipAnchor != NULL && names[i] == skipAnchor )
		{
			continue;
		}
		const int a = FindAnchor( map, names[i] );
		if( a < 0 )
		{
			continue;
		}
		idMapEntity* anchor = map->GetEntity( a );
		if( PlacedString( anchor ) != anchor->epairs.GetString( KEY_PLACED ) )
		{
			SyncAnchor( map, a, SYNC_APPLY );		// moved or turned as a whole: rebuild where it is now
		}
		else
		{
			SyncAnchor( map, a, SYNC_TRACK );
		}
	}
}

/*
===============================================================================

	Making pieces and copies

===============================================================================
*/

// a thing as text, to tell whether it changed
static idStr ItemStateText( const idMapFile* map, const Selection& sel )
{
	idFile_Memory file( "editor_state" );
	if( sel.primitive >= 0 )
	{
		const idMapPrimitive* prim = map->GetEntity( sel.entity )->GetPrimitive( sel.primitive );
		if( prim->GetType() == idMapPrimitive::TYPE_BRUSH )
		{
			static_cast<const idMapBrush*>( prim )->Write( &file, 0, vec3_origin );
		}
		else if( prim->GetType() == idMapPrimitive::TYPE_PATCH )
		{
			static_cast<const idMapPatch*>( prim )->Write( &file, 0, vec3_origin );
		}
	}
	else
	{
		map->GetEntity( sel.entity )->Write( &file, 0, false );
	}
	idStr text;
	text.Append( file.GetDataPtr(), file.Length() );
	return text;
}

static idStr ItemKey( const idMapFile* map, const Selection& sel )
{
	const idDict* dict = ItemDict( map, sel );
	return dict != NULL ? va( "%s|%s", dict->GetString( KEY_OWNER ), dict->GetString( KEY_PART ) ) : "";
}

void SnapshotCopyChanges( const idMapFile* map, int anchor, pieceEditStart_t& start )
{
	start.keys.Clear();
	start.states.Clear();
	start.removed.Clear();
	if( map == NULL || anchor <= 0 )
	{
		return;
	}
	idList<Selection> items;
	PieceItems( map, anchor, items );
	for( int i = 1; i < items.Num(); i++ )
	{
		const idDict* dict = ItemDict( map, items[i] );
		if( dict != NULL && dict->GetString( KEY_OVERRIDES )[0] != '\0' )
		{
			start.keys.Append( ItemKey( map, items[i] ) );
			start.states.Append( ItemStateText( map, items[i] ) );
		}
	}
	Tokens( map->GetEntity( anchor )->epairs.GetString( KEY_REMOVED ), start.removed );
}

// the piece's own version of a thing (by its owner in the piece, "" for the piece's own parts)
static idMapEntity* PieceEntity( const idMapFile* piece, const char* fileOwner, const char* part )
{
	for( int e = 1; e < piece->GetNumEntities(); e++ )
	{
		idMapEntity* ent = piece->GetEntity( e );
		if( idStr::Cmp( ent->epairs.GetString( KEY_OWNER ), fileOwner ) != 0 )
		{
			continue;
		}
		const char* entPart = fileOwner[0] == '\0' ? ent->epairs.GetString( "name" ) : ent->epairs.GetString( KEY_PART );
		if( idStr::Cmp( entPart, part ) == 0 )
		{
			return ent;
		}
	}
	return NULL;
}

static idMapPrimitive* PieceBrush( const idMapFile* piece, const char* fileOwner, const char* part )
{
	const idMapEntity* world = piece->GetEntity( 0 );
	for( int p = 0; p < world->GetNumPrimitives(); p++ )
	{
		idMapPrimitive* prim = world->GetPrimitive( p );
		if( idStr::Cmp( prim->epairs.GetString( KEY_OWNER ), fileOwner ) == 0 && idStr::Cmp( prim->epairs.GetString( KEY_PART, va( "b%d", p ) ), part ) == 0 )
		{
			return prim;
		}
	}
	return NULL;
}

int PlacePrefab( idMapFile* map, const char* name, const idVec3& origin, const idMat3& axis )
{
	if( map == NULL || LoadPrefab( name ) == NULL )
	{
		return -1;
	}
	idStr base = name;
	base.StripPath();
	idMapEntity* anchor = new( TAG_SYSTEM ) idMapEntity();
	anchor->epairs.Set( "classname", PREFAB_CLASS );
	anchor->epairs.Set( "name", map->GetUniqueEntityName( base ) );
	anchor->epairs.Set( KEY_PREFAB, name );
	anchor->epairs.Set( "origin", origin.ToString( 2 ) );
	anchor->epairs.Set( "noFlood", "1" );
	SetEntityAxis( anchor, axis );
	const int index = map->AddEntity( anchor );
	SyncAnchor( map, index, SYNC_APPLY );
	return index;
}

// The things of one copy, written as a piece around the copy's anchor. Names lose the copy's
// prefix; overrides of the copy's own parts go (their values become the piece's), those of
// pieces nested in it stay (they are the piece's changes to its nested pieces).
static idMapFile* PieceFromCopy( const idMapFile* map, int anchorIndex, const idStrList* keepParts = NULL, const idStrList* keepRemoved = NULL, const idMapFile* oldPiece = NULL )
{
	const idMapEntity* anchor = map->GetEntity( anchorIndex );
	const idStr name = anchor->epairs.GetString( "name" );
	const idStr prefix = name + "_";
	idMat3 inverse;
	idVec3 back;
	AnchorInverse( anchor, inverse, back );

	idList<Selection> items;
	PieceItems( map, anchorIndex, items );

	idStrList levelNames;
	for( int i = 1; i < items.Num(); i++ )
	{
		if( items[i].primitive < 0 )
		{
			levelNames.Append( map->GetEntity( items[i].entity )->epairs.GetString( "name" ) );
		}
	}

	idMapFile* piece = new( TAG_SYSTEM ) idMapFile();
	idMapEntity* world = new( TAG_SYSTEM ) idMapEntity();
	world->epairs.Set( "classname", "worldspawn" );
	world->epairs.Set( "prefab_version", "1" );
	piece->AddEntity( world );

	idStrList brushParts;
	for( int i = 1; i < items.Num(); i++ )
	{
		const Selection& sel = items[i];

		// changes this copy had before the edit, left alone by it: the piece keeps its own version
		if( keepParts != NULL && oldPiece != NULL && keepParts->FindIndex( ItemKey( map, sel ) ) >= 0 )
		{
			const idDict* dict = ItemDict( map, sel );
			const idStr owner = dict->GetString( KEY_OWNER );
			const idStr fileOwner = owner == name ? idStr( "" ) : StripPrefix( owner, prefix );
			if( sel.primitive >= 0 )
			{
				const idMapPrimitive* old = PieceBrush( oldPiece, fileOwner, dict->GetString( KEY_PART ) );
				if( old != NULL )
				{
					idMapPrimitive* copy = CopyPrimitive( old );
					if( copy != NULL )
					{
						world->AddPrimitive( copy );
						brushParts.Append( copy->epairs.GetString( KEY_PART ) );
						continue;
					}
				}
			}
			else
			{
				const idMapEntity* old = PieceEntity( oldPiece, fileOwner, dict->GetString( KEY_PART ) );
				if( old != NULL )
				{
					piece->AddEntity( CopyEntity( old ) );
					continue;
				}
			}
		}

		if( sel.primitive >= 0 )
		{
			idMapPrimitive* copy = CopyPrimitive( map->GetEntity( 0 )->GetPrimitive( sel.primitive ) );
			if( copy == NULL )
			{
				continue;
			}
			TransformPrimitiveBy( copy, inverse, back, true );
			const idStr owner = copy->epairs.GetString( KEY_OWNER );
			if( owner == name )
			{
				copy->epairs.Delete( KEY_OWNER );
				copy->epairs.Delete( KEY_OVERRIDES );
				idStr part = copy->epairs.GetString( KEY_PART );
				if( part.Length() == 0 || brushParts.FindIndex( part ) >= 0 )
				{
					int n = brushParts.Num();
					do
					{
						part = va( "b%d", n++ );
					}
					while( brushParts.FindIndex( part ) >= 0 );
				}
				brushParts.Append( part );
				copy->epairs.Set( KEY_PART, part );
			}
			else
			{
				copy->epairs.Set( KEY_OWNER, StripPrefix( owner, prefix ) );
			}
			world->AddPrimitive( copy );
			continue;
		}

		idMapEntity* copy = CopyEntity( map->GetEntity( sel.entity ) );
		TransformEntityBy( copy, inverse, back, true );
		for( int k = 0; k < copy->epairs.GetNumKeyVals(); k++ )
		{
			const idKeyValue* kv = copy->epairs.GetKeyVal( k );
			if( idStr::Icmp( kv->GetKey(), "classname" ) != 0 && levelNames.FindIndex( kv->GetValue() ) >= 0 )
			{
				copy->epairs.Set( kv->GetKey(), StripPrefix( kv->GetValue(), prefix ) );
			}
		}
		const idStr owner = copy->epairs.GetString( KEY_OWNER );
		if( owner == name )
		{
			copy->epairs.Delete( KEY_OWNER );
			copy->epairs.Delete( KEY_PART );
			copy->epairs.Delete( KEY_OVERRIDES );
		}
		else
		{
			copy->epairs.Set( KEY_OWNER, StripPrefix( owner, prefix ) );
		}
		copy->epairs.Delete( KEY_PLACED );
		piece->AddEntity( copy );
	}

	// parts this copy had deleted before the edit stay in the piece (and deleted from this copy)
	if( keepRemoved != NULL && oldPiece != NULL )
	{
		for( int i = 0; i < keepRemoved->Num(); i++ )
		{
			const idMapPrimitive* oldBrush = PieceBrush( oldPiece, "", ( *keepRemoved )[i] );
			if( oldBrush != NULL )
			{
				idMapPrimitive* copy = CopyPrimitive( oldBrush );
				if( copy != NULL )
				{
					world->AddPrimitive( copy );
				}
				continue;
			}
			const idMapEntity* oldEnt = PieceEntity( oldPiece, "", ( *keepRemoved )[i] );
			if( oldEnt != NULL && piece->FindEntity( oldEnt->epairs.GetString( "name" ) ) == NULL )
			{
				piece->AddEntity( CopyEntity( oldEnt ) );
			}
		}
	}
	return piece;
}

static bool WritePiece( idMapFile* piece, const char* name, idStr& error )
{
	const idStr path = va( "maps/%s.map", PrefabMapName( name ) );
	idStr before;
	const bool existed = ReadText( path, before );
	if( existed )
	{
		// the previous version stays on disk too, for after the editor has closed
		fileSystem->WriteFile( va( "maps/%s.bak", PrefabMapName( name ) ), before.c_str(), before.Length(), "fs_basepath" );
	}
	if( !WriteMapForEditing( piece, PrefabMapName( name ) ) )
	{
		error = va( "Couldn't write %s", path.c_str() );
		return false;
	}
	ForgetPrefab( name );
	idStr after;
	ReadText( path, after );
	if( prefabFileWritten != NULL )
	{
		prefabFileWritten( path, existed, before, after );
	}
	return true;
}

int MakePrefab( Document& doc, const char* name, const idVec3& origin, idStr& error )
{
	idMapFile* map = doc.Map();
	if( map == NULL )
	{
		error = "No level is open";
		return -1;
	}

	// what goes in: whole entities, level brushes; worldspawn as a whole never
	idList<int> entities, brushes;
	for( int i = 0; i < doc.selection.Num(); i++ )
	{
		const Selection& sel = doc.selection[i];
		if( sel.IsEmpty() || sel.IsFace() )
		{
			continue;
		}
		if( sel.entity == 0 )
		{
			if( sel.primitive >= 0 )
			{
				brushes.AddUnique( sel.primitive );
			}
			continue;
		}
		entities.AddUnique( sel.entity );
	}
	if( entities.Num() == 0 && brushes.Num() == 0 )
	{
		error = "Select what goes in the piece first";
		return -1;
	}

	// a temporary anchor at the piece's origin turns the selection into a "copy" to write
	idMapEntity* temp = new( TAG_SYSTEM ) idMapEntity();
	const idStr tempName = map->GetUniqueEntityName( "_newpiece" );
	temp->epairs.Set( "classname", PREFAB_CLASS );
	temp->epairs.Set( "name", tempName );
	temp->epairs.Set( "origin", origin.ToString( 2 ) );
	const int tempIndex = map->AddEntity( temp );

	idDict savedOwners;
	for( int i = 0; i < entities.Num(); i++ )
	{
		idMapEntity* ent = map->GetEntity( entities[i] );
		if( ent->epairs.GetString( KEY_OWNER )[0] == '\0' )
		{
			if( ent->epairs.GetString( "name" )[0] == '\0' )
			{
				ent->epairs.Set( "name", map->GetUniqueEntityName( ent->epairs.GetString( "classname" ) ) );
			}
			ent->epairs.Set( KEY_OWNER, tempName );
			ent->epairs.Set( KEY_PART, ent->epairs.GetString( "name" ) );
		}
	}
	for( int i = 0; i < brushes.Num(); i++ )
	{
		idMapPrimitive* prim = map->GetEntity( 0 )->GetPrimitive( brushes[i] );
		if( prim->epairs.GetString( KEY_OWNER )[0] == '\0' )
		{
			prim->epairs.Set( KEY_OWNER, tempName );
			prim->epairs.Set( KEY_PART, va( "b%d", i ) );
		}
	}

	idMapFile* piece = PieceFromCopy( map, tempIndex );
	const bool written = WritePiece( piece, name, error );
	delete piece;

	// out with the originals, in with a linked copy
	idList<Selection> items;
	PieceItems( map, tempIndex, items );
	idMapEntity* world = map->GetEntity( 0 );
	idList<int> deadEntities, deadBrushes;
	for( int i = 0; i < items.Num(); i++ )
	{
		if( items[i].primitive >= 0 )
		{
			deadBrushes.Append( items[i].primitive );
		}
		else
		{
			deadEntities.Append( items[i].entity );
		}
	}
	if( !written )
	{
		// put things back as they were
		for( int i = 0; i < items.Num(); i++ )
		{
			idDict* dict = ItemDict( map, items[i] );
			if( dict != NULL && idStr::Cmp( dict->GetString( KEY_OWNER ), tempName ) == 0 )
			{
				dict->Delete( KEY_OWNER );
				dict->Delete( KEY_PART );
			}
		}
		map->RemoveEntity( map->GetEntity( tempIndex ) );
		return -1;
	}
	deadBrushes.SortWithTemplate();
	deadEntities.SortWithTemplate();
	for( int i = deadBrushes.Num() - 1; i >= 0; i-- )
	{
		world->RemovePrimitive( deadBrushes[i] );
	}
	for( int i = deadEntities.Num() - 1; i >= 0; i-- )
	{
		map->RemoveEntity( map->GetEntity( deadEntities[i] ) );
	}
	return PlacePrefab( map, name, origin, mat3_identity );
}

bool WritePrefabFromCopy( idMapFile* map, int anchor, idStr& error, const pieceEditStart_t* start )
{
	if( map == NULL || anchor <= 0 || anchor >= map->GetNumEntities() || !IsPrefabAnchor( map->GetEntity( anchor ) ) )
	{
		error = "That isn't a piece";
		return false;
	}
	const idStr prefab = map->GetEntity( anchor )->epairs.GetString( KEY_PREFAB );

	// which of the copy's earlier changes the edit left alone
	idStrList keepParts, keepRemoved;
	if( start != NULL )
	{
		idList<Selection> items;
		PieceItems( map, anchor, items );
		for( int i = 1; i < items.Num(); i++ )
		{
			const int k = start->keys.FindIndex( ItemKey( map, items[i] ) );
			if( k >= 0 && start->states[k] == ItemStateText( map, items[i] ) )
			{
				keepParts.Append( start->keys[k] );
			}
		}
		idStrList removedNow;
		Tokens( map->GetEntity( anchor )->epairs.GetString( KEY_REMOVED ), removedNow );
		for( int i = 0; i < start->removed.Num(); i++ )
		{
			if( removedNow.FindIndex( start->removed[i] ) >= 0 )
			{
				keepRemoved.Append( start->removed[i] );
			}
		}
	}
	idMapFile* oldPiece = ( start != NULL ) ? ReadMapForEditing( PrefabMapName( prefab ) ) : NULL;

	idMapFile* piece = PieceFromCopy( map, anchor, &keepParts, &keepRemoved, oldPiece );
	const bool written = WritePiece( piece, prefab, error );
	delete piece;
	delete oldPiece;
	if( !written )
	{
		return false;
	}

	// this copy is now the piece: its own changes and deletions are the piece's, bar those kept
	idMapEntity* anchorEnt = map->GetEntity( anchor );
	if( keepRemoved.Num() > 0 )
	{
		anchorEnt->epairs.Set( KEY_REMOVED, JoinTokens( keepRemoved ) );
	}
	else
	{
		anchorEnt->epairs.Delete( KEY_REMOVED );
	}
	idList<Selection> items;
	PieceItems( map, anchor, items );
	const idStr name = anchorEnt->epairs.GetString( "name" );
	for( int i = 1; i < items.Num(); i++ )
	{
		idDict* dict = ItemDict( map, items[i] );
		if( dict != NULL && idStr::Cmp( dict->GetString( KEY_OWNER ), name ) == 0 && keepParts.FindIndex( ItemKey( map, items[i] ) ) < 0 )
		{
			dict->Delete( KEY_OVERRIDES );
		}
	}
	SyncPrefabs( map, prefab );
	return true;
}

/*
===============================================================================

	Overrides

===============================================================================
*/

void GetOverrides( const idMapFile* map, const Selection& sel, idStrList& keys )
{
	keys.Clear();
	const idDict* dict = ItemDict( map, sel );
	if( dict != NULL )
	{
		Tokens( dict->GetString( KEY_OVERRIDES ), keys );
	}
}

void RevertOverrides( idMapFile* map, const Selection& sel )
{
	idDict* dict = ItemDict( map, sel );
	if( dict == NULL )
	{
		return;
	}
	dict->Delete( KEY_OVERRIDES );
	if( sel.primitive < 0 && IsPrefabAnchor( map->GetEntity( sel.entity ) ) )
	{
		dict->Delete( KEY_REMOVED );	// a copy's deleted parts come back
	}
	const int top = TopAnchorOf( map, sel );
	if( top >= 0 )
	{
		SyncAnchor( map, top, SYNC_APPLY );
	}
}

bool ApplyOverridesToPiece( idMapFile* map, const Selection& sel, idStr& error )
{
	idDict* dict = ItemDict( map, sel );
	idList<int> chain;
	AnchorChain( map, sel, chain );
	if( dict == NULL || chain.Num() == 0 )
	{
		error = "That isn't part of a piece";
		return false;
	}
	const idMapEntity* owner = map->GetEntity( chain[0] );
	const idStr prefab = owner->epairs.GetString( KEY_PREFAB );
	const idStr part = dict->GetString( KEY_PART );
	idStrList overrides;
	Tokens( dict->GetString( KEY_OVERRIDES ), overrides );
	if( overrides.Num() == 0 )
	{
		return true;
	}

	idMapFile* piece = ReadMapForEditing( PrefabMapName( prefab ) );
	if( piece == NULL )
	{
		error = va( "The piece \"%s\" is missing", prefab.c_str() );
		return false;
	}
	idMat3 inverse;
	idVec3 back;
	AnchorInverse( owner, inverse, back );

	bool found = false;
	if( sel.primitive >= 0 )
	{
		idMapEntity* world = piece->GetEntity( 0 );
		for( int p = 0; p < world->GetNumPrimitives(); p++ )
		{
			idMapPrimitive* prim = world->GetPrimitive( p );
			if( prim->epairs.GetString( KEY_OWNER )[0] == '\0' && part == prim->epairs.GetString( KEY_PART, va( "b%d", p ) ) )
			{
				idMapPrimitive* copy = CopyPrimitive( map->GetEntity( 0 )->GetPrimitive( sel.primitive ) );
				if( copy != NULL )
				{
					TransformPrimitiveBy( copy, inverse, back, true );
					copy->epairs.Clear();
					copy->epairs.Set( KEY_PART, part );
					world->ReplacePrimitive( p, copy );
					found = true;
				}
				break;
			}
		}
	}
	else
	{
		idMapEntity* target = piece->FindEntity( part );
		if( target != NULL )
		{
			idMapEntity* placed = CopyEntity( map->GetEntity( sel.entity ) );
			TransformEntityBy( placed, inverse, back, true );
			for( int k = 0; k < overrides.Num(); k++ )
			{
				if( overrides[k] == "brush" )
				{
					while( target->GetNumPrimitives() > 0 )
					{
						target->RemovePrimitive( 0 );
					}
					while( placed->GetNumPrimitives() > 0 )
					{
						target->AddPrimitive( placed->DetachPrimitive( 0 ) );
					}
				}
				else if( placed->epairs.FindKey( overrides[k] ) != NULL )
				{
					target->epairs.Set( overrides[k], placed->epairs.GetString( overrides[k] ) );
				}
				else
				{
					target->epairs.Delete( overrides[k] );
				}
			}
			delete placed;
			found = true;
		}
	}
	if( !found )
	{
		delete piece;
		error = "Couldn't find that part in its piece";
		return false;
	}
	const bool written = WritePiece( piece, prefab, error );
	delete piece;
	if( !written )
	{
		return false;
	}
	dict->Delete( KEY_OVERRIDES );
	SyncPrefabs( map, prefab );
	return true;
}

void UnlinkPiece( idMapFile* map, int anchor )
{
	idList<Selection> items;
	PieceItems( map, anchor, items );
	idList<int> anchors;
	for( int i = 0; i < items.Num(); i++ )
	{
		idDict* dict = ItemDict( map, items[i] );
		if( dict == NULL )
		{
			continue;
		}
		if( items[i].primitive < 0 && IsPrefabAnchor( map->GetEntity( items[i].entity ) ) )
		{
			anchors.Append( items[i].entity );
			continue;
		}
		dict->Delete( KEY_OWNER );
		dict->Delete( KEY_PART );
		dict->Delete( KEY_OVERRIDES );
	}
	anchors.SortWithTemplate();
	for( int i = anchors.Num() - 1; i >= 0; i-- )
	{
		map->RemoveEntity( map->GetEntity( anchors[i] ) );
	}
}

} // namespace GrizzEditor

#endif // GRIZZ_EDITOR
