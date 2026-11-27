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

/*
===============================================================================

	Textures on brush faces

	A brushDef3 face maps a point p to texture coordinates with

		s = texMat[0].x * ( p . S ) + texMat[0].y * ( p . T ) + texMat[0].z
		t = texMat[1].x * ( p . S ) + texMat[1].y * ( p . T ) + texMat[1].z

	where S and T are two axes in the face's plane picked from its normal
	(TextureAxisBase). One texture repeat is 1.0 in s and t.

===============================================================================
*/

static const float DEFAULT_TILE = 128.0f;	// map units per texture repeat for new brushes

// the same axis base idMapBrushSide::GetTextureVectors and dmap use
void TextureAxisBase( const idVec3& normal, idVec3& texS, idVec3& texT )
{
	idVec3 n;
	n[0] = ( idMath::Fabs( normal[0] ) < 1e-6f ) ? 0.0f : normal[0];
	n[1] = ( idMath::Fabs( normal[1] ) < 1e-6f ) ? 0.0f : normal[1];
	n[2] = ( idMath::Fabs( normal[2] ) < 1e-6f ) ? 0.0f : normal[2];

	const float rotY = -atan2( n[2], idMath::Sqrt( n[1] * n[1] + n[0] * n[0] ) );
	const float rotZ = atan2( n[1], n[0] );

	texS[0] = -sin( rotZ );
	texS[1] = cos( rotZ );
	texS[2] = 0;

	texT[0] = -sin( rotY ) * cos( rotZ );
	texT[1] = -sin( rotY ) * sin( rotZ );
	texT[2] = -cos( rotY );
}

void SetSideTextureDefault( idMapBrushSide* side )
{
	TextureAlign align;
	align.offset.Zero();
	align.tile.Set( DEFAULT_TILE, DEFAULT_TILE );
	align.rotation = 0.0f;
	SetTextureAlign( side, align );
}

TextureAlign GetTextureAlign( const idMapBrushSide* side )
{
	idVec3 m0, m1;
	const_cast<idMapBrushSide*>( side )->GetTextureMatrix( m0, m1 );

	TextureAlign align;
	const float len0 = idVec2( m0.x, m0.y ).Length();
	const float len1 = idVec2( m1.x, m1.y ).Length();
	if( len0 < 1e-8f || len1 < 1e-8f )
	{
		align.offset.Zero();
		align.tile.Set( DEFAULT_TILE, DEFAULT_TILE );
		align.rotation = 0.0f;
		return align;
	}

	const float angle = atan2( m0.y, m0.x );
	const float vSign = ( m1.x * -sin( angle ) + m1.y * cos( angle ) ) >= 0.0f ? 1.0f : -1.0f;

	align.tile.x = 1.0f / len0;
	align.tile.y = vSign / len1;
	align.rotation = RAD2DEG( angle );
	align.offset.x = m0.z * align.tile.x;
	align.offset.y = m1.z * align.tile.y;
	return align;
}

void SetTextureAlign( idMapBrushSide* side, const TextureAlign& align )
{
	const float tileU = ( idMath::Fabs( align.tile.x ) < 0.001f ) ? DEFAULT_TILE : align.tile.x;
	const float tileV = ( idMath::Fabs( align.tile.y ) < 0.001f ) ? DEFAULT_TILE : align.tile.y;
	const float angle = DEG2RAD( align.rotation );
	const float c = cos( angle );
	const float s = sin( angle );

	idVec3 mat[2];
	mat[0].Set( c / tileU, s / tileU, align.offset.x / tileU );
	mat[1].Set( -s / tileV, c / tileV, align.offset.y / tileV );
	side->SetTextureMatrix( mat );
}

