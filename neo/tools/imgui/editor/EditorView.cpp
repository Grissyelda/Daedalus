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

#include "EditorView.h"

#include "../../imgui/BFGimgui.h"
#include "../../imgui/ImGui_Hooks.h"
#include "renderer/RenderCommon.h"

namespace DaedalusEditor
{

View view;

static const idVec4 COLOR_SELECTED( 0.20f, 0.83f, 0.60f, 1.0f );	// the emerald accent
static const idVec4 COLOR_WIRE( 0.75f, 0.78f, 0.82f, 1.0f );
static const idVec4 COLOR_FACE( 0.2f, 0.9f, 1.0f, 1.0f );
static const idVec4 COLOR_DOOR( 0.72f, 0.55f, 1.0f, 1.0f );	// violet, apart from the selection
static const char* TOOL_MATERIAL = "editor/toolsurface";

idCVar editor_lookSensitivity( "editor_lookSensitivity", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT | CVAR_NOCHEAT, "how fast the editor camera turns with the mouse", 0.1f, 10.0f );

/*
===============================================================================

	Camera

===============================================================================
*/

idMat3 EditorCamera::Axis() const
{
	return idAngles( pitch, yaw, 0.0f ).ToMat3();
}

void EditorCamera::Focus( const idBounds& bounds )
{
	if( bounds.IsCleared() )
	{
		return;
	}
	const float radius = Max( bounds.GetRadius(), 32.0f );
	origin = bounds.GetCenter() - Axis()[0] * ( radius * 1.6f + 32.0f );
}

/*
===============================================================================

	View

===============================================================================
*/

View::View()
{
	world = NULL;
	builtRevision = -1;
	builtMap = NULL;
}

void View::Shutdown()
{
	FreeAll();
	FreeGrid();
	if( world != NULL )
	{
		renderSystem->FreeRenderWorld( world );
		world = NULL;
	}
}

void View::FreeAll()
{
	for( int i = 0; i < built.Num(); i++ )
	{
		if( world != NULL && built[i].entityDef >= 0 )
		{
			world->FreeEntityDef( built[i].entityDef );
		}
		if( world != NULL && built[i].lightDef >= 0 )
		{
			world->FreeLightDef( built[i].lightDef );
		}
		if( built[i].model != NULL )
		{
			renderModelManager->FreeModel( built[i].model );
		}
		if( world != NULL && built[i].ghostDef >= 0 )
		{
			world->FreeEntityDef( built[i].ghostDef );
		}
		if( built[i].ghostModel != NULL )
		{
			renderModelManager->FreeModel( built[i].ghostModel );
		}
	}
	built.Clear();
}

static const idDeclEntityDef* EntityDefFor( const idMapEntity* ent )
{
	return static_cast<const idDeclEntityDef*>( declManager->FindType( DECL_ENTITYDEF, ent->epairs.GetString( "classname" ), false ) );
}

// spawn arguments as the game would see them: the definition's defaults, then the entity's own keys
static void MergedArgs( const idMapEntity* ent, idDict& args )
{
	args.Clear();
	const idDeclEntityDef* def = EntityDefFor( ent );
	if( def != NULL )
	{
		args = def->dict;
	}
	for( int i = 0; i < ent->epairs.GetNumKeyVals(); i++ )
	{
		const idKeyValue* kv = ent->epairs.GetKeyVal( i );
		args.Set( kv->GetKey(), kv->GetValue() );
	}
}

bool View::InFocus( const idDict& dict ) const
{
	return focusOwners.FindIndex( dict.GetString( "prefab_owner" ) ) >= 0;
}

idRenderModel* View::BuildBrushModel( const idMapEntity* ent, int entityNum, int focus )
{
	struct Bucket
	{
		const idMaterial*		material;
		idList<idDrawVert>		verts;
		idList<triIndex_t>		indexes;
	};
	idList<Bucket*> buckets;

	const idMaterial* toolMaterial = declManager->FindMaterial( TOOL_MATERIAL );

	for( int p = 0; p < ent->GetNumPrimitives(); p++ )
	{
		const idMapPrimitive* prim = ent->GetPrimitive( p );
		if( prim->GetType() != idMapPrimitive::TYPE_BRUSH )
		{
			continue;
		}
		const idMapBrush* brush = static_cast<const idMapBrush*>( prim );
		if( focus != 0 && InFocus( brush->epairs ) != ( focus == 1 ) )
		{
			continue;
		}

		for( int s = 0; s < brush->GetNumSides(); s++ )
		{
			idWinding w;
			if( !BrushSideWinding( brush, s, w ) )
			{
				continue;
			}

			const idMapBrushSide* side = brush->GetSide( s );
			const idMaterial* material = declManager->FindMaterial( side->GetMaterial() );
			if( material == NULL || !material->IsDrawn() )
			{
				// caulk, clip, visportals and the like are invisible in the game; show them as tool surfaces
				material = toolMaterial;
			}

			Bucket* bucket = NULL;
			for( int b = 0; b < buckets.Num(); b++ )
			{
				if( buckets[b]->material == material )
				{
					bucket = buckets[b];
					break;
				}
			}
			if( bucket == NULL )
			{
				bucket = new Bucket;
				bucket->material = material;
				buckets.Append( bucket );
			}

			idVec4 texVec[2];
			side->GetTextureVectors( texVec );
			float width = 1.0f, height = 1.0f;
			if( side->GetProjectionType() == idMapBrushSide::PROJECTION_VALVE220 )
			{
				// Valve 220 texture vectors are in pixels
				width = Max( 1.0f, ( float )material->GetImageWidth() );
				height = Max( 1.0f, ( float )material->GetImageHeight() );
			}

			const idVec3 normal = side->GetPlane().Normal();
			idVec3 tangent = texVec[0].ToVec3() - normal * ( normal * texVec[0].ToVec3() );
			idVec3 bitangent = texVec[1].ToVec3() - normal * ( normal * texVec[1].ToVec3() );
			tangent.Normalize();
			bitangent.Normalize();

			const int base = bucket->verts.Num();
			for( int i = 0; i < w.GetNumPoints(); i++ )
			{
				const idVec3 xyz = w[i].ToVec3();
				idDrawVert v;
				v.Clear();
				v.xyz = xyz;
				v.SetTexCoord( ( xyz * texVec[0].ToVec3() + texVec[0][3] ) / width, ( xyz * texVec[1].ToVec3() + texVec[1][3] ) / height );
				v.SetNormal( normal );
				v.SetTangent( tangent );
				v.SetBiTangent( bitangent );
				v.SetColor( 0xffffffff );
				bucket->verts.Append( v );
			}

			// triangle fan, ordered so the face points out of the brush
			for( int i = 2; i < w.GetNumPoints(); i++ )
			{
				const idVec3& a = w[0].ToVec3();
				const idVec3& b = w[i - 1].ToVec3();
				const idVec3& c = w[i].ToVec3();
				const bool frontFacing = ( ( c - a ).Cross( b - a ) * normal ) >= 0.0f;
				bucket->indexes.Append( base );
				bucket->indexes.Append( base + ( frontFacing ? i - 1 : i ) );
				bucket->indexes.Append( base + ( frontFacing ? i : i - 1 ) );
			}
		}
	}

	if( buckets.Num() == 0 )
	{
		return NULL;
	}

	idRenderModel* model = renderModelManager->AllocModel();
	model->InitEmpty( va( focus == 2 ? "_daedaluseditor_ghost%d" : "_daedaluseditor_entity%d", entityNum ) );

	for( int b = 0; b < buckets.Num(); b++ )
	{
		Bucket* bucket = buckets[b];
		if( bucket->indexes.Num() > 0 )
		{
			srfTriangles_t* tri = R_AllocStaticTriSurf();
			R_AllocStaticTriSurfVerts( tri, bucket->verts.Num() );
			R_AllocStaticTriSurfIndexes( tri, bucket->indexes.Num() );
			tri->numVerts = bucket->verts.Num();
			tri->numIndexes = bucket->indexes.Num();
			memcpy( tri->verts, bucket->verts.Ptr(), sizeof( idDrawVert ) * tri->numVerts );
			memcpy( tri->indexes, bucket->indexes.Ptr(), sizeof( triIndex_t ) * tri->numIndexes );
			tri->tangentsCalculated = true;
			R_BoundTriSurf( tri );

			modelSurface_t surf;
			surf.id = 0;
			surf.shader = bucket->material;
			surf.geometry = tri;
			model->AddSurface( surf );
		}
		delete bucket;
	}

	model->FinishSurfaces( false );
	return model;
}

void View::Sync( const Document& doc )
{
	if( world == NULL )
	{
		world = renderSystem->AllocRenderWorld();
		world->InitFromMap( NULL );
	}

	idStr focus;
	for( int i = 0; i < focusOwners.Num(); i++ )
	{
		focus += focusOwners[i] + " ";
	}
	if( doc.Map() == builtMap && doc.Revision() == builtRevision && focus == builtFocus )
	{
		return;
	}
	builtFocus = focus;

	FreeAll();
	FreeGrid();		// the world is cleared below; the grid goes back in on the next draw
	builtMap = doc.Map();
	builtRevision = doc.Revision();

	// start from an empty world each time: this also frees the light/entity interaction table
	world->InitFromMap( NULL );

	const idMapFile* map = doc.Map();
	if( map == NULL )
	{
		return;
	}

	built.SetNum( map->GetNumEntities() );
	for( int i = 0; i < map->GetNumEntities(); i++ )
	{
		const idMapEntity* ent = map->GetEntity( i );
		Built& b = built[i];

		// while a piece is edited in place, everything outside it is a ghost
		const bool focusing = focusOwners.Num() > 0;
		const bool ghost = focusing && i > 0 && !InFocus( ent->epairs );
		if( ent->GetNumPrimitives() > 0 )
		{
			b.model = BuildBrushModel( ent, i, ( focusing && i == 0 ) ? 1 : 0 );
			if( b.model != NULL )
			{
				renderEntity_t re;
				memset( &re, 0, sizeof( re ) );
				re.hModel = b.model;
				re.axis = mat3_identity;
				re.bounds = b.model->Bounds();
				re.entityNum = i;
				re.shaderParms[ SHADERPARM_RED ] = 1.0f;
				re.shaderParms[ SHADERPARM_GREEN ] = 1.0f;
				re.shaderParms[ SHADERPARM_BLUE ] = 1.0f;
				re.shaderParms[ SHADERPARM_ALPHA ] = 1.0f;
				if( ghost )
				{
					re.customShader = declManager->FindMaterial( "editor/ghost" );
				}
				b.entityDef = world->AddEntityDef( &re );
			}
			if( focusing && i == 0 )
			{
				b.ghostModel = BuildBrushModel( ent, i, 2 );
				if( b.ghostModel != NULL )
				{
					renderEntity_t re;
					memset( &re, 0, sizeof( re ) );
					re.hModel = b.ghostModel;
					re.axis = mat3_identity;
					re.bounds = b.ghostModel->Bounds();
					re.entityNum = i;
					re.shaderParms[ SHADERPARM_RED ] = 1.0f;
					re.shaderParms[ SHADERPARM_GREEN ] = 1.0f;
					re.shaderParms[ SHADERPARM_BLUE ] = 1.0f;
					re.shaderParms[ SHADERPARM_ALPHA ] = 1.0f;
					re.customShader = declManager->FindMaterial( "editor/ghost" );
					b.ghostDef = world->AddEntityDef( &re );
				}
			}
			continue;
		}

		idDict args;
		MergedArgs( ent, args );
		const idDeclEntityDef* def = EntityDefFor( ent );
		const idStr spawnClass = args.GetString( "spawnclass" );

		if( spawnClass == "idLight" || idStr::Icmp( ent->epairs.GetString( "classname" ), "light" ) == 0 )
		{
			renderLight_t rl;
			memset( &rl, 0, sizeof( rl ) );
			gameEdit->ParseSpawnArgsToRenderLight( &args, &rl );
			b.lightDef = world->AddLightDef( &rl );
		}
		else if( args.GetString( "model" )[0] != '\0' && args.GetString( "model" )[0] != '*' )
		{
			renderEntity_t re;
			memset( &re, 0, sizeof( re ) );
			gameEdit->ParseSpawnArgsToRenderEntity( &args, &re, def );
			if( re.hModel != NULL )
			{
				re.entityNum = i;
				if( ghost )
				{
					re.customShader = declManager->FindMaterial( "editor/ghost" );
				}
				b.entityDef = world->AddEntityDef( &re );
			}
		}
	}

	// Lights look up their interactions in a table that GenerateAllInteractions() normally
	// creates at map load. That call uploads geometry and can't run in the middle of a frame,
	// so allocate the (empty) table here and let interactions be created as lights need them,
	// as they are for moving entities in the game.
	idRenderWorldLocal* rw = static_cast<idRenderWorldLocal*>( world );
	if( rw->interactionTable == NULL )
	{
		rw->interactionTableWidth = rw->entityDefs.Num() + 100;
		rw->interactionTableHeight = rw->lightDefs.Num() + 100;
		const int size = rw->interactionTableWidth * rw->interactionTableHeight * sizeof( *rw->interactionTable );
		rw->interactionTable = ( idInteraction** )R_ClearedStaticAlloc( size );
	}
}

void View::BuildRenderView( renderView_t& rv ) const
{
	memset( &rv, 0, sizeof( rv ) );

	const ImVec2 size = ImGui::GetIO().DisplaySize;
	const float aspect = ( size.x > 0.0f && size.y > 0.0f ) ? size.y / size.x : 9.0f / 16.0f;

	rv.fov_x = camera.fov;
	rv.fov_y = RAD2DEG( atanf( tanf( DEG2RAD( camera.fov * 0.5f ) ) * aspect ) ) * 2.0f;
	rv.vieworg = camera.origin;
	rv.vieworg_weapon = camera.origin;
	rv.viewaxis = camera.Axis();
	rv.time[0] = rv.time[1] = Sys_Milliseconds();
}

void View::ScreenRay( float x, float y, idVec3& start, idVec3& dir ) const
{
	renderView_t rv;
	BuildRenderView( rv );

	const ImVec2 size = ImGui::GetIO().DisplaySize;
	const float nx = ( size.x > 0.0f ) ? ( 2.0f * x / size.x - 1.0f ) : 0.0f;
	const float ny = ( size.y > 0.0f ) ? ( 1.0f - 2.0f * y / size.y ) : 0.0f;

	// view axis: [0] forward, [1] left, [2] up
	start = rv.vieworg;
	dir = rv.viewaxis[0]
		  - rv.viewaxis[1] * ( nx * tanf( DEG2RAD( rv.fov_x * 0.5f ) ) )
		  + rv.viewaxis[2] * ( ny * tanf( DEG2RAD( rv.fov_y * 0.5f ) ) );
	dir.Normalize();
}

// The floor grid is a real surface: a big translucent quad with a grid texture, so walls
// and floors hide it the way they hide everything else. One texture repeat is 8 grid steps.
static const char* GRID_MATERIAL = "editor/floorgrid";
static const float GRID_EXTENT = 32768.0f;

void View::FreeGrid()
{
	if( world != NULL && gridEntity >= 0 )
	{
		world->FreeEntityDef( gridEntity );
	}
	gridEntity = -1;
	if( gridModel != NULL )
	{
		renderModelManager->FreeModel( gridModel );
		gridModel = NULL;
	}
}

void View::UpdateGrid()
{
	const float step = idMath::ClampFloat( 1.0f, 4096.0f, gridSize );
	if( !showGrid )
	{
		FreeGrid();
		return;
	}
	if( gridEntity >= 0 && gridBuiltStep == step && gridBuiltZ == gridZ )
	{
		return;
	}
	FreeGrid();
	gridBuiltStep = step;
	gridBuiltZ = gridZ;

	const float repeat = step * 8.0f;
	const float z = gridZ + 0.1f;		// a hair above, so floors built on it don't flicker through
	const idVec2 corners[4] = { idVec2( -GRID_EXTENT, -GRID_EXTENT ), idVec2( GRID_EXTENT, -GRID_EXTENT ), idVec2( GRID_EXTENT, GRID_EXTENT ), idVec2( -GRID_EXTENT, GRID_EXTENT ) };

	srfTriangles_t* tri = R_AllocStaticTriSurf();
	R_AllocStaticTriSurfVerts( tri, 4 );
	R_AllocStaticTriSurfIndexes( tri, 6 );
	tri->numVerts = 4;
	tri->numIndexes = 6;
	for( int i = 0; i < 4; i++ )
	{
		idDrawVert& v = tri->verts[i];
		v.Clear();
		v.xyz.Set( corners[i].x, corners[i].y, z );
		v.SetTexCoord( corners[i].x / repeat, -corners[i].y / repeat );
		v.SetNormal( idVec3( 0, 0, 1 ) );
		v.SetTangent( idVec3( 1, 0, 0 ) );
		v.SetBiTangent( idVec3( 0, -1, 0 ) );
		v.SetColor( 0xffffffff );
	}
	// facing up: (c - a) x (b - a) points along +Z
	const triIndex_t indexes[6] = { 0, 2, 1, 0, 3, 2 };
	memcpy( tri->indexes, indexes, sizeof( indexes ) );
	tri->tangentsCalculated = true;
	R_BoundTriSurf( tri );

	gridModel = renderModelManager->AllocModel();
	gridModel->InitEmpty( "_daedaluseditor_floorgrid" );
	modelSurface_t surf;
	surf.id = 0;
	surf.shader = declManager->FindMaterial( GRID_MATERIAL );
	surf.geometry = tri;
	gridModel->AddSurface( surf );
	gridModel->FinishSurfaces( false );

	renderEntity_t re;
	memset( &re, 0, sizeof( re ) );
	re.hModel = gridModel;
	re.axis = mat3_identity;
	re.bounds = gridModel->Bounds();
	re.noShadow = true;
	re.shaderParms[ SHADERPARM_RED ] = 1.0f;
	re.shaderParms[ SHADERPARM_GREEN ] = 1.0f;
	re.shaderParms[ SHADERPARM_BLUE ] = 1.0f;
	re.shaderParms[ SHADERPARM_ALPHA ] = 1.0f;
	gridEntity = world->AddEntityDef( &re );
}

bool View::WorldToScreen( const idVec3& point, idVec2& screen ) const
{
	renderView_t rv;
	BuildRenderView( rv );
	const idVec3 d = point - rv.vieworg;
	const float forward = d * rv.viewaxis[0];
	if( forward < 1.0f )
	{
		return false;
	}

	// the inverse of ScreenRay
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	const float nx = -( d * rv.viewaxis[1] ) / ( forward * tanf( DEG2RAD( rv.fov_x * 0.5f ) ) );
	const float ny = ( d * rv.viewaxis[2] ) / ( forward * tanf( DEG2RAD( rv.fov_y * 0.5f ) ) );
	screen.x = ( nx + 1.0f ) * 0.5f * size.x;
	screen.y = ( 1.0f - ny ) * 0.5f * size.y;
	return true;
}

// distance along the ray to a convex brush, or -1 when it misses; hitSide is the face it enters
static float RayBrush( const idMapBrush* brush, const idVec3& start, const idVec3& dir, int* hitSide = NULL )
{
	float enter = 0.0f;
	float leave = idMath::INFINITUM;
	float lastEnter = -idMath::INFINITUM;
	int enterSide = -1;

	for( int i = 0; i < brush->GetNumSides(); i++ )
	{
		const idPlane& plane = brush->GetSide( i )->GetPlane();
		const float d = plane.Distance( start );
		const float facing = plane.Normal() * dir;

		if( idMath::Fabs( facing ) < 1e-6f )
		{
			if( d > 0.0f )
			{
				return -1.0f;	// parallel and outside
			}
			continue;
		}

		const float t = -d / facing;
		if( facing < 0.0f )
		{
			if( t > lastEnter )
			{
				lastEnter = t;
				enterSide = i;
			}
			enter = Max( enter, t );
		}
		else
		{
			leave = Min( leave, t );
		}

		if( enter > leave )
		{
			return -1.0f;
		}
	}

	if( hitSide != NULL )
	{
		*hitSide = enterSide;
	}
	return enter;
}

Selection View::Pick( const Document& doc, float x, float y, bool faces ) const
{
	Selection best;
	const idMapFile* map = doc.Map();
	if( map == NULL )
	{
		return best;
	}

	idVec3 start, dir;
	ScreenRay( x, y, start, dir );

	float bestDist = idMath::INFINITUM;

	for( int i = 0; i < map->GetNumEntities(); i++ )
	{
		const idMapEntity* ent = map->GetEntity( i );

		if( ent->GetNumPrimitives() == 0 )
		{
			if( faces || ( focusOwners.Num() > 0 && !InFocus( ent->epairs ) ) )
			{
				continue;	// ghosts can't be picked
			}
			idBounds bounds;
			EntityBounds( ent, bounds );
			float scale;
			if( bounds.RayIntersection( start, dir, scale ) && scale >= 0.0f && scale < bestDist )
			{
				bestDist = scale;
				best = Selection( i );
			}
			continue;
		}

		for( int p = 0; p < ent->GetNumPrimitives(); p++ )
		{
			const idMapPrimitive* prim = ent->GetPrimitive( p );
			if( focusOwners.Num() > 0 && !InFocus( i == 0 ? prim->epairs : ent->epairs ) )
			{
				continue;
			}
			float dist = -1.0f;
			int side = -1;
			if( prim->GetType() == idMapPrimitive::TYPE_BRUSH )
			{
				dist = RayBrush( static_cast<const idMapBrush*>( prim ), start, dir, &side );
			}
			else if( !faces )
			{
				idBounds bounds;
				float scale;
				if( PrimitiveBounds( prim, bounds ) && bounds.RayIntersection( start, dir, scale ) )
				{
					dist = scale;
				}
			}

			if( dist >= 0.0f && dist < bestDist && ( !faces || side >= 0 ) )
			{
				bestDist = dist;
				if( faces )
				{
					best = Selection( i, p, side );
				}
				else
				{
					// level brushes are picked one at a time; an entity's brushes pick the whole entity
					best = Selection( i, ( i == 0 ) ? p : -1 );
				}
			}
		}
	}

	return best;
}

bool View::Trace( const Document& doc, float x, float y, idVec3& point, idVec3& normal ) const
{
	const Selection hit = Pick( doc, x, y, true );
	if( !hit.IsFace() )
	{
		return false;
	}

	idVec3 start, dir;
	ScreenRay( x, y, start, dir );
	const idMapBrush* brush = static_cast<const idMapBrush*>( doc.Map()->GetEntity( hit.entity )->GetPrimitive( hit.primitive ) );
	const float dist = RayBrush( brush, start, dir );
	if( dist < 0.0f )
	{
		return false;
	}
	point = start + dir * dist;
	normal = brush->GetSide( hit.side )->GetPlane().Normal();
	return true;
}

/*
====================
View::UpdateCamera

Unreal-style navigation:
	right mouse held		look around; WASD fly, Q/E down/up, Shift faster, wheel sets speed
	middle mouse held		pan
	Alt + left mouse held	orbit around orbitPivot
	wheel					move forward and back

While a button is held the pointer is hidden and kept in place, so it can't run
into the edge of the screen.
====================
*/
bool View::UpdateCamera( bool viewportHovered )
{
	ImGuiIO& io = ImGui::GetIO();
	const float dt = idMath::ClampFloat( 0.0f, 0.1f, io.DeltaTime );

	// which drag, if any, is moving the camera
	if( navMode == NAV_NONE && viewportHovered )
	{
		if( ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
		{
			navMode = NAV_LOOK;
		}
		else if( ImGui::IsMouseClicked( ImGuiMouseButton_Middle ) )
		{
			navMode = NAV_PAN;
		}
		else if( io.KeyAlt && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
		{
			navMode = NAV_ORBIT;
		}
		if( navMode != NAV_NONE )
		{
			navAnchor.Set( io.MousePos.x, io.MousePos.y );
		}
	}
	if( ( navMode == NAV_LOOK && !ImGui::IsMouseDown( ImGuiMouseButton_Right ) ) ||
			( navMode == NAV_PAN && !ImGui::IsMouseDown( ImGuiMouseButton_Middle ) ) ||
			( navMode == NAV_ORBIT && !ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) )
	{
		navMode = NAV_NONE;
	}

	// While a drag moves the camera the pointer is held still and hidden, and the raw
	// movement of the mouse (no pointer acceleration) drives the camera, as in a game.
	idVec2 delta( 0, 0 );
	if( navMode != NAV_NONE )
	{
		if( !navCaptured )
		{
			Sys_CaptureMouseForLook( true, ( int )navAnchor.x, ( int )navAnchor.y );
			navCaptured = true;
		}
		Sys_GetMouseLookDelta( delta.x, delta.y );
		delta *= editor_lookSensitivity.GetFloat();
		ImGuiHook::SetMousePos( navAnchor.x, navAnchor.y );
		ImGui::SetMouseCursor( ImGuiMouseCursor_None );
	}
	else if( navCaptured )
	{
		Sys_CaptureMouseForLook( false, ( int )navAnchor.x, ( int )navAnchor.y );
		ImGuiHook::SetMousePos( navAnchor.x, navAnchor.y );
		navCaptured = false;
	}

	const idMat3 axis = camera.Axis();

	switch( navMode )
	{
		case NAV_LOOK:
		{
			camera.yaw -= delta.x * 0.11f;
			camera.pitch = idMath::ClampFloat( -89.0f, 89.0f, camera.pitch + delta.y * 0.11f );

			float speed = camera.speed * ( io.KeyShift ? 3.0f : 1.0f );
			idVec3 move = vec3_zero;
			if( ImGui::IsKeyDown( ImGuiKey_W ) )
			{
				move += axis[0];
			}
			if( ImGui::IsKeyDown( ImGuiKey_S ) )
			{
				move -= axis[0];
			}
			if( ImGui::IsKeyDown( ImGuiKey_A ) )
			{
				move += axis[1];
			}
			if( ImGui::IsKeyDown( ImGuiKey_D ) )
			{
				move -= axis[1];
			}
			if( ImGui::IsKeyDown( ImGuiKey_E ) )
			{
				move += idVec3( 0, 0, 1 );
			}
			if( ImGui::IsKeyDown( ImGuiKey_Q ) )
			{
				move -= idVec3( 0, 0, 1 );
			}
			camera.origin += move * speed * dt;

			// the wheel changes flying speed while looking around
			if( io.MouseWheel != 0.0f )
			{
				camera.speed = idMath::ClampFloat( 32.0f, 4096.0f, camera.speed * ( io.MouseWheel > 0.0f ? 1.25f : 0.8f ) );
			}
			break;
		}

		case NAV_PAN:
		{
			// faster when far from what's in front, like dragging the world by hand
			const float scale = Max( 0.25f, camera.speed / 384.0f ) * 0.75f;
			camera.origin += axis[1] * delta.x * scale;
			camera.origin += axis[2] * delta.y * scale;
			break;
		}

		case NAV_ORBIT:
		{
			const float distance = Max( 16.0f, ( orbitPivot - camera.origin ).Length() );
			camera.yaw -= delta.x * 0.15f;
			camera.pitch = idMath::ClampFloat( -89.0f, 89.0f, camera.pitch + delta.y * 0.15f );
			camera.origin = orbitPivot - camera.Axis()[0] * distance;
			break;
		}

		default:
		{
			if( viewportHovered && io.MouseWheel != 0.0f )
			{
				// toward or away from what's under the pointer
				idVec3 start, dir;
				ScreenRay( io.MousePos.x, io.MousePos.y, start, dir );
				camera.origin += dir * io.MouseWheel * Max( 32.0f, camera.speed * 0.25f );
			}
			break;
		}
	}

	return navMode != NAV_NONE;
}

void View::ClearOutlines()
{
	if( world != NULL )
	{
		world->DebugClearLines( 0 );
	}
}

void View::Draw( const Document& doc )
{
	Sync( doc );

	if( world == NULL )
	{
		return;
	}

	// Outlines are debug lines, which the game clears every frame. Nothing else does
	// while editing, so clear them here or they pile up until the buffer is full.
	world->DebugClearLines( 0 );

	// the leak: a line from inside the level out through the gap
	for( int i = 0; i + 1 < leakPath.Num(); i++ )
	{
		world->DebugLine( colorRed, leakPath[i], leakPath[i + 1], 0, false );
	}
	if( leakPath.Num() > 0 )
	{
		world->DebugArrow( colorRed, leakPath[leakPath.Num() - 2 >= 0 ? leakPath.Num() - 2 : 0], leakPath[leakPath.Num() - 1], 12 );
	}

	const idMapFile* map = doc.Map();
	if( map != NULL )
	{
		// point entities: coloured boxes, as their definitions ask
		for( int i = 0; i < map->GetNumEntities(); i++ )
		{
			const idMapEntity* ent = map->GetEntity( i );
			if( ent->GetNumPrimitives() > 0 )
			{
				continue;
			}
			idBounds bounds;
			EntityBounds( ent, bounds );
			idVec3 color( 0.5f, 0.5f, 1.0f );
			const idDeclEntityDef* def = EntityDefFor( ent );
			if( def != NULL )
			{
				def->dict.GetVector( "editor_color", "0.5 0.5 1", color );
			}
			const bool selected = doc.selection.Contains( Selection( i ) );
			const idVec4 boxColor = selected ? COLOR_SELECTED : idVec4( color.x, color.y, color.z, 1.0f );

			// turned with the entity, with an arrow for the way it faces
			idMat3 axis;
			const bool turned = EntityAxis( ent, axis );
			const idVec3 origin = ent->epairs.GetVector( "origin" );
			world->DebugBox( boxColor, idBox( idBounds( bounds[0] - origin, bounds[1] - origin ), origin, axis ) );
			if( turned || idStr::Icmp( ent->epairs.GetString( "classname" ), "info_player_start" ) == 0 )
			{
				const float length = Max( 24.0f, ( bounds[1].x - bounds[0].x ) * 1.25f );
				const idVec3 center = bounds.GetCenter();
				world->DebugArrow( boxColor, center, center + axis[0] * length, 6 );
			}
		}



		// brush outlines: all of them in wireframe mode, otherwise just the selection
		for( int i = 0; i < map->GetNumEntities(); i++ )
		{
			const idMapEntity* ent = map->GetEntity( i );
			for( int p = 0; p < ent->GetNumPrimitives(); p++ )
			{
				const bool selected = ( i != 0 && doc.selection.Contains( Selection( i ) ) ) || doc.selection.Contains( Selection( i, p ) );
				const bool selectedWorldBrush = false;
				if( !wireframe && !selected && !selectedWorldBrush )
				{
					continue;
				}

				const idMapPrimitive* prim = ent->GetPrimitive( p );
				if( prim->GetType() != idMapPrimitive::TYPE_BRUSH )
				{
					idBounds bounds;
					if( PrimitiveBounds( prim, bounds ) )
					{
						world->DebugBounds( ( selected || selectedWorldBrush ) ? COLOR_SELECTED : COLOR_WIRE, bounds );
					}
					continue;
				}

				const idMapBrush* brush = static_cast<const idMapBrush*>( prim );
				for( int s = 0; s < brush->GetNumSides(); s++ )
				{
					idWinding w;
					if( BrushSideWinding( brush, s, w ) )
					{
						world->DebugWinding( ( selected || selectedWorldBrush ) ? COLOR_SELECTED : COLOR_WIRE, w, vec3_origin, mat3_identity );
					}
				}
			}
		}
	}

	// doors show which way they open and how far
	if( map != NULL && !doc.selection.IsFaces() )
	{
		for( int i = 0; i < doc.selection.Num(); i++ )
		{
			const Selection& sel = doc.selection[i];
			if( sel.entity <= 0 || sel.entity >= map->GetNumEntities() )
			{
				continue;
			}
			const idMapEntity* ent = map->GetEntity( sel.entity );
			if( idStr::Icmp( ent->epairs.GetString( "classname" ), "func_door" ) != 0 )
			{
				continue;
			}
			const idDeclEntityDef* def = EntityDefFor( ent );
			idDict args;
			if( def != NULL )
			{
				args.Copy( def->dict );
			}
			args.Copy( ent->epairs );

			const float dir = args.GetFloat( "movedir", args.GetString( "angle", "0" ) );
			idVec3 move;
			if( dir == -1.0f )
			{
				move.Set( 0, 0, 1 );
			}
			else if( dir == -2.0f )
			{
				move.Set( 0, 0, -1 );
			}
			else
			{
				move = idAngles( 0, dir, 0 ).ToForward();
			}

			idBounds bounds;
			EntityBounds( ent, bounds );
			const idVec3 size = bounds[1] - bounds[0];
			const float travel = idMath::Fabs( move.x ) * size.x + idMath::Fabs( move.y ) * size.y + idMath::Fabs( move.z ) * size.z - args.GetFloat( "lip", "8" );
			const idVec3 start = bounds.GetCenter();
			world->DebugArrow( COLOR_DOOR, start, start + move * Max( travel, 16.0f ), 8 );
			world->DebugBounds( COLOR_DOOR, bounds, move * Max( travel, 0.0f ) );
		}
	}

	// selected faces: their outline, plus a slightly smaller one inside so they stand out
	if( map != NULL && doc.selection.IsFaces() )
	{
		for( int i = 0; i < doc.selection.Num(); i++ )
		{
			const Selection& sel = doc.selection[i];
			if( sel.entity >= map->GetNumEntities() || sel.primitive >= map->GetEntity( sel.entity )->GetNumPrimitives() )
			{
				continue;
			}
			const idMapPrimitive* prim = map->GetEntity( sel.entity )->GetPrimitive( sel.primitive );
			if( prim->GetType() != idMapPrimitive::TYPE_BRUSH )
			{
				continue;
			}
			idWinding w;
			if( !BrushSideWinding( static_cast<const idMapBrush*>( prim ), sel.side, w ) )
			{
				continue;
			}
			world->DebugWinding( COLOR_FACE, w, vec3_origin, mat3_identity );

			const idVec3 center = w.GetCenter();
			const idVec3 lift = static_cast<const idMapBrush*>( prim )->GetSide( sel.side )->GetPlane().Normal() * 0.5f;
			for( int j = 0; j < w.GetNumPoints(); j++ )
			{
				w[j].ToVec3() = center + ( w[j].ToVec3() - center ) * 0.92f + lift;
			}
			world->DebugWinding( COLOR_FACE, w, vec3_origin, mat3_identity );
		}
	}

	UpdateGrid();

	renderView_t rv;
	BuildRenderView( rv );
	world->RenderScene( &rv );
}

} // namespace DaedalusEditor

#endif // DAEDALUS_EDITOR
