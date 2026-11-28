/*
===========================================================================

Daedalus editor: checking levels for problems

Copyright (C) 2026 the Daedalus authors

This file is part of Daedalus, which is based on the Doom 3 BFG Edition GPL
Source Code and RBDOOM-3-BFG. It is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version.

===========================================================================
*/

#include "precompiled.h"
#pragma hdrstop

#include "EditorProblems.h"
#include "EditorPrefabs.h"
#include "EditorImport.h"
#include "EditorIcons.h"
#include "renderer/RenderCommon.h"

namespace DaedalusEditor
{
namespace Problems
{

const char* SeverityName( severity_t severity )
{
	switch( severity )
	{
		case SEV_ERROR:
			return "error";
		case SEV_WARNING:
			return "warning";
		default:
			return "note";
	}
}

const char* SeverityIcon( severity_t severity )
{
	switch( severity )
	{
		case SEV_ERROR:
			return ICON_ERROR;
		case SEV_WARNING:
			return ICON_WARNING;
		default:
			return ICON_INFO;
	}
}

void Sort( idList<problem_t>& list )
{
	// a stable sort by severity: list sizes are small
	idList<problem_t> sorted;
	sorted.SetNum( 0 );
	for( int s = SEV_ERROR; s >= SEV_NOTE; s-- )
	{
		for( int i = 0; i < list.Num(); i++ )
		{
			if( list[i].severity == s )
			{
				sorted.Append( list[i] );
			}
		}
	}
	list = sorted;
}

void Count( const idList<problem_t>& list, int& errors, int& warnings, int& notes )
{
	errors = warnings = notes = 0;
	for( int i = 0; i < list.Num(); i++ )
	{
		switch( list[i].severity )
		{
			case SEV_ERROR:
				errors++;
				break;
			case SEV_WARNING:
				warnings++;
				break;
			default:
				notes++;
				break;
		}
	}
}

idStr Summary( const idList<problem_t>& list )
{
	int errors, warnings, notes;
	Count( list, errors, warnings, notes );
	if( errors + warnings + notes == 0 )
	{
		return "no problems";
	}
	idStr text;
	const int counts[3] = { errors, warnings, notes };
	const char* names[3] = { "error", "warning", "note" };
	for( int i = 0; i < 3; i++ )
	{
		if( counts[i] == 0 )
		{
			continue;
		}
		if( text.Length() > 0 )
		{
			text += ", ";
		}
		text += va( "%d %s%s", counts[i], names[i], counts[i] == 1 ? "" : "s" );
	}
	return text;
}

bool ImageFileExists( const char* name )
{
	idStr base = name;
	base.BackSlashesToSlashes();
	base.StripFileExtension();
	static const char* exts[] = { ".png", ".tga", ".jpg", ".jpeg", ".dds", ".exr", ".hdr", ".bmp" };
	for( int i = 0; i < sizeof( exts ) / sizeof( exts[0] ); i++ )
	{
		if( fileSystem->ReadFile( base + exts[i], NULL, NULL ) > 0 )
		{
			return true;
		}
	}
	return false;
}

void ListLevels( idStrList& maps )
{
	maps.Clear();
	idFileList* files = fileSystem->ListFilesTree( "maps", ".map", true );
	for( int i = 0; i < files->GetNumFiles(); i++ )
	{
		idStr path = files->GetFile( i );
		path.StripFileExtension();
		path.StripLeading( "maps/" );
		if( maps.FindIndex( path ) < 0 )
		{
			maps.Append( path );
		}
	}
	fileSystem->FreeFileList( files );
	maps.SortWithTemplate( idSort_PathStr() );
}

/*
-------------------------------------------------------------------------------
	the checks
-------------------------------------------------------------------------------
*/

static idStr Label( const idMapEntity* ent, int index )
{
	const char* name = ent->epairs.GetString( "name" );
	if( name[0] != '\0' )
	{
		return name;
	}
	return va( "%s %d", ent->epairs.GetString( "classname", "?" ), index );
}

static problem_t& Add( idList<problem_t>& out, severity_t severity, const char* code, const Selection& where )
{
	problem_t& p = out.Alloc();
	p = problem_t();
	p.severity = severity;
	p.code = code;
	p.where = where;
	return p;
}

// a vector ("x y z"), as projected lights keep in "target"
static bool IsVector( const char* value )
{
	float a, b, c;
	return sscanf( value, "%f %f %f", &a, &b, &c ) == 3;
}

// can the game find this model?
static bool ModelExists( const char* model )
{
	idStr path = model;
	idStr ext;
	path.ExtractFileExtension( ext );
	if( ext.Icmp( "prt" ) == 0 )
	{
		path.StripFileExtension();
		return declManager->FindType( DECL_PARTICLE, path, false ) != NULL;
	}
	if( ext.Length() == 0 && declManager->FindType( DECL_MODELDEF, path, false ) != NULL )
	{
		return true;
	}
	// glTF models can name one mesh inside the file: models/x.glb/mesh
	for( const char* kind : { ".glb/", ".gltf/" } )
	{
		const int at = path.Find( kind, false );
		if( at >= 0 )
		{
			path = path.Left( at + idStr::Length( kind ) - 1 );
			break;
		}
	}
	return fileSystem->ReadFile( path, NULL, NULL ) > 0;
}

// what's wrong with a material, if anything: "" when it's fine, else the missing picture's name
// (or the material's own name, when there's no material and no picture of that name)
static idStr MaterialTrouble( const char* name, idDict& cache )
{
	const idKeyValue* kv = cache.FindKey( name );
	if( kv != NULL )
	{
		return kv->GetValue();
	}
	idStr trouble;
	if( name[0] == '\0' )
	{
		trouble = "-";
	}
	else
	{
		const idMaterial* mat = static_cast<const idMaterial*>( declManager->FindType( DECL_MATERIAL, name, false ) );
		if( mat == NULL || mat->GetState() == DS_DEFAULTED || mat->IsImplicit() )
		{
			// no material: one made from a picture of the same name is fine
			if( !ImageFileExists( name ) )
			{
				trouble = name;
			}
		}
		else
		{
			for( int i = 0; i < mat->GetNumStages() && trouble.Length() == 0; i++ )
			{
				const idImage* image = mat->GetStage( i )->texture.image;
				if( image == NULL )
				{
					continue;
				}
				const char* imageName = image->GetName();
				if( imageName[0] == '_' || strchr( imageName, '(' ) != NULL )
				{
					continue;	// made by the engine, or built from other pictures
				}
				if( !ImageFileExists( imageName ) )
				{
					trouble = imageName;
				}
			}
		}
	}
	cache.Set( name, trouble );
	return trouble;
}

static void CheckMaterials( const idMapFile* map, idList<problem_t>& out )
{
	idDict cache;
	idStrList troubled;				// materials with trouble, in the order first met
	idList<Selection> firstUse;
	idList<int> uses;
	for( int e = 0; e < map->GetNumEntities(); e++ )
	{
		const idMapEntity* ent = map->GetEntity( e );
		for( int p = 0; p < ent->GetNumPrimitives(); p++ )
		{
			const idMapPrimitive* prim = ent->GetPrimitive( p );
			idStrList materials;
			if( prim->GetType() == idMapPrimitive::TYPE_BRUSH )
			{
				const idMapBrush* brush = static_cast<const idMapBrush*>( prim );
				for( int s = 0; s < brush->GetNumSides(); s++ )
				{
					materials.AddUnique( brush->GetSide( s )->GetMaterial() );
				}
			}
			else if( prim->GetType() == idMapPrimitive::TYPE_PATCH )
			{
				materials.AddUnique( static_cast<const idMapPatch*>( prim )->GetMaterial() );
			}
			for( int m = 0; m < materials.Num(); m++ )
			{
				if( MaterialTrouble( materials[m], cache ).Length() == 0 )
				{
					continue;
				}
				const int at = troubled.FindIndex( materials[m] );
				if( at < 0 )
				{
					troubled.Append( materials[m] );
					firstUse.Append( Selection( e, p ) );
					uses.Append( 1 );
				}
				else
				{
					uses[at]++;
				}
			}
		}
	}
	for( int i = 0; i < troubled.Num(); i++ )
	{
		const idStr trouble = MaterialTrouble( troubled[i], cache );
		const idStr shapes = uses[i] == 1 ? idStr( "1 shape" ) : idStr( va( "%d shapes", uses[i] ) );
		if( trouble == "-" )
		{
			problem_t& p = Add( out, SEV_WARNING, "missing_material", firstUse[i] );
			p.text = va( "%s %s no material, so %s as a checkerboard.", shapes.c_str(), uses[i] == 1 ? "has" : "have", uses[i] == 1 ? "it shows" : "they show" );
			p.next = "Paint them with a material from the Paint tab.";
			p.fix = FIX_USE_MATERIAL;
		}
		else if( trouble.Icmp( troubled[i] ) == 0 )
		{
			problem_t& p = Add( out, SEV_WARNING, "missing_material", firstUse[i] );
			p.text = va( "The material \"%s\" doesn't exist, so %s using it %s as a checkerboard.", troubled[i].c_str(), shapes.c_str(), uses[i] == 1 ? "shows" : "show" );
			p.next = "Paint over it with a material that exists, or add the material (a .mtr file in materials/, or a picture of that name in textures/).";
			p.fix = FIX_USE_MATERIAL;
		}
		else
		{
			problem_t& p = Add( out, SEV_WARNING, "missing_texture", firstUse[i] );
			p.text = va( "The material \"%s\" (on %s) uses the picture \"%s\", which isn't there.", troubled[i].c_str(), shapes.c_str(), trouble.c_str() );
			p.next = va( "Put the picture at %s.png (or .tga, .jpg), or paint with another material.", trouble.c_str() );
			p.fix = FIX_USE_MATERIAL;
		}
		out[out.Num() - 1].subject = troubled[i];
		out[out.Num() - 1].count = uses[i];
	}
}

void CheckLevel( const idMapFile* map, levelKind_t kind, idList<problem_t>& out )
{
	out.Clear();
	if( map == NULL || map->GetNumEntities() == 0 )
	{
		return;
	}

	// names, for links and duplicates (case matters, as in the game)
	idStrList nameList;
	idList<int> nameOwner;			// the first entity with each name
	idHashIndex nameHash;
	auto findName = [&]( const char* name ) -> int
	{
		for( int i = nameHash.First( idStr::Hash( name ) ); i != -1; i = nameHash.Next( i ) )
		{
			if( nameList[i].Cmp( name ) == 0 )
			{
				return nameOwner[i];
			}
		}
		return -1;
	};
	for( int e = 1; e < map->GetNumEntities(); e++ )
	{
		const char* name = map->GetEntity( e )->epairs.GetString( "name" );
		if( name[0] != '\0' && findName( name ) < 0 )
		{
			nameHash.Add( idStr::Hash( name ), nameList.Append( name ) );
			nameOwner.Append( e );
		}
	}

	bool hasStart = false;
	idStrList importsSeen;
	for( int e = 1; e < map->GetNumEntities(); e++ )
	{
		const idMapEntity* ent = map->GetEntity( e );
		const char* classname = ent->epairs.GetString( "classname" );
		const char* name = ent->epairs.GetString( "name" );
		const idStr label = Label( ent, e );

		if( idStr::Icmp( classname, "info_player_start" ) == 0 )
		{
			hasStart = true;
		}

		if( IsPrefabAnchor( ent ) )
		{
			if( !PrefabExists( ent->epairs.GetString( KEY_PREFAB ) ) )
			{
				problem_t& p = Add( out, SEV_WARNING, "piece_missing", Selection( e ) );
				p.text = va( "\"%s\" is a copy of the piece \"%s\", which is missing (maps/prefabs/%s.map), so it can't be updated.",
							 name, ent->epairs.GetString( KEY_PREFAB ), ent->epairs.GetString( KEY_PREFAB ) );
				p.next = "Unlink it to keep what's there as ordinary things, or put the piece's file back.";
				p.subject = ent->epairs.GetString( KEY_PREFAB );
				p.fix = FIX_UNLINK_PIECE;
			}
			continue;		// the game skips anchors: nothing else about them matters
		}

		if( classname[0] == '\0' || declManager->FindType( DECL_ENTITYDEF, classname, false ) == NULL )
		{
			problem_t& p = Add( out, SEV_ERROR, "unknown_type", Selection( e ) );
			p.text = classname[0] == '\0' ? va( "\"%s\" has no type, so it won't appear in the game.", label.c_str() )
					 : va( "\"%s\" is a \"%s\", a type that doesn't exist, so it won't appear in the game.", label.c_str(), classname );
			p.next = "Delete it, or give it a type that exists (the Inspector's Advanced card, classname).";
			p.subject = classname;
		}

		if( name[0] != '\0' && findName( name ) != e )
		{
			problem_t& p = Add( out, SEV_ERROR, "duplicate_name", Selection( e ) );
			p.text = va( "Two things are called \"%s\". The game stops loading the level when names clash.", name );
			p.next = "Rename this one (the fix picks a free name), and check anything that linked to it.";
			p.subject = name;
			p.fix = FIX_RENAME;
		}

		const char* model = ent->epairs.GetString( "model" );
		if( ent->GetNumPrimitives() > 0 )
		{
			if( idStr::Icmp( model, name ) != 0 )
			{
				problem_t& p = Add( out, SEV_ERROR, "model_name", Selection( e ) );
				p.text = va( "\"%s\" is made of brushes, but its 'model' setting doesn't match its name, so it won't appear in the game.", label.c_str() );
				p.next = "Fix sets 'model' to its name.";
				p.fix = FIX_MODEL_NAME;
			}
		}
		else if( model[0] != '\0' )
		{
			if( !ModelExists( model ) )
			{
				problem_t& p = Add( out, SEV_WARNING, "missing_model", Selection( e ) );
				p.text = va( "\"%s\" uses the model \"%s\", which isn't there, so the game shows a box instead.", label.c_str(), model );
				p.next = "Bring the model in again (Kits > Bring in), or pick another model in the Inspector.";
				p.subject = model;
			}
			const Import::model_t* imported = Import::FindByModel( model );
			if( imported != NULL && importsSeen.FindIndex( imported->name ) < 0 )
			{
				importsSeen.Append( imported->name );
				for( int i = 0; i < imported->warnings.Num(); i++ )
				{
					problem_t& p = Add( out, SEV_NOTE, "import_note", Selection( e ) );
					p.text = va( "%s: %s", imported->name.c_str(), imported->warnings[i].c_str() );
					p.next = "Nothing to fix here: this is what bringing the model in couldn't do. Change the source file and it comes in again.";
					p.subject = imported->name;
				}
				if( imported->sourceMissing )
				{
					problem_t& p = Add( out, SEV_NOTE, "import_source_gone", Selection( e ) );
					p.text = va( "%s: the file it was brought in from (%s) is gone, so it won't update any more.", imported->name.c_str(), imported->source.c_str() );
					p.next = "Bring the model in again from where it is now, if you want it to keep updating.";
					p.subject = imported->name;
				}
			}
		}

		// links to other things
		for( int k = 0; k < ent->epairs.GetNumKeyVals(); k++ )
		{
			const idKeyValue* kv = ent->epairs.GetKeyVal( k );
			const idStr& key = kv->GetKey();
			if( !( key.Icmpn( "target", 6 ) == 0 || key.Icmp( "bind" ) == 0 ) || kv->GetValue().Length() == 0 || IsVector( kv->GetValue() ) )
			{
				continue;
			}
			if( findName( kv->GetValue() ) < 0 )
			{
				problem_t& p = Add( out, SEV_WARNING, "broken_link", Selection( e ) );
				p.text = va( "\"%s\" links to \"%s\" (%s), but nothing has that name.", label.c_str(), kv->GetValue().c_str(), key.c_str() );
				p.next = "Point the link at something that exists in the Inspector, or remove it.";
				p.subject = key;
				p.fix = FIX_REMOVE_KEY;
			}
		}
	}

	if( kind == LEVEL && !hasStart )
	{
		problem_t& p = Add( out, SEV_ERROR, "no_start", Selection() );
		p.text = "The level has no player start, so Play has nowhere to put you.";
		p.next = "Add one, or drag Player start in from the Logic tab.";
		p.fix = FIX_ADD_START;
	}

	CheckMaterials( map, out );
	Sort( out );
}

} // namespace Problems
} // namespace DaedalusEditor