void FitTexture( idMapBrush* brush, int sideNum, float repeatsU, float repeatsV )
{
	idWinding w;
	if( !BrushSideWinding( brush, sideNum, w ) )
	{
		return;
	}

	idMapBrushSide* side = brush->GetSide( sideNum );
	TextureAlign align = GetTextureAlign( side );

	idVec3 texS, texT;
	TextureAxisBase( side->GetPlane().Normal(), texS, texT );
	const float angle = DEG2RAD( align.rotation );
	const float c = cos( angle );
	const float s = sin( angle );

	idVec2 mins( idMath::INFINITUM, idMath::INFINITUM );
	idVec2 maxs( -idMath::INFINITUM, -idMath::INFINITUM );
	for( int i = 0; i < w.GetNumPoints(); i++ )
	{
		const idVec3 p = w[i].ToVec3();
		const float u = p * texS;
		const float v = p * texT;
		const float su = u * c + v * s;
		const float sv = -u * s + v * c;
		mins.x = Min( mins.x, su );
		mins.y = Min( mins.y, sv );
		maxs.x = Max( maxs.x, su );
		maxs.y = Max( maxs.y, sv );
	}

	repeatsU = Max( repeatsU, 0.01f );
	repeatsV = Max( repeatsV, 0.01f );
	const float signU = align.tile.x < 0.0f ? -1.0f : 1.0f;
	const float signV = align.tile.y < 0.0f ? -1.0f : 1.0f;
	align.tile.x = signU * Max( maxs.x - mins.x, 1.0f ) / repeatsU;
	align.tile.y = signV * Max( maxs.y - mins.y, 1.0f ) / repeatsV;
	align.offset.x = -( signU > 0.0f ? mins.x : maxs.x );
	align.offset.y = -( signV > 0.0f ? mins.y : maxs.y );
	SetTextureAlign( side, align );
}

idMapBrush* BrushFromPlanes( const idList<idPlane>& planes, const char* material )
{
	idMapBrush* brush = new( TAG_SYSTEM ) idMapBrush();
	for( int i = 0; i < planes.Num(); i++ )
	{
		idMapBrushSide* side = new( TAG_SYSTEM ) idMapBrushSide();
		side->SetPlane( planes[i] );
		side->SetMaterial( material );
		SetSideTextureDefault( side );
		brush->AddSide( side );
	}
	return brush;
}

static idMapBrush* MakeBoxBrush( const idVec3& mins, const idVec3& maxs, const char* material )
{
	idList<idPlane> planes;
	planes.Append( idPlane( idVec3( 0, 0, 1 ), maxs.z ) );
	planes.Append( idPlane( idVec3( 0, 0, -1 ), -mins.z ) );
	planes.Append( idPlane( idVec3( 1, 0, 0 ), maxs.x ) );
	planes.Append( idPlane( idVec3( -1, 0, 0 ), -mins.x ) );
	planes.Append( idPlane( idVec3( 0, 1, 0 ), maxs.y ) );
	planes.Append( idPlane( idVec3( 0, -1, 0 ), -mins.y ) );
	return BrushFromPlanes( planes, material );
}

/*
===============================================================================

	Transforms

	linear is an ordinary column-vector matrix: row r of the idMat3 is row r
	of the matrix, so a point goes to ( row0 . p, row1 . p, row2 . p ) + t.

===============================================================================
*/

static idVec3 MulPoint( const idMat3& m, const idVec3& p )
{
	return idVec3( m[0] * p, m[1] * p, m[2] * p );
}

