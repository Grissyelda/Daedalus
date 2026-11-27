/*
===========================================================================

GrizzTech4 editor: bringing models in

Copyright (C) 2026 the GrizzTech4 authors

This file is part of GrizzTech4, which is based on the Doom 3 BFG Edition GPL
Source Code and RBDOOM-3-BFG. It is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version.

===========================================================================
*/

#include "precompiled.h"
#pragma hdrstop

#include "EditorImport.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#define STBI_NO_STDIO
#include "../../../libs/stb/stb_image.h"
#include "../../../libs/stb/stb_image_write.h"

namespace GrizzEditor
{
namespace Import
{

static const char*	IMPORTED_DIR = "models/imported";
static const char*	IMPORT_FOLDER = "import";
static const float	INCHES_PER_METER = 39.3700787f;
static const int	POLL_MS = 500;
static const int	SETTLE_MS = 700;		// a source must stop changing for this long before it's read

static idList<model_t>	models;
static bool				modelsRead = false;
static idStr			activity;
static int				activityTime = 0;
static int				lastPoll = 0;
static idStrList		importFolderKnown;		// files in the import folder already looked at, with their times
static idSysMutex		dropMutex;
static idStrList		dropped;

/*
-------------------------------------------------------------------------------
	small helpers
-------------------------------------------------------------------------------
*/

const char* MadeInLabel( madeIn_t madeIn )
{
	switch( madeIn )
	{
		case MADE_IN_METERS:
			return "Meters";
		case MADE_IN_CENTIMETERS:
			return "Centimeters";
		case MADE_IN_INCHES:
			return "Game units";
		default:
			return "Custom size";
	}
}

static const char* MadeInKey( madeIn_t madeIn )
{
	switch( madeIn )
	{
		case MADE_IN_METERS:
			return "meters";
		case MADE_IN_CENTIMETERS:
			return "centimeters";
		case MADE_IN_INCHES:
			return "units";
		default:
			return "custom";
	}
}

static madeIn_t MadeInFromKey( const char* key )
{
	for( int i = 0; i < MADE_IN_COUNT; i++ )
	{
		if( idStr::Icmp( key, MadeInKey( ( madeIn_t )i ) ) == 0 )
		{
			return ( madeIn_t )i;
		}
	}
	return MADE_IN_METERS;
}

float MadeInScale( madeIn_t madeIn, float custom )
{
	switch( madeIn )
	{
		case MADE_IN_METERS:
			return INCHES_PER_METER;
		case MADE_IN_CENTIMETERS:
			return INCHES_PER_METER * 0.01f;
		case MADE_IN_INCHES:
			return 1.0f;
		default:
			return custom > 0.0f ? custom : 1.0f;
	}
}

// names in paths and decls: lower case letters, digits, _ and -
static idStr CleanName( const char* text )
{
	idStr name;
	for( const char* c = text; *c != '\0'; c++ )
	{
		const char ch = idStr::ToLower( *c );
		if( ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' ) || ch == '_' || ch == '-' )
		{
			name += ch;
		}
		else if( name.Length() > 0 && name[name.Length() - 1] != '_' )
		{
			name += '_';
		}
	}
	name.StripTrailing( '_' );
	return name;
}

// the same cleaning the engine does when it looks a model's material up (Model_gltf.cpp)
static idStr MaterialPart( const char* text )
{
	idStr name;
	for( const char* c = text; *c != '\0'; c++ )
	{
		const char ch = idStr::ToLower( *c );
		const bool keep = ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' ) || ch == '_' || ch == '-';
		name += keep ? ch : '_';
	}
	return name;
}

bool IsModelFile( const char* path )
{
	idStr ext;
	idStr( path ).ExtractFileExtension( ext );
	return ext.Icmp( "glb" ) == 0 || ext.Icmp( "gltf" ) == 0 || ext.Icmp( "obj" ) == 0;
}

static void Activity( const char* text )
{
	activity = text;
	activityTime = Sys_Milliseconds();
	common->Printf( "Import: %s\n", text );
}

const char* LastActivity()
{
	if( activity.Length() > 0 && Sys_Milliseconds() - activityTime > 8000 )
	{
		activity.Clear();
	}
	return activity.c_str();
}

static idStr ModelFolder( const char* name )
{
	return va( "%s/%s", IMPORTED_DIR, name );
}

static idStr SettingsPath( const char* name )
{
	return va( "%s/%s/%s.import", IMPORTED_DIR, name, name );
}

static idStr MaterialFile( const char* name )
{
	// decl folders aren't searched below their top level, so this sits in materials/ itself
	return va( "materials/imported_%s.mtr", name );
}

static idStr GameFolderOS()
{
	idStr path = fileSystem->RelativePathToOSPath( "", "fs_basepath" );
	path.BackSlashesToSlashes();
	path.StripTrailing( '/' );
	return path;
}

// sources inside the project are kept relative to it, so the project can move
static idStr StoredSourcePath( const char* osPath )
{
	idStr path = osPath;
	path.BackSlashesToSlashes();
	const idStr game = GameFolderOS() + "/";
	if( path.Length() > game.Length() && path.Left( game.Length() ).Icmp( game ) == 0 )
	{
		return path.c_str() + game.Length();
	}
	return path;
}

static idStr SourceOSPath( const model_t& m )
{
	idStr path = m.source;
	path.BackSlashesToSlashes();
	const bool absolute = path.Length() > 0 && ( path[0] == '/' || ( path.Length() > 1 && path[1] == ':' ) );
	if( absolute )
	{
		return path;
	}
	return GameFolderOS() + "/" + path;
}

static bool ReadOSFile( const char* osPath, idList<byte>& bytes, ID_TIME_T* time = NULL )
{
	bytes.Clear();
	idFile* f = fileSystem->OpenExplicitFileRead( osPath );
	if( f == NULL )
	{
		return false;
	}
	const int length = f->Length();
	bytes.SetNum( length );
	const int got = length > 0 ? f->Read( bytes.Ptr(), length ) : 0;
	if( time != NULL )
	{
		*time = f->Timestamp();
	}
	fileSystem->CloseFile( f );
	return got == length;
}

static bool StatOSFile( const char* osPath, ID_TIME_T& time, int& length )
{
	idFile* f = fileSystem->OpenExplicitFileRead( osPath );
	if( f == NULL )
	{
		return false;
	}
	time = f->Timestamp();
	length = f->Length();
	fileSystem->CloseFile( f );
	return true;
}

static idStr OSFolderOf( const char* osPath )
{
	idStr folder = osPath;
	folder.BackSlashesToSlashes();
	folder.StripFilename();
	return folder;
}

// a path in a model file, relative to the folder the file is in
static idStr ResolveRelative( const idStr& folder, const char* uri )
{
	idStr decoded;
	for( const char* c = uri; *c != '\0'; c++ )
	{
		// glTF uris are URL-encoded; spaces are the common case
		if( c[0] == '%' && isxdigit( ( unsigned char )c[1] ) && isxdigit( ( unsigned char )c[2] ) )
		{
			const char hex[3] = { c[1], c[2], '\0' };
			decoded += ( char )strtol( hex, NULL, 16 );
			c += 2;
		}
		else
		{
			decoded += *c;
		}
	}
	decoded.BackSlashesToSlashes();
	if( decoded.Length() > 0 && ( decoded[0] == '/' || ( decoded.Length() > 1 && decoded[1] == ':' ) ) )
	{
		return decoded;
	}
	return folder + "/" + decoded;
}

static bool DecodeDataUri( const char* uri, idList<byte>& bytes, idStr* mimeType )
{
	// data:[<mime>][;base64],<data>
	const char* comma = strchr( uri, ',' );
	if( idStr::Cmpn( uri, "data:", 5 ) != 0 || comma == NULL )
	{
		return false;
	}
	idStr header( uri + 5, 0, ( int )( comma - uri - 5 ) );
	if( mimeType != NULL )
	{
		*mimeType = header;
		const int semi = mimeType->Find( ';' );
		if( semi >= 0 )
		{
			mimeType->CapLength( semi );
		}
	}
	if( header.Find( ";base64" ) < 0 )
	{
		return false;
	}
	idBase64 b64;
	b64 = idStr( comma + 1 );
	bytes.SetNum( b64.DecodeLength() );
	const int length = b64.Decode( bytes.Ptr() );
	bytes.SetNum( length );
	return true;
}

/*
-------------------------------------------------------------------------------
	images
-------------------------------------------------------------------------------
*/

struct image_t
{
	byte*	pixels = NULL;		// RGBA
	int		width = 0;
	int		height = 0;

	~image_t()
	{
		if( pixels != NULL )
		{
			stbi_image_free( pixels );
		}
	}
	bool Load( const byte* data, int length )
	{
		int channels = 0;
		pixels = stbi_load_from_memory( data, length, &width, &height, &channels, 4 );
		return pixels != NULL;
	}
	bool Valid() const
	{
		return pixels != NULL;
	}
	// a channel at a point given in another image's size (nearest), for mixing differently sized maps
	byte Sample( int x, int y, int otherWidth, int otherHeight, int channel ) const
	{
		const int sx = idMath::ClampInt( 0, width - 1, x * width / Max( 1, otherWidth ) );
		const int sy = idMath::ClampInt( 0, height - 1, y * height / Max( 1, otherHeight ) );
		return pixels[( sy * width + sx ) * 4 + channel];
	}
};

static void PngWrite( void* context, void* data, int size )
{
	idList<byte>* out = static_cast<idList<byte>*>( context );
	const int at = out->Num();
	out->SetNum( at + size );
	memcpy( out->Ptr() + at, data, size );
}

static bool WritePNG( const char* relativePath, const byte* rgba, int width, int height )
{
	idList<byte> png;
	if( !stbi_write_png_to_func( PngWrite, &png, width, height, 4, rgba, width * 4 ) )
	{
		return false;
	}
	return fileSystem->WriteFile( relativePath, png.Ptr(), png.Num(), "fs_basepath" ) == png.Num();
}

// where a picture comes from: bytes from the model, or a file next to it
struct imageSource_t
{
	idList<byte>	bytes;
	idStr			file;		// for messages
	bool Valid() const
	{
		return bytes.Num() > 0;
	}
	unsigned int Hash() const
	{
		return bytes.Num() > 0 ? MD5_BlockChecksum( bytes.Ptr(), bytes.Num() ) : 0;
	}
};

/*
-------------------------------------------------------------------------------
	materials

	Every material is a PBR material: base colour, normal map and an RMAO map
	(roughness, metal, ambient occlusion in red, green and blue: the engine
	takes a map as RMAO when its name ends in _rmao). Pictures are written as
	.png files, and only when what they're made from has changed.
-------------------------------------------------------------------------------
*/

struct material_t
{
	idStr			name;				// as the model calls it
	idVec4			baseColor = idVec4( 1, 1, 1, 1 );
	float			metal = 1.0f;		// glTF's defaults
	float			rough = 1.0f;
	idVec3			emissive = vec3_zero;
	bool			alphaMask = false;
	float			alphaCutoff = 0.5f;
	bool			alphaBlend = false;
	bool			twoSided = false;
	imageSource_t	base;
	imageSource_t	normal;
	imageSource_t	metalRough;			// glTF: roughness in green, metal in blue
	imageSource_t	roughOnly;			// OBJ: separate maps, value in red
	imageSource_t	metalOnly;
	imageSource_t	occlusion;			// red
	imageSource_t	emissiveMap;
};

struct writeContext_t
{
	idStr		name;				// the import's name
	idStr		texFolder;			// models/imported/<name>/textures
	idStrList	cacheIn;			// "file hash" lines from the last import
	idStrList	cacheOut;
	idStrList*	warnings;
	idStrList	writtenImages;		// image names (no extension) rewritten this time
	int			written = 0;
	int			reused = 0;
};

// write a picture made from these sources unless the same sources made it last time
template< typename func_t >
static bool WriteMap( writeContext_t& ctx, const char* file, unsigned int hash, func_t make )
{
	const idStr rel = ctx.texFolder + "/" + file;
	const idStr line = va( "%s %u", file, hash );
	ctx.cacheOut.Append( line );
	if( ctx.cacheIn.FindIndex( line ) >= 0 && fileSystem->GetFileLength( rel ) > 0 )
	{
		ctx.reused++;
		return true;
	}
	if( !make( rel ) )
	{
		ctx.warnings->Append( va( "Couldn't write the picture %s.", file ) );
		return false;
	}
	ctx.written++;
	idStr image = rel;
	image.StripFileExtension();
	ctx.writtenImages.Append( image );
	return true;
}

static bool CopyPicture( const char* rel, const imageSource_t& src, idStrList& warnings )
{
	image_t img;
	if( !img.Load( src.bytes.Ptr(), src.bytes.Num() ) )
	{
		warnings.Append( va( "Couldn't read the picture %s (%s).", src.file.c_str(), stbi_failure_reason() ) );
		return false;
	}
	return WritePNG( rel, img.pixels, img.width, img.height );
}

static idStr MaterialText( writeContext_t& ctx, const char* declName, material_t& mat )
{
	const idStr part = MaterialPart( mat.name );
	idStr text;
	text += va( "%s\n{\n", declName );
	text += va( "\t// \"%s\" from the model\n", mat.name.c_str() );
	if( mat.twoSided )
	{
		text += "\ttwoSided\n";
	}

	// base colour
	const bool tinted = !mat.baseColor.Compare( idVec4( 1, 1, 1, 1 ), 0.002f );
	idStr baseMap = "_white";
	if( mat.base.Valid() )
	{
		const idStr file = part + "_basecolor.png";
		if( WriteMap( ctx, file, mat.base.Hash(), [&]( const idStr & rel )
	{
		return CopyPicture( rel, mat.base, *ctx.warnings );
		} ) )
		{
			baseMap = ctx.texFolder + "/" + part + "_basecolor";
		}
	}
	text += "\t{\n\t\tblend\tbasecolormap\n";
	text += va( "\t\tmap\t\t%s\n", baseMap.c_str() );
	if( tinted )
	{
		text += va( "\t\tcolor\t%.4f, %.4f, %.4f, %.4f\n", mat.baseColor.x, mat.baseColor.y, mat.baseColor.z, mat.baseColor.w );
	}
	if( mat.alphaMask || mat.alphaBlend )
	{
		text += va( "\t\talphaTest\t%.3f\n", mat.alphaMask ? mat.alphaCutoff : 0.5f );
	}
	text += "\t}\n";
	if( mat.alphaBlend )
	{
		ctx.warnings->Append( va( "\"%s\" is see-through in the model; it's drawn with see-through parts cut out instead.", mat.name.c_str() ) );
	}

	// normal map
	if( mat.normal.Valid() )
	{
		const idStr file = part + "_normal.png";
		if( WriteMap( ctx, file, mat.normal.Hash(), [&]( const idStr & rel )
	{
		return CopyPicture( rel, mat.normal, *ctx.warnings );
		} ) )
		{
			text += va( "\tnormalmap\t%s/%s_normal\n", ctx.texFolder.c_str(), part.c_str() );
		}
	}

	// roughness, metal and occlusion, put together
	{
		const idStr file = part + "_rmao.png";
		const idStr values = va( "%.4f %.4f", mat.rough, mat.metal );
		unsigned int hash = MD5_BlockChecksum( values.c_str(), values.Length() );
		hash ^= mat.metalRough.Hash() * 3 ^ mat.roughOnly.Hash() * 5 ^ mat.metalOnly.Hash() * 7 ^ mat.occlusion.Hash() * 11;
		const bool ok = WriteMap( ctx, file, hash, [&]( const idStr & rel )
		{
			image_t mr, r, m, ao;
			if( mat.metalRough.Valid() )
			{
				mr.Load( mat.metalRough.bytes.Ptr(), mat.metalRough.bytes.Num() );
			}
			if( mat.roughOnly.Valid() )
			{
				r.Load( mat.roughOnly.bytes.Ptr(), mat.roughOnly.bytes.Num() );
			}
			if( mat.metalOnly.Valid() )
			{
				m.Load( mat.metalOnly.bytes.Ptr(), mat.metalOnly.bytes.Num() );
			}
			if( mat.occlusion.Valid() )
			{
				ao.Load( mat.occlusion.bytes.Ptr(), mat.occlusion.bytes.Num() );
			}
			// the biggest of the maps sets the size; with no maps at all a tiny one holds the values
			int w = 4, h = 4;
			const image_t* maps[4] = { &mr, &r, &m, &ao };
			for( int i = 0; i < 4; i++ )
			{
				if( maps[i]->Valid() && maps[i]->width * maps[i]->height > w * h )
				{
					w = maps[i]->width;
					h = maps[i]->height;
				}
			}
			idList<byte> out;
			out.SetNum( w * h * 4 );
			for( int y = 0; y < h; y++ )
			{
				for( int x = 0; x < w; x++ )
				{
					float rough = mat.rough;
					float metal = mat.metal;
					float occ = 1.0f;
					if( mr.Valid() )
					{
						rough *= mr.Sample( x, y, w, h, 1 ) / 255.0f;
						metal *= mr.Sample( x, y, w, h, 2 ) / 255.0f;
					}
					if( r.Valid() )
					{
						rough *= r.Sample( x, y, w, h, 0 ) / 255.0f;
					}
					if( m.Valid() )
					{
						metal *= m.Sample( x, y, w, h, 0 ) / 255.0f;
					}
					if( ao.Valid() )
					{
						occ = ao.Sample( x, y, w, h, 0 ) / 255.0f;
					}
					byte* p = &out[( y * w + x ) * 4];
					p[0] = ( byte )idMath::ClampInt( 0, 255, idMath::Ftoi( rough * 255.0f + 0.5f ) );
					p[1] = ( byte )idMath::ClampInt( 0, 255, idMath::Ftoi( metal * 255.0f + 0.5f ) );
					p[2] = ( byte )idMath::ClampInt( 0, 255, idMath::Ftoi( occ * 255.0f + 0.5f ) );
					p[3] = 128;		// room for a height map later
				}
			}
			return WritePNG( rel, out.Ptr(), w, h );
		} );
		if( ok )
		{
			text += va( "\trmaomap\t\t%s/%s_rmao\n", ctx.texFolder.c_str(), part.c_str() );
		}
	}

	// glow
	if( mat.emissiveMap.Valid() || mat.emissive.LengthSqr() > 0.0001f )
	{
		idStr glowMap = "_white";
		if( mat.emissiveMap.Valid() )
		{
			const idStr file = part + "_emissive.png";
			if( WriteMap( ctx, file, mat.emissiveMap.Hash(), [&]( const idStr & rel )
		{
			return CopyPicture( rel, mat.emissiveMap, *ctx.warnings );
			} ) )
			{
				glowMap = ctx.texFolder + "/" + part + "_emissive";
			}
		}
		const idVec3 glow = mat.emissiveMap.Valid() && mat.emissive.LengthSqr() < 0.0001f ? idVec3( 1, 1, 1 ) : mat.emissive;
		text += "\t{\n\t\tblend\tadd\n";
		text += va( "\t\tmap\t\t%s\n", glowMap.c_str() );
		text += va( "\t\tcolor\t%.4f, %.4f, %.4f, 1\n", glow.x, glow.y, glow.z );
		text += "\t}\n";
	}

	text += "}\n\n";
	return text;
}

static bool WriteMaterials( const model_t& m, idList<material_t>& mats, idStrList& cacheIn, idStrList& cacheOut, idStrList& warnings, idStrList& writtenImages, bool& textChanged )
{
	writeContext_t ctx;
	ctx.name = m.name;
	ctx.texFolder = ModelFolder( m.name ) + "/textures";
	ctx.cacheIn = cacheIn;
	ctx.warnings = &warnings;

	idStr prefix = m.model;
	prefix.StripFileExtension();

	idStr text;
	text += va( "/*\n\tMaterials for the imported model \"%s\", written by the GrizzTech4 editor.\n", m.name.c_str() );
	text += "\tThey're written again each time the model is brought in again, so changes made here\n";
	text += "\tdon't last: change the model, or make a material of your own with another name.\n*/\n\n";
	for( int i = 0; i < mats.Num(); i++ )
	{
		const idStr declName = prefix + "/" + MaterialPart( mats[i].name );
		text += MaterialText( ctx, declName, mats[i] );
	}
	cacheOut = ctx.cacheOut;
	writtenImages = ctx.writtenImages;

	const idStr file = MaterialFile( m.name );
	{
		// unchanged materials aren't written (or reloaded) again
		char* old = NULL;
		const int oldLength = fileSystem->ReadFile( file, ( void** )&old );
		textChanged = oldLength != text.Length() || old == NULL || memcmp( old, text.c_str(), text.Length() ) != 0;
		if( old != NULL )
		{
			fileSystem->FreeFile( old );
		}
		if( !textChanged )
		{
			common->Printf( "Import: materials unchanged, %d pictures written, %d unchanged\n", ctx.written, ctx.reused );
			return true;
		}
	}
	if( fileSystem->WriteFile( file, text.c_str(), text.Length(), "fs_basepath" ) != text.Length() )
	{
		warnings.Append( va( "Couldn't write %s.", file.c_str() ) );
		return false;
	}
	common->Printf( "Import: %d materials, %d pictures written, %d unchanged\n", mats.Num(), ctx.written, ctx.reused );
	return true;
}

/*
-------------------------------------------------------------------------------
	glTF

	Whatever it came as (.glb, or .gltf with its .bin and pictures beside it),
	the project gets one .glb with everything inside: one buffer holding all
	the data, and every picture stored in it. The engine reads paths in a
	.gltf from the top of the project, not from beside the file, and stops
	when one is missing, so a self-contained .glb is the safe form.
-------------------------------------------------------------------------------
*/

static const unsigned int GLB_MAGIC = 0x46546C67;	// "glTF"
static const unsigned int GLB_JSON = 0x4E4F534A;
static const unsigned int GLB_BIN = 0x004E4942;

static unsigned int ReadU32( const byte* p )
{
	return ( unsigned int )p[0] | ( ( unsigned int )p[1] << 8 ) | ( ( unsigned int )p[2] << 16 ) | ( ( unsigned int )p[3] << 24 );
}

static void AppendU32( idList<byte>& out, unsigned int v )
{
	out.Append( ( byte )( v & 0xff ) );
	out.Append( ( byte )( ( v >> 8 ) & 0xff ) );
	out.Append( ( byte )( ( v >> 16 ) & 0xff ) );
	out.Append( ( byte )( ( v >> 24 ) & 0xff ) );
}

static void Align4( idList<byte>& bin, byte pad )
{
	while( bin.Num() & 3 )
	{
		bin.Append( pad );
	}
}

struct gltfSource_t
{
	rapidjson::Document		doc;
	idList<byte>			bin;		// one buffer with all the data, after Flatten()
};

static bool ReadGltf( const char* osPath, gltfSource_t& src, idStr& error )
{
	idList<byte> file;
	if( !ReadOSFile( osPath, file ) || file.Num() == 0 )
	{
		error = "The file couldn't be read.";
		return false;
	}

	idStr json;
	idList<byte> glbBin;
	if( file.Num() >= 12 && ReadU32( file.Ptr() ) == GLB_MAGIC )
	{
		if( ReadU32( file.Ptr() + 4 ) != 2 )
		{
			error = "It's an old glTF 1 file; export it again as glTF 2.";
			return false;
		}
		int at = 12;
		while( at + 8 <= file.Num() )
		{
			const unsigned int length = ReadU32( file.Ptr() + at );
			const unsigned int type = ReadU32( file.Ptr() + at + 4 );
			if( at + 8 + ( int )length > file.Num() )
			{
				error = "The file is cut short (was it still being saved?).";
				return false;
			}
			if( type == GLB_JSON )
			{
				json = idStr( ( const char* )file.Ptr() + at + 8, 0, length );
			}
			else if( type == GLB_BIN && glbBin.Num() == 0 )
			{
				glbBin.SetNum( length );
				memcpy( glbBin.Ptr(), file.Ptr() + at + 8, length );
			}
			at += 8 + ( ( length + 3 ) & ~3u );
		}
	}
	else
	{
		json = idStr( ( const char* )file.Ptr(), 0, file.Num() );
	}

	src.doc.Parse( json.c_str() );
	if( src.doc.HasParseError() || !src.doc.IsObject() )
	{
		error = "The file isn't a glTF model that can be read (it may be damaged, or still being saved).";
		return false;
	}
	if( !src.doc.HasMember( "asset" ) || !src.doc["asset"].IsObject() || !src.doc["asset"].HasMember( "version" ) ||
			!src.doc["asset"]["version"].IsString() || src.doc["asset"]["version"].GetString()[0] != '2' )
	{
		error = "Only glTF 2 models can be brought in.";
		return false;
	}
	if( !src.doc.HasMember( "meshes" ) || !src.doc["meshes"].IsArray() || src.doc["meshes"].Size() == 0 )
	{
		error = "The file has no meshes in it.";
		return false;
	}

	// gather every buffer into one
	rapidjson::Document::AllocatorType& alloc = src.doc.GetAllocator();
	const idStr folder = OSFolderOf( osPath );
	idList<int> bufferBase;
	if( src.doc.HasMember( "buffers" ) && src.doc["buffers"].IsArray() )
	{
		rapidjson::Value& buffers = src.doc["buffers"];
		for( rapidjson::SizeType i = 0; i < buffers.Size(); i++ )
		{
			idList<byte> data;
			if( buffers[i].HasMember( "uri" ) && buffers[i]["uri"].IsString() )
			{
				const char* uri = buffers[i]["uri"].GetString();
				if( !DecodeDataUri( uri, data, NULL ) && !ReadOSFile( ResolveRelative( folder, uri ), data ) )
				{
					error = va( "The data file \"%s\" next to the model is missing.", uri );
					return false;
				}
			}
			else
			{
				data = glbBin;
			}
			Align4( src.bin, 0 );
			bufferBase.Append( src.bin.Num() );
			const int at = src.bin.Num();
			src.bin.SetNum( at + data.Num() );
			if( data.Num() > 0 )
			{
				memcpy( src.bin.Ptr() + at, data.Ptr(), data.Num() );
			}
		}
	}
	if( src.doc.HasMember( "bufferViews" ) && src.doc["bufferViews"].IsArray() )
	{
		rapidjson::Value& views = src.doc["bufferViews"];
		for( rapidjson::SizeType i = 0; i < views.Size(); i++ )
		{
			rapidjson::Value& v = views[i];
			const int buffer = v.HasMember( "buffer" ) && v["buffer"].IsInt() ? v["buffer"].GetInt() : 0;
			if( buffer < 0 || buffer >= bufferBase.Num() )
			{
				error = "The model points at data that isn't there.";
				return false;
			}
			const int offset = v.HasMember( "byteOffset" ) && v["byteOffset"].IsInt() ? v["byteOffset"].GetInt() : 0;
			if( v.HasMember( "buffer" ) )
			{
				v["buffer"].SetInt( 0 );
			}
			else
			{
				v.AddMember( "buffer", 0, alloc );
			}
			if( v.HasMember( "byteOffset" ) )
			{
				v["byteOffset"].SetInt( offset + bufferBase[buffer] );
			}
			else
			{
				v.AddMember( "byteOffset", offset + bufferBase[buffer], alloc );
			}
		}
	}

	// pictures stored as files beside the model go inside it
	if( src.doc.HasMember( "images" ) && src.doc["images"].IsArray() )
	{
		rapidjson::Value& images = src.doc["images"];
		for( rapidjson::SizeType i = 0; i < images.Size(); i++ )
		{
			rapidjson::Value& img = images[i];
			if( !img.HasMember( "uri" ) || !img["uri"].IsString() )
			{
				continue;
			}
			const char* uri = img["uri"].GetString();
			idList<byte> data;
			idStr mime;
			if( !DecodeDataUri( uri, data, &mime ) )
			{
				ReadOSFile( ResolveRelative( folder, uri ), data );
				idStr ext;
				idStr( uri ).ExtractFileExtension( ext );
				mime = ext.Icmp( "png" ) == 0 ? "image/png" : "image/jpeg";
			}
			img.RemoveMember( "uri" );
			if( data.Num() == 0 )
			{
				continue;		// a missing picture is reported when the material asks for it
			}
			Align4( src.bin, 0 );
			rapidjson::Value view( rapidjson::kObjectType );
			view.AddMember( "buffer", 0, alloc );
			view.AddMember( "byteOffset", src.bin.Num(), alloc );
			view.AddMember( "byteLength", data.Num(), alloc );
			if( !src.doc.HasMember( "bufferViews" ) )
			{
				src.doc.AddMember( "bufferViews", rapidjson::Value( rapidjson::kArrayType ), alloc );
			}
			src.doc["bufferViews"].PushBack( view, alloc );
			const int at = src.bin.Num();
			src.bin.SetNum( at + data.Num() );
			memcpy( src.bin.Ptr() + at, data.Ptr(), data.Num() );
			img.AddMember( "bufferView", ( int )src.doc["bufferViews"].Size() - 1, alloc );
			if( !img.HasMember( "mimeType" ) )
			{
				rapidjson::Value mimeValue( mime.c_str(), alloc );
				img.AddMember( "mimeType", mimeValue, alloc );
			}
		}
	}
	Align4( src.bin, 0 );

	// one buffer, inside the file
	rapidjson::Value buffers( rapidjson::kArrayType );
	rapidjson::Value buffer( rapidjson::kObjectType );
	buffer.AddMember( "byteLength", src.bin.Num(), alloc );
	buffers.PushBack( buffer, alloc );
	if( src.doc.HasMember( "buffers" ) )
	{
		src.doc["buffers"] = buffers;
	}
	else
	{
		src.doc.AddMember( "buffers", buffers, alloc );
	}
	return true;
}

static void WriteGlb( gltfSource_t& src, idList<byte>& out )
{
	// the engine's reader wants the buffers and their views before anything that uses them
	rapidjson::Document ordered;
	ordered.SetObject();
	rapidjson::Document::AllocatorType& alloc = ordered.GetAllocator();
	const char* first[] = { "asset", "buffers", "bufferViews" };
	for( int i = 0; i < 3; i++ )
	{
		if( src.doc.HasMember( first[i] ) )
		{
			rapidjson::Value key( first[i], alloc );
			rapidjson::Value value( src.doc[first[i]], alloc );
			ordered.AddMember( key, value, alloc );
		}
	}
	for( rapidjson::Value::ConstMemberIterator it = src.doc.MemberBegin(); it != src.doc.MemberEnd(); ++it )
	{
		const char* name = it->name.GetString();
		if( idStr::Cmp( name, "asset" ) == 0 || idStr::Cmp( name, "buffers" ) == 0 || idStr::Cmp( name, "bufferViews" ) == 0 )
		{
			continue;
		}
		rapidjson::Value key( name, alloc );
		rapidjson::Value value( it->value, alloc );
		ordered.AddMember( key, value, alloc );
	}

	rapidjson::StringBuffer sb;
	rapidjson::Writer<rapidjson::StringBuffer> writer( sb );
	ordered.Accept( writer );

	idList<byte> json;
	json.SetNum( ( int )sb.GetSize() );
	memcpy( json.Ptr(), sb.GetString(), sb.GetSize() );
	Align4( json, ' ' );

	out.Clear();
	AppendU32( out, GLB_MAGIC );
	AppendU32( out, 2 );
	AppendU32( out, 12 + 8 + json.Num() + ( src.bin.Num() > 0 ? 8 + src.bin.Num() : 0 ) );
	AppendU32( out, json.Num() );
	AppendU32( out, GLB_JSON );
	out.Append( json );
	if( src.bin.Num() > 0 )
	{
		AppendU32( out, src.bin.Num() );
		AppendU32( out, GLB_BIN );
		out.Append( src.bin );
	}
}

// the bytes of a picture a material uses, by texture index
static bool GltfTextureBytes( gltfSource_t& src, const rapidjson::Value& info, imageSource_t& out )
{
	if( !info.IsObject() || !info.HasMember( "index" ) || !info["index"].IsInt() )
	{
		return false;
	}
	const rapidjson::Value& d = src.doc;
	const int tex = info["index"].GetInt();
	if( !d.HasMember( "textures" ) || tex < 0 || tex >= ( int )d["textures"].Size() )
	{
		return false;
	}
	const rapidjson::Value& t = d["textures"][tex];
	if( !t.HasMember( "source" ) || !t["source"].IsInt() )
	{
		return false;
	}
	const int image = t["source"].GetInt();
	if( !d.HasMember( "images" ) || image < 0 || image >= ( int )d["images"].Size() )
	{
		return false;
	}
	const rapidjson::Value& img = d["images"][image];
	out.file = img.HasMember( "name" ) && img["name"].IsString() ? img["name"].GetString() : va( "picture %d", image );
	if( !img.HasMember( "bufferView" ) || !img["bufferView"].IsInt() )
	{
		return false;
	}
	const rapidjson::Value& view = d["bufferViews"][img["bufferView"].GetInt()];
	const int offset = view.HasMember( "byteOffset" ) ? view["byteOffset"].GetInt() : 0;
	const int length = view["byteLength"].GetInt();
	if( offset < 0 || offset + length > src.bin.Num() )
	{
		return false;
	}
	out.bytes.SetNum( length );
	memcpy( out.bytes.Ptr(), src.bin.Ptr() + offset, length );
	return true;
}

static float JsonFloat( const rapidjson::Value& v, const char* key, float def )
{
	return v.IsObject() && v.HasMember( key ) && v[key].IsNumber() ? v[key].GetDouble() : def;
}

static void GltfMaterials( gltfSource_t& src, idList<material_t>& mats, idStrList& warnings )
{
	const rapidjson::Value& d = src.doc;
	if( !d.HasMember( "materials" ) || !d["materials"].IsArray() )
	{
		return;
	}
	const rapidjson::Value& list = d["materials"];
	for( rapidjson::SizeType i = 0; i < list.Size(); i++ )
	{
		const rapidjson::Value& m = list[i];
		material_t& mat = mats.Alloc();
		mat.name = m.HasMember( "name" ) && m["name"].IsString() && m["name"].GetString()[0] ? m["name"].GetString() : va( "material_%d", ( int )i );

		if( m.HasMember( "pbrMetallicRoughness" ) && m["pbrMetallicRoughness"].IsObject() )
		{
			const rapidjson::Value& pbr = m["pbrMetallicRoughness"];
			if( pbr.HasMember( "baseColorFactor" ) && pbr["baseColorFactor"].IsArray() && pbr["baseColorFactor"].Size() == 4 )
			{
				for( int c = 0; c < 4; c++ )
				{
					mat.baseColor[c] = pbr["baseColorFactor"][c].GetDouble();
				}
			}
			mat.metal = JsonFloat( pbr, "metallicFactor", 1.0f );
			mat.rough = JsonFloat( pbr, "roughnessFactor", 1.0f );
			if( pbr.HasMember( "baseColorTexture" ) && !GltfTextureBytes( src, pbr["baseColorTexture"], mat.base ) )
			{
				warnings.Append( va( "The colour picture for \"%s\" isn't in the model.", mat.name.c_str() ) );
			}
			if( pbr.HasMember( "metallicRoughnessTexture" ) )
			{
				GltfTextureBytes( src, pbr["metallicRoughnessTexture"], mat.metalRough );
			}
		}
		if( m.HasMember( "normalTexture" ) )
		{
			GltfTextureBytes( src, m["normalTexture"], mat.normal );
		}
		if( m.HasMember( "occlusionTexture" ) )
		{
			GltfTextureBytes( src, m["occlusionTexture"], mat.occlusion );
		}
		if( m.HasMember( "emissiveTexture" ) )
		{
			GltfTextureBytes( src, m["emissiveTexture"], mat.emissiveMap );
		}
		if( m.HasMember( "emissiveFactor" ) && m["emissiveFactor"].IsArray() && m["emissiveFactor"].Size() == 3 )
		{
			for( int c = 0; c < 3; c++ )
			{
				mat.emissive[c] = m["emissiveFactor"][c].GetDouble();
			}
		}
		if( m.HasMember( "alphaMode" ) && m["alphaMode"].IsString() )
		{
			mat.alphaMask = idStr::Icmp( m["alphaMode"].GetString(), "MASK" ) == 0;
			mat.alphaBlend = idStr::Icmp( m["alphaMode"].GetString(), "BLEND" ) == 0;
		}
		mat.alphaCutoff = JsonFloat( m, "alphaCutoff", 0.5f );
		mat.twoSided = m.HasMember( "doubleSided" ) && m["doubleSided"].IsBool() && m["doubleSided"].GetBool();
	}
}

/*
-------------------------------------------------------------------------------
	OBJ

	OBJ files stand Y up (Blender's default export), the game stands Z up, so
	the copy is turned to match and scaled to game units. Its materials come
	from the .mtl file beside it.
-------------------------------------------------------------------------------
*/

static idStr RestOfLine( const char* line, int skip )
{
	idStr rest = line + skip;
	rest.StripLeading( ' ' );
	rest.StripLeading( '\t' );
	rest.StripTrailingWhitespace();
	return rest;
}

// a map line in an .mtl ends in the file name, after any options such as "-bm 1.0"
static idStr MtlMapFile( const idStr& rest )
{
	idStrList words;
	idStr word;
	for( int i = 0; i <= rest.Length(); i++ )
	{
		const char c = i < rest.Length() ? rest[i] : ' ';
		if( c == ' ' || c == '\t' )
		{
			if( word.Length() > 0 )
			{
				words.Append( word );
				word.Clear();
			}
		}
		else
		{
			word += c;
		}
	}
	// skip options and their values; what's left (the file name may hold spaces) is the file
	int i = 0;
	while( i < words.Num() && words[i][0] == '-' )
	{
		const idStr opt = words[i];
		int values = 1;
		if( opt == "-o" || opt == "-s" || opt == "-t" )
		{
			values = 3;
		}
		else if( opt == "-mm" )
		{
			values = 2;
		}
		i += 1 + values;
	}
	idStr file;
	for( ; i < words.Num(); i++ )
	{
		if( file.Length() > 0 )
		{
			file += ' ';
		}
		file += words[i];
	}
	return file;
}

static void ReadMtl( const idStr& mtlPath, idList<material_t>& mats, idStrList& warnings )
{
	idList<byte> bytes;
	if( !ReadOSFile( mtlPath, bytes ) )
	{
		warnings.Append( "The .mtl file with the model's materials is missing, so it's plain white." );
		return;
	}
	bytes.Append( 0 );
	const idStr folder = OSFolderOf( mtlPath );
	material_t* mat = NULL;
	float specularExponent = -1.0f;

	auto loadMap = [&]( const idStr & rest, imageSource_t & dest )
	{
		const idStr file = MtlMapFile( rest );
		dest.file = file;
		if( !ReadOSFile( ResolveRelative( folder, file ), dest.bytes ) )
		{
			warnings.Append( va( "The picture \"%s\" isn't next to the model.", file.c_str() ) );
		}
	};
	auto finish = [&]()
	{
		if( mat != NULL && specularExponent >= 0.0f && !mat->roughOnly.Valid() && mat->rough == 1.0f )
		{
			// Blender writes roughness r as Ns = (1 - r)^2 * 1000
			mat->rough = 1.0f - idMath::Sqrt( idMath::ClampFloat( 0.0f, 1000.0f, specularExponent ) / 1000.0f );
		}
		specularExponent = -1.0f;
	};

	idStr text = ( const char* )bytes.Ptr();
	int start = 0;
	while( start < text.Length() )
	{
		int end = text.Find( '\n', start );
		if( end < 0 )
		{
			end = text.Length();
		}
		idStr line( text, start, end );
		start = end + 1;
		line.StripTrailingWhitespace();
		line.StripLeading( ' ' );
		line.StripLeading( '\t' );
		if( line.IsEmpty() || line[0] == '#' )
		{
			continue;
		}
		idStr key = line;
		int space = line.Find( ' ' );
		const int tab = line.Find( '\t' );
		if( space < 0 || ( tab >= 0 && tab < space ) )
		{
			space = tab;
		}
		key.CapLength( space >= 0 ? space : line.Length() );
		const idStr rest = RestOfLine( line, key.Length() );

		if( key.Icmp( "newmtl" ) == 0 )
		{
			finish();
			mat = &mats.Alloc();
			mat->name = rest;
			mat->metal = 0.0f;		// OBJ has no metal unless it says so
			mat->rough = 1.0f;
			continue;
		}
		if( mat == NULL )
		{
			continue;
		}
		idLexer src( rest.c_str(), rest.Length(), "mtl", LEXFL_NOERRORS | LEXFL_NOWARNINGS );
		if( key.Icmp( "Kd" ) == 0 )
		{
			mat->baseColor.x = src.ParseFloat();
			mat->baseColor.y = src.ParseFloat();
			mat->baseColor.z = src.ParseFloat();
		}
		else if( key.Icmp( "d" ) == 0 )
		{
			mat->baseColor.w = src.ParseFloat();
		}
		else if( key.Icmp( "Tr" ) == 0 )
		{
			mat->baseColor.w = 1.0f - src.ParseFloat();
		}
		else if( key.Icmp( "Ns" ) == 0 )
		{
			specularExponent = src.ParseFloat();
		}
		else if( key.Icmp( "Pr" ) == 0 )
		{
			mat->rough = src.ParseFloat();
			specularExponent = -1.0f;
		}
		else if( key.Icmp( "Pm" ) == 0 )
		{
			mat->metal = src.ParseFloat();
		}
		else if( key.Icmp( "Ke" ) == 0 )
		{
			mat->emissive.x = src.ParseFloat();
			mat->emissive.y = src.ParseFloat();
			mat->emissive.z = src.ParseFloat();
		}
		else if( key.Icmp( "map_Kd" ) == 0 )
		{
			loadMap( rest, mat->base );
		}
		else if( key.Icmp( "map_Bump" ) == 0 || key.Icmp( "bump" ) == 0 || key.Icmp( "norm" ) == 0 )
		{
			loadMap( rest, mat->normal );
		}
		else if( key.Icmp( "map_Pr" ) == 0 )
		{
			loadMap( rest, mat->roughOnly );
			mat->rough = 1.0f;
			specularExponent = -1.0f;
		}
		else if( key.Icmp( "map_Pm" ) == 0 )
		{
			loadMap( rest, mat->metalOnly );
			mat->metal = 1.0f;
		}
		else if( key.Icmp( "map_Ke" ) == 0 )
		{
			loadMap( rest, mat->emissiveMap );
		}
		else if( key.Icmp( "map_d" ) == 0 )
		{
			mat->alphaMask = true;
		}
	}
	finish();
	for( int i = 0; i < mats.Num(); i++ )
	{
		if( mats[i].baseColor.w < 0.99f )
		{
			mats[i].alphaBlend = true;
		}
	}
}

// one corner of an OBJ face: v, vt and vn indices (0-based; -1 when missing)
struct objCorner_t
{
	int v = -1, vt = -1, vn = -1;
};

// "12", "12/3", "12//7", "12/3/7"; negative numbers count back from the end
static bool ParseObjCorner( const idStr& word, int numV, int numVt, int numVn, objCorner_t& c )
{
	int values[3] = { 0, 0, 0 };
	bool have[3] = { false, false, false };
	int part = 0;
	idStr digits;
	for( int i = 0; i <= word.Length(); i++ )
	{
		const char ch = i < word.Length() ? word[i] : '/';
		if( ch == '/' )
		{
			if( digits.Length() > 0 && part < 3 )
			{
				values[part] = atoi( digits );
				have[part] = true;
			}
			digits.Clear();
			part++;
		}
		else
		{
			digits += ch;
		}
	}
	auto resolve = []( int value, int count )
	{
		return value > 0 ? value - 1 : count + value;
	};
	if( !have[0] )
	{
		return false;
	}
	c.v = resolve( values[0], numV );
	c.vt = have[1] ? resolve( values[1], numVt ) : -1;
	c.vn = have[2] ? resolve( values[2], numVn ) : -1;
	return c.v >= 0 && c.v < numV && c.vt < numVt && c.vn < numVn;
}

/*
The engine's OBJ reader takes only triangles written v/vt/vn and stops at
anything else, so the copy is written in exactly that form: faces are split
into triangles, a missing texture coordinate points at a spare (0, 0), and a
missing normal gets the face's own. Each material is its own object, because
the reader gives an object the material in use when the object ends.
*/
static bool ConvertObj( const char* osPath, const model_t& m, float scale, idStr& out, idList<material_t>& mats, int& meshes, idStrList& warnings, idStr& error )
{
	idList<byte> bytes;
	if( !ReadOSFile( osPath, bytes ) || bytes.Num() == 0 )
	{
		error = "The file couldn't be read.";
		return false;
	}
	bytes.Append( 0 );
	const idStr text = ( const char* )bytes.Ptr();
	const idStr folder = OSFolderOf( osPath );

	idStr prefix = m.model;
	prefix.StripFileExtension();

	// Y up to Z up (undoing what Blender's OBJ export does), then the extra turn
	const idMat3 turn = idAngles( 0.0f, m.turn, 0.0f ).ToMat3();
	idList<idVec3> verts, normals;
	idList<idVec2> texCoords;
	struct objFace_t
	{
		int material;
		objCorner_t corners[3];
	};
	idList<objFace_t> faces;
	idStrList usedMaterials;
	int current = -1;
	int skipped = 0;
	idStrList groups;

	int start = 0;
	while( start < text.Length() )
	{
		int end = text.Find( '\n', start );
		if( end < 0 )
		{
			end = text.Length();
		}
		idStr line( text, start, end );
		start = end + 1;
		line.StripTrailingWhitespace();
		line.StripLeading( ' ' );
		line.StripLeading( '\t' );

		if( line.Cmpn( "v ", 2 ) == 0 || line.Cmpn( "vn ", 3 ) == 0 || line.Cmpn( "vt ", 3 ) == 0 )
		{
			const int skip = line[1] == ' ' ? 2 : 3;
			idLexer src( line.c_str() + skip, line.Length() - skip, "obj", LEXFL_NOERRORS | LEXFL_NOWARNINGS );
			const float x = src.ParseFloat();
			const float y = src.ParseFloat();
			if( line[1] == 't' )
			{
				texCoords.Append( idVec2( x, y ) );
				continue;
			}
			const float z = src.ParseFloat();
			idVec3 v( x, -z, y );
			v = v * turn;
			if( line[1] == 'n' )
			{
				v.Normalize();
				normals.Append( v );
			}
			else
			{
				verts.Append( v * scale );
			}
		}
		else if( line.Cmpn( "usemtl", 6 ) == 0 )
		{
			const idStr name = RestOfLine( line, 6 );
			current = usedMaterials.FindIndex( name );
			if( current < 0 )
			{
				current = usedMaterials.Append( name );
			}
		}
		else if( line.Cmpn( "mtllib", 6 ) == 0 )
		{
			ReadMtl( ResolveRelative( folder, RestOfLine( line, 6 ) ), mats, warnings );
		}
		else if( line.Cmpn( "o ", 2 ) == 0 || line.Cmpn( "g ", 2 ) == 0 )
		{
			const idStr name = RestOfLine( line, 2 );
			if( groups.FindIndex( name ) < 0 )
			{
				groups.Append( name );
			}
		}
		else if( line.Cmpn( "f ", 2 ) == 0 )
		{
			idStrList words;
			idStr word;
			const idStr rest = RestOfLine( line, 2 );
			for( int i = 0; i <= rest.Length(); i++ )
			{
				const char c = i < rest.Length() ? rest[i] : ' ';
				if( c == ' ' || c == '\t' )
				{
					if( word.Length() > 0 )
					{
						words.Append( word );
						word.Clear();
					}
				}
				else
				{
					word += c;
				}
			}
			idList<objCorner_t> corners;
			bool ok = words.Num() >= 3;
			for( int i = 0; i < words.Num() && ok; i++ )
			{
				objCorner_t c;
				ok = ParseObjCorner( words[i], verts.Num(), texCoords.Num(), normals.Num(), c );
				corners.Append( c );
			}
			if( !ok )
			{
				skipped++;
				continue;
			}
			if( current < 0 )
			{
				current = usedMaterials.Append( "default" );
			}
			// a fan of triangles; corners without a normal get the face's
			for( int i = 1; i + 1 < corners.Num(); i++ )
			{
				objFace_t& f = faces.Alloc();
				f.material = current;
				f.corners[0] = corners[0];
				f.corners[1] = corners[i];
				f.corners[2] = corners[i + 1];
				const idVec3& a = verts[f.corners[0].v];
				idVec3 n = ( verts[f.corners[1].v] - a ).Cross( verts[f.corners[2].v] - a );
				const bool needNormal = f.corners[0].vn < 0 || f.corners[1].vn < 0 || f.corners[2].vn < 0;
				if( needNormal )
				{
					if( n.Normalize() == 0.0f )
					{
						n.Set( 0, 0, 1 );
					}
					const int index = normals.Append( n );
					for( int k = 0; k < 3; k++ )
					{
						if( f.corners[k].vn < 0 )
						{
							f.corners[k].vn = index;
						}
					}
				}
			}
		}
		// everything else (comments, s, l, ...) is left out: the reader wouldn't take it
	}
	if( faces.Num() == 0 )
	{
		error = "The file has no faces in it.";
		return false;
	}
	if( skipped > 0 )
	{
		warnings.Append( va( "%d faces in the file couldn't be read and were left out.", skipped ) );
	}
	meshes = Max( groups.Num(), 1 );

	const int spareTexCoord = texCoords.Append( idVec2( 0, 0 ) );
	out = va( "# %s, brought in by the GrizzTech4 editor: standing Z up, in game units, as triangles\n", m.name.c_str() );
	for( int i = 0; i < verts.Num(); i++ )
	{
		out += va( "v %.6f %.6f %.6f\n", verts[i].x, verts[i].y, verts[i].z );
	}
	for( int i = 0; i < texCoords.Num(); i++ )
	{
		out += va( "vt %.6f %.6f\n", texCoords[i].x, texCoords[i].y );
	}
	for( int i = 0; i < normals.Num(); i++ )
	{
		out += va( "vn %.6f %.6f %.6f\n", normals[i].x, normals[i].y, normals[i].z );
	}
	for( int mat = 0; mat < usedMaterials.Num(); mat++ )
	{
		out += va( "o part_%d\nusemtl %s/%s\n", mat, prefix.c_str(), MaterialPart( usedMaterials[mat] ).c_str() );
		for( int i = 0; i < faces.Num(); i++ )
		{
			if( faces[i].material != mat )
			{
				continue;
			}
			out += "f";
			for( int k = 0; k < 3; k++ )
			{
				const objCorner_t& c = faces[i].corners[k];
				out += va( " %d/%d/%d", c.v + 1, ( c.vt >= 0 ? c.vt : spareTexCoord ) + 1, c.vn + 1 );
			}
			out += "\n";
		}
	}

	// materials the faces use that the .mtl didn't describe still get a plain one
	for( int i = 0; i < usedMaterials.Num(); i++ )
	{
		bool have = false;
		for( int j = 0; j < mats.Num() && !have; j++ )
		{
			have = MaterialPart( mats[j].name ) == MaterialPart( usedMaterials[i] );
		}
		if( !have )
		{
			material_t& mat = mats.Alloc();
			mat.name = usedMaterials[i];
			mat.metal = 0.0f;
		}
	}
	return true;
}

/*
-------------------------------------------------------------------------------
	the settings file
-------------------------------------------------------------------------------
*/

static idStr Quote( const idStr& s )
{
	idStr q = s;
	q.Replace( "\"", "'" );
	return idStr( "\"" ) + q + "\"";
}

static bool WriteSettings( const model_t& m, const idStrList& cache )
{
	const float scale = MadeInScale( m.madeIn, m.customScale );
	idStr text;
	text += "// GrizzTech4 import settings, written by the editor. Change them on the model's card in the Inspector.\n";
	text += va( "source %s\n", Quote( m.source ).c_str() );
	text += va( "type %s\n", m.sourceType.c_str() );
	text += va( "madeIn %s\n", MadeInKey( m.madeIn ) );
	text += va( "customScale %g\n", m.customScale );
	text += va( "turn %g\n", m.turn );
	text += va( "materials %d\n", m.materials );
	text += va( "textures %d\n", m.textures );
	text += va( "meshes %d\n", m.meshes );
	text += va( "skinned %d\n", m.skinned ? 1 : 0 );
	if( m.sourceType != "obj" )
	{
		// read by the engine when it loads the model (see Model_gltf.cpp)
		text += va( "options - scale %g - rotate %g\n", scale, m.turn );	// the option reader wants "-" and the name apart
	}
	for( int i = 0; i < cache.Num(); i++ )
	{
		text += va( "cache %s\n", cache[i].c_str() );
	}
	for( int i = 0; i < m.warnings.Num(); i++ )
	{
		text += va( "warning %s\n", Quote( m.warnings[i] ).c_str() );
	}
	const idStr path = SettingsPath( m.name );
	return fileSystem->WriteFile( path, text.c_str(), text.Length(), "fs_basepath" ) == text.Length();
}

static bool ReadSettings( const char* name, model_t& m, idStrList* cache )
{
	char* text = NULL;
	const idStr path = SettingsPath( name );
	if( fileSystem->ReadFile( path, ( void** )&text ) <= 0 || text == NULL )
	{
		return false;
	}
	m = model_t();
	m.name = name;
	idLexer src( text, idStr::Length( text ), path, LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWPATHNAMES | LEXFL_NOFATALERRORS | LEXFL_NOSTRINGESCAPECHARS );
	idToken token, value;
	while( src.ReadToken( &token ) )
	{
		if( token == "source" && src.ReadToken( &value ) )
		{
			m.source = value;
		}
		else if( token == "type" && src.ReadToken( &value ) )
		{
			m.sourceType = value;
		}
		else if( token == "madeIn" && src.ReadToken( &value ) )
		{
			m.madeIn = MadeInFromKey( value );
		}
		else if( token == "customScale" )
		{
			m.customScale = src.ParseFloat();
		}
		else if( token == "turn" )
		{
			m.turn = src.ParseFloat();
		}
		else if( token == "materials" )
		{
			m.materials = src.ParseInt();
		}
		else if( token == "textures" )
		{
			m.textures = src.ParseInt();
		}
		else if( token == "meshes" )
		{
			m.meshes = src.ParseInt();
		}
		else if( token == "skinned" )
		{
			m.skinned = src.ParseInt() != 0;
		}
		else if( token == "cache" )
		{
			idStr line;
			src.ReadRestOfLine( line );
			line.StripLeading( ' ' );
			line.StripTrailingWhitespace();
			if( cache != NULL )
			{
				cache->Append( line );
			}
		}
		else if( token == "warning" && src.ReadToken( &value ) )
		{
			m.warnings.Append( value );
		}
		else
		{
			src.SkipRestOfLine();
		}
	}
	fileSystem->FreeFile( text );
	m.model = va( "%s/%s/%s.%s", IMPORTED_DIR, name, name, m.sourceType == "obj" ? "obj" : "glb" );
	StatOSFile( SourceOSPath( m ), m.sourceTime, m.sourceLength );
	return true;
}

/*
-------------------------------------------------------------------------------
	importing
-------------------------------------------------------------------------------
*/

static model_t* FindEditable( const char* name )
{
	for( int i = 0; i < models.Num(); i++ )
	{
		if( models[i].name.Icmp( name ) == 0 )
		{
			return &models[i];
		}
	}
	return NULL;
}

static idStr UniqueName( const char* base )
{
	idStr name = CleanName( base );
	if( name.IsEmpty() )
	{
		name = "model";
	}
	if( name[0] >= '0' && name[0] <= '9' )
	{
		name = "model_" + name;
	}
	idStr tried = name;
	for( int n = 2; FindEditable( tried ) != NULL; n++ )
	{
		tried = va( "%s_%d", name.c_str(), n );
	}
	return tried;
}

// load a newly written file, or one that changed, so the editor and a paused game see it now
static idCVar editor_importReload( "editor_importReload", "7", CVAR_INTEGER, "what reloads after a model is brought in again: 1 materials, 2 pictures, 4 the model" );

static void ReloadInEngine( const model_t& m, bool materialsChanged, const idStrList& images, bool modelChanged )
{
	const int what = editor_importReload.GetInteger();
	if( ( what & 1 ) && materialsChanged )
	{
		declManager->ReloadFile( MaterialFile( m.name ), true );
	}
	if( ( what & 2 ) && images.Num() > 0 )
	{
		idStr cmd = "reloadImage";
		for( int i = 0; i < images.Num(); i++ )
		{
			cmd += va( " \"%s\"", images[i].c_str() );
		}
		cmdSystem->BufferCommandText( CMD_EXEC_INSERT, cmd + "\n" );
	}
	if( modelChanged && ( what & 4 ) )
	{
		cmdSystem->BufferCommandText( CMD_EXEC_INSERT, "reloadModels\n" );
	}
}

static bool DoImport( model_t& m, idStr& message, bool isNew )
{
	const idStr osPath = SourceOSPath( m );
	idStr error;
	idStrList warnings;
	idList<material_t> mats;
	idStrList cacheIn, cacheOut;
	model_t previous;
	const bool hadSettings = ReadSettings( m.name, previous, &cacheIn );
	// the size and turn are applied when the model loads, so a change to them reloads it
	const bool settingsChanged = !hadSettings || previous.madeIn != m.madeIn || previous.customScale != m.customScale || previous.turn != m.turn;

	idList<byte> modelBytes;
	if( m.sourceType == "obj" )
	{
		idStr obj;
		int meshes = 0;
		if( !ConvertObj( osPath, m, MadeInScale( m.madeIn, m.customScale ), obj, mats, meshes, warnings, error ) )
		{
			message = va( "Couldn't bring in %s: %s", m.name.c_str(), error.c_str() );
			return false;
		}
		m.meshes = meshes;
		m.skinned = false;
		modelBytes.SetNum( obj.Length() );
		memcpy( modelBytes.Ptr(), obj.c_str(), obj.Length() );
	}
	else
	{
		gltfSource_t src;
		if( !ReadGltf( osPath, src, error ) )
		{
			message = va( "Couldn't bring in %s: %s", m.name.c_str(), error.c_str() );
			return false;
		}
		m.meshes = ( int )src.doc["meshes"].Size();
		m.skinned = src.doc.HasMember( "skins" ) && src.doc["skins"].IsArray() && src.doc["skins"].Size() > 0;
		if( m.skinned )
		{
			warnings.Append( "It has a skeleton. It's placed standing still for now; animated characters come with the gameplay tools." );
		}
		GltfMaterials( src, mats, warnings );
		WriteGlb( src, modelBytes );
	}

	m.materials = mats.Num();
	m.textures = 0;
	for( int i = 0; i < mats.Num(); i++ )
	{
		m.textures += ( mats[i].base.Valid() ? 1 : 0 ) + ( mats[i].normal.Valid() ? 1 : 0 ) + ( mats[i].emissiveMap.Valid() ? 1 : 0 );
	}

	// materials first, so they're there when the model is loaded
	idStrList writtenImages;
	bool materialsChanged = true;
	if( !WriteMaterials( m, mats, cacheIn, cacheOut, warnings, writtenImages, materialsChanged ) )
	{
		message = va( "Couldn't write the materials for %s.", m.name.c_str() );
		return false;
	}
	m.warnings = warnings;
	if( !WriteSettings( m, cacheOut ) )
	{
		message = va( "Couldn't write the settings for %s.", m.name.c_str() );
		return false;
	}
	bool modelChanged = true;
	{
		// an unchanged model isn't written (or reloaded) again
		void* old = NULL;
		const int oldLength = fileSystem->ReadFile( m.model, &old );
		modelChanged = settingsChanged || oldLength != modelBytes.Num() || old == NULL || memcmp( old, modelBytes.Ptr(), modelBytes.Num() ) != 0;
		if( old != NULL )
		{
			fileSystem->FreeFile( old );
		}
	}
	if( modelChanged && fileSystem->WriteFile( m.model, modelBytes.Ptr(), modelBytes.Num(), "fs_basepath" ) != modelBytes.Num() )
	{
		message = va( "Couldn't write %s.", m.model.c_str() );
		return false;
	}
	StatOSFile( osPath, m.sourceTime, m.sourceLength );
	m.sourceMissing = false;
	m.pendingLength = -1;

	if( !isNew )
	{
		ReloadInEngine( m, materialsChanged, writtenImages, modelChanged );
	}
	else
	{
		declManager->ReloadFile( MaterialFile( m.name ), true );
	}
	idStr notes;
	if( m.warnings.Num() > 0 )
	{
		notes = va( ", %d note%s in Problems", m.warnings.Num(), m.warnings.Num() == 1 ? "" : "s" );
	}
	message = va( "%s %s (%d material%s%s)", isNew ? "Brought in" : "Updated", m.name.c_str(), m.materials, m.materials == 1 ? "" : "s", notes.c_str() );
	Activity( message );
	return true;
}

bool ImportFile( const char* sourcePath, idStr& name, idStr& message )
{
	Refresh();
	if( !IsModelFile( sourcePath ) )
	{
		message = "Only .glb, .gltf and .obj models can be brought in.";
		return false;
	}
	idStr osPath = sourcePath;
	osPath.BackSlashesToSlashes();
	const idStr stored = StoredSourcePath( osPath );

	// the same file again is an update
	for( int i = 0; i < models.Num(); i++ )
	{
		if( models[i].source.Icmp( stored ) == 0 )
		{
			name = models[i].name;
			return DoImport( models[i], message, false );
		}
	}

	idStr base = osPath;
	base.StripPath();
	base.StripFileExtension();
	idStr ext;
	osPath.ExtractFileExtension( ext );
	ext.ToLower();

	model_t m;
	m.name = UniqueName( base );
	m.source = stored;
	m.sourceType = ext == "obj" ? "obj" : ( ext == "gltf" ? "gltf" : "glb" );
	m.model = va( "%s/%s/%s.%s", IMPORTED_DIR, m.name.c_str(), m.name.c_str(), m.sourceType == "obj" ? "obj" : "glb" );
	if( !DoImport( m, message, true ) )
	{
		return false;
	}
	models.Append( m );
	name = m.name;
	return true;
}

bool Reimport( const char* name, idStr& message )
{
	Refresh();
	model_t* m = FindEditable( name );
	if( m == NULL )
	{
		message = va( "There's no imported model called %s.", name );
		return false;
	}
	return DoImport( *m, message, false );
}

bool SetMadeIn( const char* name, madeIn_t madeIn, float customScale, idStr& message )
{
	Refresh();
	model_t* m = FindEditable( name );
	if( m == NULL )
	{
		message = va( "There's no imported model called %s.", name );
		return false;
	}
	m->madeIn = madeIn;
	if( customScale > 0.0f )
	{
		m->customScale = customScale;
	}
	return DoImport( *m, message, false );
}

bool SetTurn( const char* name, float degrees, idStr& message )
{
	Refresh();
	model_t* m = FindEditable( name );
	if( m == NULL )
	{
		message = va( "There's no imported model called %s.", name );
		return false;
	}
	m->turn = idMath::AngleNormalize360( degrees );
	return DoImport( *m, message, false );
}

void Refresh()
{
	if( modelsRead )
	{
		return;
	}
	modelsRead = true;
	models.Clear();
	idStrList folders;
	const idStr root = fileSystem->RelativePathToOSPath( IMPORTED_DIR, "fs_basepath" );
	Sys_ListFiles( root, "/", folders );
	folders.SortWithTemplate( idSort_PathStr() );
	for( int i = 0; i < folders.Num(); i++ )
	{
		if( folders[i] == "." || folders[i] == ".." )
		{
			continue;
		}
		model_t m;
		if( ReadSettings( folders[i], m, NULL ) )
		{
			models.Append( m );
		}
	}
}

const idList<model_t>& Models()
{
	Refresh();
	return models;
}

const model_t* FindByName( const char* name )
{
	Refresh();
	return FindEditable( name );
}

const model_t* FindByModel( const char* modelPath )
{
	Refresh();
	idStr path = modelPath;
	path.BackSlashesToSlashes();
	for( int i = 0; i < models.Num(); i++ )
	{
		if( models[i].model.Icmp( path ) == 0 )
		{
			return &models[i];
		}
	}
	return NULL;
}

idStr ImportFolder()
{
	idStr path = fileSystem->RelativePathToOSPath( IMPORT_FOLDER, "fs_basepath" );
	path.BackSlashesToSlashes();
	if( Sys_IsFolder( path ) != FOLDER_YES )
	{
		Sys_Mkdir( path );
	}
	return path;
}

/*
-------------------------------------------------------------------------------
	watching
-------------------------------------------------------------------------------
*/

static bool WatchImportFolder( idStr& message )
{
	const idStr folder = ImportFolder();
	idStrList files;
	Sys_ListFiles( folder, "", files );
	for( int i = 0; i < files.Num(); i++ )
	{
		if( !IsModelFile( files[i] ) )
		{
			continue;
		}
		const idStr osPath = folder + "/" + files[i];
		const idStr stored = StoredSourcePath( osPath );
		bool known = false;
		for( int j = 0; j < models.Num() && !known; j++ )
		{
			known = models[j].source.Icmp( stored ) == 0;
		}
		if( known )
		{
			continue;		// changes to it are picked up like any other source
		}

		// a new file: wait until it stops growing (it may still be being saved)
		ID_TIME_T time = 0;
		int length = 0;
		if( !StatOSFile( osPath, time, length ) )
		{
			continue;
		}
		const idStr seen = va( "%s|%d|%d", files[i].c_str(), ( int )time, length );
		if( importFolderKnown.FindIndex( seen ) < 0 )
		{
			// first sighting at this size; remember it, and try again next time round
			for( int j = importFolderKnown.Num() - 1; j >= 0; j-- )
			{
				if( importFolderKnown[j].Cmpn( files[i] + "|", files[i].Length() + 1 ) == 0 )
				{
					importFolderKnown.RemoveIndex( j );
				}
			}
			importFolderKnown.Append( seen );
			continue;
		}
		idStr name;
		ImportFile( osPath, name, message );
		return true;
	}
	return false;
}

bool Poll()
{
	const int now = Sys_Milliseconds();
	if( now - lastPoll < POLL_MS )
	{
		return false;
	}
	lastPoll = now;
	Refresh();

	idStr message;
	for( int i = 0; i < models.Num(); i++ )
	{
		model_t& m = models[i];
		ID_TIME_T time = 0;
		int length = 0;
		if( !StatOSFile( SourceOSPath( m ), time, length ) )
		{
			m.sourceMissing = true;
			continue;
		}
		m.sourceMissing = false;
		if( time == m.sourceTime && length == m.sourceLength )
		{
			m.pendingLength = -1;
			continue;
		}
		// changed: wait until it has stayed the same for a moment (the tool may still be writing it)
		if( time != m.pendingTime || length != m.pendingLength )
		{
			m.pendingTime = time;
			m.pendingLength = length;
			m.pendingSince = now;
			Activity( va( "%s changed, updating...", m.name.c_str() ) );
			continue;
		}
		if( now - m.pendingSince < SETTLE_MS )
		{
			continue;
		}
		if( !DoImport( m, message, false ) )
		{
			// a half-written or broken file: remember this version so it isn't tried over and over
			m.sourceTime = time;
			m.sourceLength = length;
			m.pendingLength = -1;
			Activity( message );
		}
		return true;
	}
	return WatchImportFolder( message );
}

/*
-------------------------------------------------------------------------------
	dropped files and the system's file windows
-------------------------------------------------------------------------------
*/

void QueueDroppedFile( const char* osPath )
{
	idScopedCriticalSection lock( dropMutex );
	dropped.Append( osPath );
}

bool TakeDroppedFile( idStr& osPath )
{
	idScopedCriticalSection lock( dropMutex );
	if( dropped.Num() == 0 )
	{
		return false;
	}
	osPath = dropped[0];
	dropped.RemoveIndex( 0 );
	return true;
}

void ShowFolder( const char* osPath )
{
	Sys_OpenFolder( osPath );
}

bool BrowseForModel( idStr& osPath )
{
	char path[MAX_OSPATH * 4];
	if( !Sys_OpenFileDialog( "Bring in a model", "Models (.glb .gltf .obj)|*.glb;*.gltf;*.obj", path, sizeof( path ) ) )
	{
		return false;
	}
	osPath = path;
	return true;
}

} // namespace Import
} // namespace GrizzEditor
