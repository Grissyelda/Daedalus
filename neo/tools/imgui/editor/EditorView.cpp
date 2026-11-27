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

#include "EditorView.h"

#include "../../imgui/BFGimgui.h"
#include "renderer/RenderCommon.h"

namespace GrizzEditor
{

View view;

static const idVec4 COLOR_SELECTED( 1.0f, 0.78f, 0.15f, 1.0f );
static const idVec4 COLOR_WIRE( 0.75f, 0.78f, 0.82f, 1.0f );
static const idVec4 COLOR_FACE( 0.2f, 0.9f, 1.0f, 1.0f );
static const char* TOOL_MATERIAL = "editor/toolsurface";

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

idRenderModel* View::BuildBrushModel( const idMapEntity* ent, int entityNum )
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
	model->InitEmpty( va( "_gt4editor_entity%d", entityNum ) );

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

	if( doc.Map() == builtMap && doc.Revision() == builtRevision )
	{
		return;
	}

	FreeAll();
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

		if( ent->GetNumPrimitives() > 0 )
		{
			b.model = BuildBrushModel( ent, i );
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
				b.entityDef = world->AddEntityDef( &re );
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
			if( faces )
			{
				continue;
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

void View::UpdateCamera( bool viewportHovered )
{
	ImGuiIO& io = ImGui::GetIO();
	const float dt = idMath::ClampFloat( 0.0f, 0.1f, io.DeltaTime );

	static bool looking = false;
	if( viewportHovered && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
	{
		looking = true;
	}
	if( !ImGui::IsMouseDown( ImGuiMouseButton_Right ) )
	{
		looking = false;
	}

	const idMat3 axis = camera.Axis();

	if( looking )
	{
		camera.yaw -= io.MouseDelta.x * 0.2f;
		camera.pitch = idMath::ClampFloat( -89.0f, 89.0f, camera.pitch + io.MouseDelta.y * 0.2f );

		float speed = camera.speed * ( ImGui::IsKeyDown( ImGuiKey_LeftShift ) ? 3.0f : 1.0f );
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
	}
	else if( viewportHovered && io.MouseWheel != 0.0f )
	{
		camera.origin += axis[0] * io.MouseWheel * 64.0f;
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
			world->DebugBounds( selected ? COLOR_SELECTED : idVec4( color.x, color.y, color.z, 1.0f ), bounds );
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

	renderView_t rv;
	BuildRenderView( rv );
	world->RenderScene( &rv );
}

} // namespace GrizzEditor

#endif // GRIZZ_EDITOR