static void TransformSide( idMapBrushSide* side, const idMat3& linear, const idMat3& normalMatrix, const idVec3& translation, bool lockTexture )
{
	const idPlane oldPlane = side->GetPlane();
	const idVec3 oldNormal = oldPlane.Normal();

	idVec3 oldS, oldT;
	TextureAxisBase( oldNormal, oldS, oldT );

	// three points on the old face, 64 units apart along its texture axes
	idVec3 points[3];
	points[0] = oldNormal * ( -oldPlane[3] );
	points[1] = points[0] + oldS * 64.0f;
	points[2] = points[0] + oldT * 64.0f;

	idVec3 m0, m1;
	side->GetTextureMatrix( m0, m1 );
	float st[3][2];
	for( int i = 0; i < 3; i++ )
	{
		const float u = points[i] * oldS;
		const float v = points[i] * oldT;
		st[i][0] = m0.x * u + m0.y * v + m0.z;
		st[i][1] = m1.x * u + m1.y * v + m1.z;
	}

	idVec3 moved[3];
	for( int i = 0; i < 3; i++ )
	{
		moved[i] = MulPoint( linear, points[i] ) + translation;
	}

	idVec3 normal = MulPoint( normalMatrix, oldNormal );
	if( normal.Normalize() < 1e-6f )
	{
		return;		// squashed flat
	}

	idPlane plane;
	plane.SetNormal( normal );
	plane[3] = -( normal * moved[0] );
	side->SetPlane( plane );

	if( !lockTexture )
	{
		return;
	}

	// find the texture matrix that puts the same texture coordinates on the moved points
	idVec3 newS, newT;
	TextureAxisBase( normal, newS, newT );

	idMat3 a;
	for( int i = 0; i < 3; i++ )
	{
		a[i].Set( moved[i] * newS, moved[i] * newT, 1.0f );
	}
	if( !a.InverseSelf() )
	{
		return;
	}

	idVec3 mat[2];
	for( int row = 0; row < 2; row++ )
	{
		const idVec3 target( st[0][row], st[1][row], st[2][row] );
		mat[row] = MulPoint( a, target );
	}
	side->SetTextureMatrix( mat );
}

static void TransformPrimitive( idMapPrimitive* prim, const idMat3& linear, const idMat3& normalMatrix, const idVec3& translation, bool lockTexture )
{
	if( prim->GetType() == idMapPrimitive::TYPE_BRUSH )
	{
		idMapBrush* brush = static_cast<idMapBrush*>( prim );
		for( int i = 0; i < brush->GetNumSides(); i++ )
		{
			TransformSide( brush->GetSide( i ), linear, normalMatrix, translation, lockTexture );
		}
	}
	else if( prim->GetType() == idMapPrimitive::TYPE_PATCH )
	{
		// patch texture coordinates are stored per point, so they follow by themselves
		idMapPatch* patch = static_cast<idMapPatch*>( prim );
		for( int i = 0; i < patch->GetWidth() * patch->GetHeight(); i++ )
		{
			( *patch )[i].xyz = MulPoint( linear, ( *patch )[i].xyz ) + translation;
		}
	}
}

static bool EntityAxis( const idMapEntity* ent, idMat3& axis )
{
	if( ent->epairs.GetMatrix( "rotation", "1 0 0 0 1 0 0 0 1", axis ) )
	{
		return true;
	}
	if( ent->epairs.FindKey( "angle" ) != NULL )
	{
		axis = idAngles( 0.0f, ent->epairs.GetFloat( "angle" ), 0.0f ).ToMat3();
		return true;
	}
	axis.Identity();
	return false;
}

static void SetEntityAxis( idMapEntity* ent, const idMat3& axis )
{
	// a plain turn about the vertical is written as "angle", which every tool understands
	const idVec3& forward = axis[0];
	const idVec3& up = axis[2];
	if( idMath::Fabs( forward.z ) < 0.0001f && up.z > 0.9999f )
	{
		float yaw = RAD2DEG( atan2( forward.y, forward.x ) );
		if( yaw < 0.0f )
		{
			yaw += 360.0f;
		}
		ent->epairs.Delete( "rotation" );
		if( idMath::Fabs( yaw ) < 0.01f || idMath::Fabs( yaw - 360.0f ) < 0.01f )
		{
			ent->epairs.Delete( "angle" );
		}
		else
		{
			ent->epairs.SetFloat( "angle", idMath::Rint( yaw * 100.0f ) / 100.0f );
		}
		return;
	}

	ent->epairs.Delete( "angle" );
	ent->epairs.SetMatrix( "rotation", axis );
}

static void TransformEntityKeys( idMapEntity* ent, const idMat3& linear, const idVec3& translation )
{
	if( ent->epairs.FindKey( "origin" ) != NULL || ent->GetNumPrimitives() == 0 )
	{
		const idVec3 origin = MulPoint( linear, ent->epairs.GetVector( "origin" ) ) + translation;
		idVec3 snapped = origin;
		for( int i = 0; i < 3; i++ )
		{
			snapped[i] = idMath::Rint( origin[i] * 100.0f ) / 100.0f;
		}
		ent->epairs.Set( "origin", snapped.ToString( 2 ) );
	}

	// Point entities turn with the transform. Brush entities don't get a rotation key:
	// their brushes are stored already turned, and a key would turn them a second time.
	if( ent->GetNumPrimitives() > 0 || linear.Compare( mat3_identity, 0.00001f ) )
	{
		return;
	}

	idMat3 axis;
	EntityAxis( ent, axis );

	idVec3 forward = MulPoint( linear, axis[0] );
	idVec3 up = MulPoint( linear, axis[2] );
	if( forward.Normalize() < 1e-6f || up.Normalize() < 1e-6f )
	{
		return;
	}
	idVec3 left = up.Cross( forward );
	if( left.Normalize() < 1e-6f )
	{
		return;
	}
	up = forward.Cross( left );
	up.Normalize();

	axis[0] = forward;
	axis[1] = left;
	axis[2] = up;
	SetEntityAxis( ent, axis );
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
	transforming = false;
	textureLock = true;
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
	transforming = false;
	transformStart.Clear();
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

/*
====================
Document::ConvertValveSides

Maps saved by TrenchBroom use the Valve 220 texture format. The editor works in,
and saves, Doom 3's own format, so faces are converted when the map opens.
====================
*/
void Document::ConvertValveSides()
{
	int converted = 0;
	for( int e = 0; e < map->GetNumEntities(); e++ )
	{
		idMapEntity* ent = map->GetEntity( e );
		for( int p = 0; p < ent->GetNumPrimitives(); p++ )
		{
			if( ent->GetPrimitive( p )->GetType() != idMapPrimitive::TYPE_BRUSH )
			{
				continue;
			}
			idMapBrush* brush = static_cast<idMapBrush*>( ent->GetPrimitive( p ) );
			for( int i = 0; i < brush->GetNumSides(); i++ )
			{
				idMapBrushSide* side = brush->GetSide( i );
				if( side->GetProjectionType() != idMapBrushSide::PROJECTION_VALVE220 )
				{
					continue;
				}

				idVec2i size = side->GetTextureSize();
				const idMaterial* material = declManager->FindMaterial( side->GetMaterial() );
				idImage* image = ( material != NULL ) ? material->GetEditorImage() : NULL;
				if( image != NULL && image->GetUploadWidth() > 0 )
				{
					size.x = image->GetUploadWidth();
					size.y = image->GetUploadHeight();
				}
				size.x = Max( size.x, 1 );
				size.y = Max( size.y, 1 );

				idVec4 vecs[2];
				side->GetTextureVectors( vecs );

				idVec3 texS, texT;
				TextureAxisBase( side->GetPlane().Normal(), texS, texT );

				idVec3 mat[2];
				for( int row = 0; row < 2; row++ )
				{
					const float scale = 1.0f / ( row == 0 ? size.x : size.y );
					const idVec3 axis = vecs[row].ToVec3() * scale;
					mat[row].Set( axis * texS, axis * texT, vecs[row].w * scale );
				}
				side->SetTextureMatrix( mat );
				side->projection = idMapBrushSide::PROJECTION_BP;
				converted++;
			}
		}
	}

	if( converted > 0 )
	{
		common->Printf( "Editor: converted %d faces from the Valve 220 texture format\n", converted );
	}
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
	ConvertValveSides();
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
	const int index = map->AddEntity( ent );
	selection.Set( Selection( index ) );
	EndChange();
	return index;
}

int Document::AddBrushes( idList<idMapBrush*>& brushes, const char* label )
{
	if( map == NULL || map->GetNumEntities() == 0 || brushes.Num() == 0 )
	{
		for( int i = 0; i < brushes.Num(); i++ )
		{
			delete brushes[i];
		}
		brushes.Clear();
		return -1;
	}

	BeginChange( label );
	idMapEntity* world = map->GetEntity( 0 );
	selection.Clear();
	for( int i = 0; i < brushes.Num(); i++ )
	{
		world->AddPrimitive( brushes[i] );
		selection.Add( Selection( 0, world->GetNumPrimitives() - 1 ) );
	}
	brushes.Clear();
	EndChange();
	return world->GetNumPrimitives() - 1;
}

void Document::SetMaterial( const char* material )
{
	if( map == NULL || selection.IsEmpty() )
	{
		return;
	}

	BeginChange( "Set material" );
	int changed = 0;
	for( int i = 0; i < selection.Num(); i++ )
	{
		const Selection& sel = selection[i];
		if( sel.entity < 0 || sel.entity >= map->GetNumEntities() )
		{
			continue;
		}
		idMapEntity* ent = map->GetEntity( sel.entity );
		for( int p = 0; p < ent->GetNumPrimitives(); p++ )
		{
			if( sel.primitive >= 0 && sel.primitive != p )
			{
				continue;
			}
			if( sel.primitive < 0 && sel.entity == 0 )
			{
				continue;	// never the whole level at once
			}
			idMapPrimitive* prim = ent->GetPrimitive( p );
			if( prim->GetType() == idMapPrimitive::TYPE_BRUSH )
			{
				idMapBrush* brush = static_cast<idMapBrush*>( prim );
				for( int s = 0; s < brush->GetNumSides(); s++ )
				{
					if( sel.side < 0 || sel.side == s )
					{
						brush->GetSide( s )->SetMaterial( material );
						changed++;
					}
				}
			}
			else if( prim->GetType() == idMapPrimitive::TYPE_PATCH )
			{
				static_cast<idMapPatch*>( prim )->SetMaterial( material );
				changed++;
			}
		}
	}

	if( changed > 0 )
	{
		EndChange();
	}
	else
	{
		CancelChange();
	}
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
	if( undoStack.Num() == 0 || transforming )
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
	ValidateSelection();
	return true;
}

bool Document::Redo()
{
	if( redoStack.Num() == 0 || transforming )
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
	ValidateSelection();
	return true;
}

/*
===============================================================================

	Selection

===============================================================================
*/

void SelectionSet::Add( const Selection& s )
{
	if( s.IsEmpty() || Contains( s ) )
	{
		return;
	}

	// objects and faces don't mix
	if( items.Num() > 0 && items[0].IsFace() != s.IsFace() )
	{
		items.Clear();
	}

	if( !s.IsFace() )
	{
		if( s.primitive < 0 )
		{
			// the whole entity covers its brushes
			for( int i = items.Num() - 1; i >= 0; i-- )
			{
				if( items[i].entity == s.entity )
				{
					items.RemoveIndex( i );
				}
			}
		}
		else if( s.entity != 0 && Contains( Selection( s.entity ) ) )
		{
			return;		// already covered by its entity
		}
	}

	items.Append( s );
}

void SelectionSet::Toggle( const Selection& s )
{
	if( Contains( s ) )
	{
		Remove( s );
	}
	else
	{
		Add( s );
	}
}

static bool SelectionExists( const idMapFile* map, const Selection& sel )
{
	if( map == NULL || sel.entity < 0 || sel.entity >= map->GetNumEntities() )
	{
		return false;
	}
	const idMapEntity* ent = map->GetEntity( sel.entity );
	if( sel.primitive >= ent->GetNumPrimitives() )
	{
		return false;
	}
	if( sel.side >= 0 )
	{
		const idMapPrimitive* prim = ent->GetPrimitive( sel.primitive );
		if( prim->GetType() != idMapPrimitive::TYPE_BRUSH || sel.side >= static_cast<const idMapBrush*>( prim )->GetNumSides() )
		{
			return false;
		}
	}
	return true;
}

bool Document::SelectionIsValid() const
{
	return !selection.IsEmpty() && SelectionExists( map, selection.Primary() );
}

void Document::ValidateSelection()
{
	for( int i = selection.items.Num() - 1; i >= 0; i-- )
	{
		if( !SelectionExists( map, selection.items[i] ) )
		{
			selection.items.RemoveIndex( i );
		}
	}
}

idMapEntity* Document::SelectedEntity() const
{
	return SelectionIsValid() ? map->GetEntity( selection.Primary().entity ) : NULL;
}

idMapPrimitive* Document::SelectedPrimitive() const
{
	const Selection sel = selection.Primary();
	if( !SelectionIsValid() || sel.primitive < 0 )
	{
		return NULL;
	}
	return map->GetEntity( sel.entity )->GetPrimitive( sel.primitive );
}

idMapBrushSide* Document::SelectedFace() const
{
	const Selection sel = selection.Primary();
	if( !SelectionIsValid() || sel.side < 0 )
	{
		return NULL;
	}
	return static_cast<idMapBrush*>( map->GetEntity( sel.entity )->GetPrimitive( sel.primitive ) )->GetSide( sel.side );
}

bool Document::CanTransformSelection() const
{
	if( map == NULL || selection.IsFaces() )
	{
		return false;
	}
	for( int i = 0; i < selection.Num(); i++ )
	{
		const Selection& sel = selection[i];
		if( SelectionExists( map, sel ) && ( sel.entity != 0 || sel.primitive >= 0 ) )
		{
			return true;
		}
	}
	return false;
}

bool Document::SelectionBounds( idBounds& bounds ) const
{
	bounds.Clear();
	for( int i = 0; i < selection.Num(); i++ )
	{
		const Selection& sel = selection[i];
		if( !SelectionExists( map, sel ) )
		{
			continue;
		}
		const idMapEntity* ent = map->GetEntity( sel.entity );
		idBounds b;
		if( sel.side >= 0 )
		{
			idWinding w;
			if( BrushSideWinding( static_cast<const idMapBrush*>( ent->GetPrimitive( sel.primitive ) ), sel.side, w ) )
			{
				w.GetBounds( b );
				bounds.AddBounds( b );
			}
		}
		else if( sel.primitive >= 0 )
		{
			if( PrimitiveBounds( ent->GetPrimitive( sel.primitive ), b ) )
			{
				bounds.AddBounds( b );
			}
		}
		else if( EntityBounds( ent, b ) )
		{
			bounds.AddBounds( b );
		}
	}
	return !bounds.IsCleared();
}

void Document::TranslateSelection( const idVec3& delta )
{
	TransformSelection( mat3_identity, delta );
}

void Document::TransformSelection( const idMat3& linear, const idVec3& translation )
{
	if( map == NULL || selection.IsFaces() )
	{
		return;
	}

	idMat3 normalMatrix = linear;
	if( !normalMatrix.InverseSelf() )
	{
		return;		// squashed to nothing
	}
	normalMatrix.TransposeSelf();

	for( int i = 0; i < selection.Num(); i++ )
	{
		const Selection& sel = selection[i];
		if( !SelectionExists( map, sel ) )
		{
			continue;
		}
		idMapEntity* ent = map->GetEntity( sel.entity );

		if( sel.primitive >= 0 )
		{
			TransformPrimitive( ent->GetPrimitive( sel.primitive ), linear, normalMatrix, translation, textureLock );
			continue;
		}
		if( sel.entity == 0 )
		{
			continue;	// the whole level doesn't move
		}

		TransformEntityKeys( ent, linear, translation );
		for( int p = 0; p < ent->GetNumPrimitives(); p++ )
		{
			TransformPrimitive( ent->GetPrimitive( p ), linear, normalMatrix, translation, textureLock );
		}
	}
}

void Document::SaveTransformStart()
{
	transformStart.Clear();
	for( int i = 0; i < selection.Num(); i++ )
	{
		const Selection& sel = selection[i];
		if( !SelectionExists( map, sel ) )
		{
			continue;
		}
		idMapEntity* ent = map->GetEntity( sel.entity );

		// the entity's keys, then each primitive it moves
		const int first = sel.primitive >= 0 ? sel.primitive : 0;
		const int last = sel.primitive >= 0 ? sel.primitive : ent->GetNumPrimitives() - 1;

		if( sel.primitive < 0 )
		{
			SavedItem& item = transformStart.Alloc();
			item.entity = sel.entity;
			item.primitive = -1;
			item.hasOrigin = ent->epairs.FindKey( "origin" ) != NULL;
			item.origin = ent->epairs.GetVector( "origin" );
			item.hasAxis = EntityAxis( ent, item.axis );
		}

		for( int p = first; p <= last; p++ )
		{
			SavedItem& item = transformStart.Alloc();
			item.entity = sel.entity;
			item.primitive = p;
			item.hasOrigin = false;
			item.hasAxis = false;
			const idMapPrimitive* prim = ent->GetPrimitive( p );
			if( prim->GetType() == idMapPrimitive::TYPE_BRUSH )
			{
				const idMapBrush* brush = static_cast<const idMapBrush*>( prim );
				for( int s = 0; s < brush->GetNumSides(); s++ )
				{
					SavedSide& saved = item.sides.Alloc();
					saved.plane = brush->GetSide( s )->GetPlane();
					brush->GetSide( s )->GetTextureMatrix( saved.texMat[0], saved.texMat[1] );
				}
			}
			else if( prim->GetType() == idMapPrimitive::TYPE_PATCH )
			{
				const idMapPatch* patch = static_cast<const idMapPatch*>( prim );
				for( int v = 0; v < patch->GetWidth() * patch->GetHeight(); v++ )
				{
					item.points.Append( ( *patch )[v].xyz );
				}
			}
		}
	}
}

void Document::BeginTransform()
{
	if( transforming || !CanTransformSelection() )
	{
		return;
	}
	transforming = true;
	SaveTransformStart();
}

void Document::UpdateTransform( const idMat3& linear, const idVec3& translation )
{
	if( !transforming )
	{
		return;
	}

	// back to where the drag started...
	for( int i = 0; i < transformStart.Num(); i++ )
	{
		const SavedItem& item = transformStart[i];
		idMapEntity* ent = map->GetEntity( item.entity );
		if( item.primitive < 0 )
		{
			if( item.hasOrigin )
			{
				ent->epairs.SetVector( "origin", item.origin );
			}
			if( item.hasAxis )
			{
				SetEntityAxis( ent, item.axis );
			}
			else
			{
				ent->epairs.Delete( "rotation" );
				ent->epairs.Delete( "angle" );
			}
			continue;
		}

		idMapPrimitive* prim = ent->GetPrimitive( item.primitive );
		if( prim->GetType() == idMapPrimitive::TYPE_BRUSH )
		{
			idMapBrush* brush = static_cast<idMapBrush*>( prim );
			for( int s = 0; s < item.sides.Num() && s < brush->GetNumSides(); s++ )
			{
				brush->GetSide( s )->SetPlane( item.sides[s].plane );
				brush->GetSide( s )->SetTextureMatrix( item.sides[s].texMat );
			}
		}
		else if( prim->GetType() == idMapPrimitive::TYPE_PATCH )
		{
			idMapPatch* patch = static_cast<idMapPatch*>( prim );
			for( int v = 0; v < item.points.Num(); v++ )
			{
				( *patch )[v].xyz = item.points[v];
			}
		}
	}

	// ...then the whole transform in one go
	TransformSelection( linear, translation );
	revision++;
	dirty = true;
}

void Document::EndTransform()
{
	transforming = false;
	transformStart.Clear();
}

// highest entity, then highest primitive, first; selections are short so a simple sort will do
static void SortDescending( idList<Selection>& list )
{
	for( int i = 1; i < list.Num(); i++ )
	{
		for( int j = i; j > 0; j-- )
		{
			const Selection& a = list[j - 1];
			const Selection& b = list[j];
			const bool inOrder = ( a.entity != b.entity ) ? ( a.entity > b.entity ) : ( a.primitive > b.primitive );
			if( inOrder )
			{
				break;
			}
			const Selection swap = list[j - 1];
			list[j - 1] = list[j];
			list[j] = swap;
		}
	}
}

void Document::DeleteSelection()
{
	if( map == NULL || selection.IsEmpty() || selection.IsFaces() )
	{
		return;
	}

	// brushes first, from the highest index down so the others keep their places,
	// then whole entities the same way
	idList<Selection> prims, ents;
	for( int i = 0; i < selection.Num(); i++ )
	{
		const Selection& sel = selection[i];
		if( !SelectionExists( map, sel ) )
		{
			continue;
		}
		if( sel.primitive >= 0 )
		{
			prims.Append( sel );
		}
		else if( sel.entity != 0 )
		{
			ents.Append( sel );
		}
	}

	if( prims.Num() == 0 && ents.Num() == 0 )
	{
		common->Warning( "Editor: the level itself (worldspawn) can't be deleted" );
		return;
	}

	BeginChange( ( prims.Num() + ents.Num() ) > 1 ? "Delete selection" : ( prims.Num() ? "Delete brush" : "Delete entity" ) );

	SortDescending( prims );
	SortDescending( ents );

	for( int i = 0; i < prims.Num(); i++ )
	{
		map->GetEntity( prims[i].entity )->RemovePrimitive( prims[i].primitive );
	}
	for( int i = 0; i < ents.Num(); i++ )
	{
		map->RemoveEntity( map->GetEntity( ents[i].entity ) );
	}

	selection.Clear();
	EndChange();
}

void Document::DuplicateSelection( const idVec3& offset )
{
	if( map == NULL || !CanTransformSelection() )
	{
		return;
	}

	BeginChange( "Duplicate" );

	SelectionSet copies;
	for( int i = 0; i < selection.Num(); i++ )
	{
		const Selection sel = selection[i];
		if( !SelectionExists( map, sel ) || ( sel.entity == 0 && sel.primitive < 0 ) )
		{
			continue;
		}
		idMapEntity* ent = map->GetEntity( sel.entity );

		// round-trip through text so every kind of primitive copies exactly
		idFile_Memory file( "editor_duplicate" );
		if( sel.primitive >= 0 )
		{
			idMapEntity* holder = new( TAG_SYSTEM ) idMapEntity();
			holder->epairs.Set( "classname", "worldspawn" );
			holder->AddPrimitive( ent->GetPrimitive( sel.primitive ) );
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
		idMapFile scratch;
		if( !scratch.ParseFromMemory( text.c_str(), text.Length(), "duplicate" ) || scratch.GetNumEntities() != 1 )
		{
			continue;
		}

		idMapEntity* copy = scratch.GetEntity( 0 );
		if( sel.primitive >= 0 )
		{
			ent->AddPrimitive( copy->DetachPrimitive( 0 ) );
			copies.Add( Selection( sel.entity, ent->GetNumPrimitives() - 1 ) );
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
			copies.Add( Selection( map->AddEntity( newEnt ) ) );
		}
	}

	selection = copies;
	TranslateSelection( offset );
	EndChange();
}

} // namespace GrizzEditor

#endif // GRIZZ_EDITOR
