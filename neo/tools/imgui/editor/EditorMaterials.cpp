/*
===========================================================================

Daedalus editor: making and changing materials

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

#if defined( DAEDALUS_EDITOR )

#include "EditorMaterials.h"
#include "renderer/RenderCommon.h"

#include "../../../libs/stb/stb_image.h"
#include "../../../libs/stb/stb_image_write.h"

namespace DaedalusEditor
{
namespace Materials
{

static const char* SETTINGS_MARK = "daedalus-material";

/*
-------------------------------------------------------------------------------
	presets, slots, surfaces
-------------------------------------------------------------------------------
*/

static const preset_t presets[] =
{
	{ "Stone",			"Rock, concrete, plaster, tiles",			0.85f, 0.0f, 0.0f, SEE_NONE,	1.0f,  false, "stone" },
	{ "Brick",			"Brick and blockwork",						0.80f, 0.0f, 0.0f, SEE_NONE,	1.0f,  false, "stone" },
	{ "Wood",			"Planks, beams, furniture",					0.70f, 0.0f, 0.0f, SEE_NONE,	1.0f,  false, "wood" },
	{ "Painted metal",	"Metal under paint or a coating",			0.45f, 0.0f, 0.0f, SEE_NONE,	1.0f,  false, "metal" },
	{ "Bare metal",		"Steel, iron, brass: shiny and reflective",	0.30f, 1.0f, 0.0f, SEE_NONE,	1.0f,  false, "metal" },
	{ "Plastic",		"Plastic, rubber, smooth paint",			0.40f, 0.0f, 0.0f, SEE_NONE,	1.0f,  false, "plastic" },
	{ "Fabric",			"Cloth, carpet, upholstery",				0.95f, 0.0f, 0.0f, SEE_NONE,	1.0f,  false, "cardboard" },
	{ "Ground",			"Dirt, grass, sand, mud",					0.95f, 0.0f, 0.0f, SEE_NONE,	1.0f,  false, "stone" },
	{ "Foliage",		"Leaves and plants, cut out from the picture",	0.80f, 0.0f, 0.0f, SEE_CUTOUT,	1.0f,  true,  "wood" },
	{ "Glass",			"Windows and screens you can see through",	0.05f, 0.0f, 0.0f, SEE_THROUGH,	0.25f, true,  "glass" },
	{ "Water",			"Still water you can see into",				0.05f, 0.0f, 0.0f, SEE_THROUGH,	0.60f, false, "liquid" },
	{ "Glowing",		"Lamps, signs and screens that give off light", 0.50f, 0.0f, 1.0f, SEE_NONE,	1.0f,  false, "none" },
};

const preset_t* Presets( int& count )
{
	count = sizeof( presets ) / sizeof( presets[0] );
	return presets;
}

void ApplyPreset( settings_t& s, const char* name )
{
	s.preset = name;
	for( const preset_t& p : presets )
	{
		if( idStr::Icmp( p.name, name ) == 0 )
		{
			s.roughness = p.roughness;
			s.metal = p.metal;
			s.glow = p.glow;
			s.transparency = p.transparency;
			s.opacity = p.opacity;
			s.twoSided = p.twoSided;
			s.surface = p.surface;
			s.solid = p.transparency != SEE_THROUGH || idStr::Icmp( p.surface, "liquid" ) != 0;
			return;
		}
	}
	s.preset = "Custom";
}

static const char* slotLabels[SLOT_COUNT] = { "Colour", "Normal", "Roughness", "Metal", "Occlusion", "Glow" };
static const char* slotKeys[SLOT_COUNT] = { "color", "normal", "rough", "metal", "ao", "glow" };
static const char* slotHints[SLOT_COUNT] =
{
	"The colour of the surface (base colour, albedo, diffuse). Without one, the tint colour is used.",
	"Fine bumps and grooves, as a normal map (the blue-purple picture).",
	"How rough each part is: white is rough and matte, black is smooth and shiny. Without one, the Roughness slider is used.",
	"Which parts are bare metal: white is metal, black isn't. Without one, the Metal slider is used.",
	"Ambient occlusion: dark where light can't reach, like cracks and corners.",
	"Which parts glow, and in what colour. Without one, the whole colour picture glows by the Glow amount."
};

const char* SlotLabel( slot_t slot )
{
	return slotLabels[slot];
}

const char* SlotKey( slot_t slot )
{
	return slotKeys[slot];
}

const char* SlotHint( slot_t slot )
{
	return slotHints[slot];
}

static const char* surfaceTypes[] = { "none", "stone", "metal", "wood", "glass", "plastic", "cardboard", "liquid", "flesh", "ricochet" };

const char** SurfaceTypes( int& count )
{
	count = sizeof( surfaceTypes ) / sizeof( surfaceTypes[0] );
	return surfaceTypes;
}

/*
-------------------------------------------------------------------------------
	names
-------------------------------------------------------------------------------
*/

bool IsPictureFile( const char* path )
{
	idStr ext;
	idStr( path ).ExtractFileExtension( ext );
	return ext.Icmp( "png" ) == 0 || ext.Icmp( "jpg" ) == 0 || ext.Icmp( "jpeg" ) == 0 || ext.Icmp( "tga" ) == 0 || ext.Icmp( "bmp" ) == 0;
}

// the words in a file name, lower case: "Wall_Bricks-Normal.png" -> wall bricks normal
static void Words( const char* fileName, idStrList& words )
{
	idStr name = fileName;
	name.BackSlashesToSlashes();
	name.StripPath();
	name.StripFileExtension();
	words.Clear();
	idStr word;
	for( int i = 0; i <= name.Length(); i++ )
	{
		const char c = name[i];
		// camelCase splits too: BrickWallNormal -> brick wall normal
		const bool upperAfterLower = i > 0 && c >= 'A' && c <= 'Z' && name[i - 1] >= 'a' && name[i - 1] <= 'z';
		if( c == '\0' || !idStr::CharIsAlpha( c ) && !idStr::CharIsNumeric( c ) || upperAfterLower )
		{
			if( word.Length() > 0 )
			{
				word.ToLower();
				words.Append( word );
				word.Clear();
			}
			if( c == '\0' || !upperAfterLower )
			{
				continue;
			}
		}
		word += c;
	}
}

struct slotWords_t
{
	int			slot;		// SLOT_COUNT: a picture we don't use (height, opacity ...)
	const char*	words;
};

// common names, from asset stores and Blender's Node Wrangler
static const slotWords_t slotWords[] =
{
	{ SLOT_NORMAL,	" normal normals nor nrm nrml norm normalgl normaldx nmap n " },
	{ SLOT_ROUGH,	" roughness rough rgh " },
	{ SLOT_METAL,	" metallic metalness metal mtl " },
	{ SLOT_AO,		" ao ambientocclusion occlusion ambient occ " },
	{ SLOT_GLOW,	" emission emissive emit glow " },
	{ SLOT_COLOR,	" color colour albedo diffuse diff basecolor base col d " },
	{ SLOT_COUNT,	" height displacement displace disp dsp bump bmp opacity alpha mask specular spec gloss glossiness arm orm " },
};

// the slot of one word, -1 for none
static int SlotOfWord( const idStr& word )
{
	const idStr padded = " " + word + " ";
	for( const slotWords_t& sw : slotWords )
	{
		if( strstr( sw.words, padded.c_str() ) != NULL )
		{
			return sw.slot;
		}
	}
	return -1;
}

// the slot named in a file name, and where in its words: from the end, since "metal_plate_normal" is a normal map
static int FindSlotWord( const idStrList& words, int& at )
{
	for( int i = words.Num() - 1; i >= 0; i-- )
	{
		// "base color" and "ambient occlusion" come as two words
		if( i > 0 && ( ( words[i - 1] == "base" && words[i] == "color" ) || ( words[i - 1] == "ambient" && words[i] == "occlusion" ) ) )
		{
			at = i - 1;
			return words[i] == "color" ? SLOT_COLOR : SLOT_AO;
		}
		const int slot = SlotOfWord( words[i] );
		// one-letter words only count at the end (brick_n.png), so "a_wall" isn't an albedo
		if( slot >= 0 && ( words[i].Length() > 1 || i == words.Num() - 1 ) && !( i == 0 && words.Num() > 1 && words[i].Length() <= 2 ) )
		{
			at = i;
			return slot;
		}
	}
	at = words.Num();
	return -1;
}

slot_t GuessSlot( const char* fileName, bool& known )
{
	idStrList words;
	Words( fileName, words );
	int at;
	const int slot = FindSlotWord( words, at );
	known = slot >= 0;
	return slot >= 0 ? ( slot_t )slot : SLOT_COLOR;
}

static idStr CleanName( const idStr& text )
{
	idStr out;
	for( int i = 0; i < text.Length(); i++ )
	{
		const char c = idStr::ToLower( text[i] );
		if( idStr::CharIsAlpha( c ) || idStr::CharIsNumeric( c ) )
		{
			out += c;
		}
		else if( out.Length() > 0 && out[out.Length() - 1] != '_' )
		{
			out += '_';
		}
	}
	out.StripTrailing( '_' );
	return out.Length() > 0 ? out : idStr( "material" );
}

idStr NameFromPicture( const char* fileName )
{
	idStrList words;
	Words( fileName, words );
	int at;
	FindSlotWord( words, at );
	// sizes and versions aren't part of the name: brick_wall_2k_normal -> brick_wall
	idStr name;
	for( int i = 0; i < at; i++ )
	{
		const idStr& w = words[i];
		const bool size = ( w.Length() == 2 && idStr::CharIsNumeric( w[0] ) && w[1] == 'k' ) || w == "1024" || w == "2048" || w == "4096" || w == "512";
		if( size )
		{
			continue;
		}
		if( name.Length() > 0 )
		{
			name += "_";
		}
		name += w;
	}
	if( name.Length() == 0 )
	{
		idStr stem = fileName;
		stem.StripPath();
		stem.StripFileExtension();
		name = stem;
	}
	return CleanName( name );
}

idStr DeclName( const char* name )
{
	return va( "textures/%s", name );
}

idStr FilePath( const char* name )
{
	return va( "materials/%s.mtr", name );
}

idStr PictureFolder( const char* name )
{
	return va( "textures/%s", name );
}

idStr UniqueName( const char* base )
{
	const idStr clean = CleanName( base );
	idStr name = clean;
	for( int n = 2; ; n++ )
	{
		if( declManager->FindType( DECL_MATERIAL, DeclName( name ), false ) == NULL && fileSystem->ReadFile( FilePath( name ), NULL, NULL ) <= 0 )
		{
			return name;
		}
		name = va( "%s_%d", clean.c_str(), n );
	}
}

/*
-------------------------------------------------------------------------------
	files
-------------------------------------------------------------------------------
*/

static bool ReadText( const char* relativePath, idStr& text )
{
	void* buffer = NULL;
	const int length = fileSystem->ReadFile( relativePath, &buffer );
	if( length < 0 || buffer == NULL )
	{
		text.Clear();
		return false;
	}
	text = idStr( ( const char* )buffer ).Left( length );
	fileSystem->FreeFile( buffer );
	return true;
}

static bool WriteText( const char* relativePath, const idStr& text )
{
	return fileSystem->WriteFile( relativePath, text.c_str(), text.Length(), "fs_basepath" ) == text.Length();
}

static idDict editableCache;		// decl name -> editor name, or "-" for not ours

idStr EditableName( const char* declName )
{
	const idKeyValue* kv = editableCache.FindKey( declName );
	if( kv != NULL )
	{
		return kv->GetValue() == "-" ? idStr() : kv->GetValue();
	}
	idStr result;
	if( idStr::Icmpn( declName, "textures/", 9 ) == 0 && strchr( declName + 9, '/' ) == NULL )
	{
		const idStr name = declName + 9;
		idStr text;
		if( ReadText( FilePath( name ), text ) && text.Find( SETTINGS_MARK ) >= 0 )
		{
			result = name;
		}
	}
	editableCache.Set( declName, result.Length() > 0 ? result : idStr( "-" ) );
	return result;
}

bool Load( const char* name, settings_t& s )
{
	s = settings_t();
	idStr text;
	if( !ReadText( FilePath( name ), text ) )
	{
		return false;
	}
	// the settings line, inside the material: "// daedalus-material preset ..."
	int mark = text.Find( va( "// %s ", SETTINGS_MARK ) );
	if( mark < 0 )
	{
		return false;
	}
	mark += 3;
	int end = text.Find( '\n', mark );
	if( end < 0 )
	{
		end = text.Length();
	}
	const idStr line = text.Mid( mark + idStr::Length( SETTINGS_MARK ), end - mark - idStr::Length( SETTINGS_MARK ) );
	idLexer src( line.c_str(), line.Length(), "material settings", LEXFL_NOERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWPATHNAMES );
	idToken key, value;
	while( src.ReadToken( &key ) )
	{
		if( !src.ReadToken( &value ) )
		{
			break;
		}
		if( key == "-" )
		{
			continue;
		}
		if( value == "-" )
		{
			// a negative number: the lexer gives the sign on its own
			idToken number;
			src.ReadToken( &number );
			value = "-" + number;
		}
		if( key.Icmp( "preset" ) == 0 )
		{
			s.preset = value;
		}
		else if( key.Icmp( "tint" ) == 0 )
		{
			sscanf( value.c_str(), "%f %f %f", &s.tint.x, &s.tint.y, &s.tint.z );
		}
		else if( key.Icmp( "roughness" ) == 0 )
		{
			s.roughness = atof( value );
		}
		else if( key.Icmp( "metalness" ) == 0 )
		{
			s.metal = atof( value );
		}
		else if( key.Icmp( "glow" ) == 0 )
		{
			s.glow = atof( value );
		}
		else if( key.Icmp( "transparency" ) == 0 )
		{
			s.transparency = idMath::ClampInt( SEE_NONE, SEE_THROUGH, atoi( value ) );
		}
		else if( key.Icmp( "opacity" ) == 0 )
		{
			s.opacity = atof( value );
		}
		else if( key.Icmp( "twosided" ) == 0 )
		{
			s.twoSided = atoi( value ) != 0;
		}
		else if( key.Icmp( "shadows" ) == 0 )
		{
			s.shadows = atoi( value ) != 0;
		}
		else if( key.Icmp( "solid" ) == 0 )
		{
			s.solid = atoi( value ) != 0;
		}
		else if( key.Icmp( "surface" ) == 0 )
		{
			s.surface = value;
		}
		else
		{
			for( int i = 0; i < SLOT_COUNT; i++ )
			{
				if( key.Icmp( va( "%s_picture", slotKeys[i] ) ) == 0 )
				{
					s.pictures[i] = value;
				}
			}
		}
	}
	return true;
}

/*
-------------------------------------------------------------------------------
	pictures
-------------------------------------------------------------------------------
*/

struct picture_t
{
	byte*	pixels = NULL;		// RGBA
	int		width = 0;
	int		height = 0;

	~picture_t()
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
	bool LoadProject( const char* relativePath )
	{
		void* buffer = NULL;
		const int length = fileSystem->ReadFile( relativePath, &buffer );
		if( length <= 0 || buffer == NULL )
		{
			return false;
		}
		const bool ok = Load( ( const byte* )buffer, length );
		fileSystem->FreeFile( buffer );
		return ok;
	}
	bool Valid() const
	{
		return pixels != NULL;
	}
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

// a file's bytes: an OS path, or a path inside the project
static bool ReadAnyFile( const char* path, idList<byte>& bytes )
{
	idFile* f = fileSystem->OpenExplicitFileRead( path );
	if( f == NULL )
	{
		f = fileSystem->OpenFileRead( path );
	}
	if( f == NULL )
	{
		return false;
	}
	bytes.SetNum( f->Length() );
	const int read = f->Read( bytes.Ptr(), bytes.Num() );
	fileSystem->CloseFile( f );
	return read == bytes.Num();
}

// the project path of a picture the engine knows by name (no extension)
static idStr PictureFile( const char* imageName )
{
	idStr base = imageName;
	base.BackSlashesToSlashes();
	base.StripFileExtension();
	for( const char* ext : { ".png", ".tga", ".jpg", ".jpeg" } )
	{
		if( fileSystem->ReadFile( base + ext, NULL, NULL ) > 0 )
		{
			return base + ext;
		}
	}
	return "";
}

static idStr MapName( const idStr& picture )
{
	idStr name = picture;
	name.StripFileExtension();
	return name;
}

// write pixels into the material's folder; the file name carries a hash of the content, so undo still finds older pictures
static bool StorePicture( const char* name, const char* key, const byte* rgba, int width, int height, idStr& path )
{
	const unsigned int hash = MD5_BlockChecksum( rgba, width * height * 4 ) ^ ( unsigned int )( width * 73856093 ) ^ ( unsigned int )( height * 19349663 );
	path = va( "%s/%s_%08x.png", PictureFolder( name ).c_str(), key, hash );
	if( fileSystem->ReadFile( path, NULL, NULL ) > 0 )
	{
		return true;		// the same picture is already there
	}
	return WritePNG( path, rgba, width, height );
}

bool SetPicture( const char* name, settings_t& s, slot_t slot, const char* osPath, idStr& error )
{
	if( osPath == NULL || osPath[0] == '\0' )
	{
		s.pictures[slot].Clear();
		return true;
	}
	idList<byte> bytes;
	if( !ReadAnyFile( osPath, bytes ) || bytes.Num() == 0 )
	{
		error = va( "Couldn't read %s.", osPath );
		return false;
	}
	picture_t pic;
	if( !pic.Load( bytes.Ptr(), bytes.Num() ) )
	{
		error = va( "Couldn't read the picture %s (%s).", osPath, stbi_failure_reason() );
		return false;
	}
	idStr path;
	if( !StorePicture( name, slotKeys[slot], pic.pixels, pic.width, pic.height, path ) )
	{
		error = va( "Couldn't write the picture into %s.", PictureFolder( name ).c_str() );
		return false;
	}
	s.pictures[slot] = path;
	return true;
}

// bumps and roughness from the colour picture: brightness is read as height (light is high),
// its slope makes the normal map, and its local contrast makes the roughness
bool MakeFromColor( const char* name, settings_t& s, float bumps, float roughness, bool makeNormal, bool makeRough, idStr& error )
{
	if( s.pictures[SLOT_COLOR].Length() == 0 )
	{
		error = "Put a colour picture in first: the others are made from it.";
		return false;
	}
	picture_t color;
	if( !color.LoadProject( s.pictures[SLOT_COLOR] ) )
	{
		error = va( "Couldn't read %s.", s.pictures[SLOT_COLOR].c_str() );
		return false;
	}
	const int w = color.width;
	const int h = color.height;
	idList<float> height;
	height.SetNum( w * h );
	for( int i = 0; i < w * h; i++ )
	{
		const byte* p = color.pixels + i * 4;
		height[i] = ( 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2] ) / 255.0f;
	}
	// a light blur, so noise in the picture doesn't turn into sharp bumps (wraps, as textures tile)
	idList<float> smooth;
	smooth.SetNum( w * h );
	for( int y = 0; y < h; y++ )
	{
		for( int x = 0; x < w; x++ )
		{
			float sum = 0.0f;
			for( int dy = -1; dy <= 1; dy++ )
			{
				for( int dx = -1; dx <= 1; dx++ )
				{
					sum += height[( ( y + dy + h ) % h ) * w + ( x + dx + w ) % w];
				}
			}
			smooth[y * w + x] = sum / 9.0f;
		}
	}
	auto H = [&]( int x, int y )
	{
		return smooth[( ( y + h ) % h ) * w + ( x + w ) % w];
	};

	idList<byte> out;
	out.SetNum( w * h * 4 );
	if( makeNormal )
	{
		const float strength = 2.0f + bumps * 14.0f;
		for( int y = 0; y < h; y++ )
		{
			for( int x = 0; x < w; x++ )
			{
				// Sobel
				const float gx = ( H( x + 1, y - 1 ) + 2.0f * H( x + 1, y ) + H( x + 1, y + 1 ) ) - ( H( x - 1, y - 1 ) + 2.0f * H( x - 1, y ) + H( x - 1, y + 1 ) );
				const float gy = ( H( x - 1, y + 1 ) + 2.0f * H( x, y + 1 ) + H( x + 1, y + 1 ) ) - ( H( x - 1, y - 1 ) + 2.0f * H( x, y - 1 ) + H( x + 1, y - 1 ) );
				idVec3 n( -gx * strength, gy * strength, 1.0f );	// OpenGL-style green up, as id Tech 4 reads them
				n.Normalize();
				byte* o = out.Ptr() + ( y * w + x ) * 4;
				o[0] = ( byte )idMath::ClampInt( 0, 255, idMath::Ftoi( ( n.x * 0.5f + 0.5f ) * 255.0f + 0.5f ) );
				o[1] = ( byte )idMath::ClampInt( 0, 255, idMath::Ftoi( ( n.y * 0.5f + 0.5f ) * 255.0f + 0.5f ) );
				o[2] = ( byte )idMath::ClampInt( 0, 255, idMath::Ftoi( ( n.z * 0.5f + 0.5f ) * 255.0f + 0.5f ) );
				o[3] = 255;
			}
		}
		idStr path;
		if( !StorePicture( name, slotKeys[SLOT_NORMAL], out.Ptr(), w, h, path ) )
		{
			error = va( "Couldn't write the picture into %s.", PictureFolder( name ).c_str() );
			return false;
		}
		s.pictures[SLOT_NORMAL] = path;
	}
	if( makeRough )
	{
		// rougher where the picture is busy and dark (cracks, grime), smoother where it's even
		for( int y = 0; y < h; y++ )
		{
			for( int x = 0; x < w; x++ )
			{
				const float detail = idMath::Fabs( height[y * w + x] - H( x, y ) ) * 4.0f;
				float r = roughness + ( 0.5f - H( x, y ) ) * 0.35f + detail;
				r = idMath::ClampFloat( 0.02f, 1.0f, r );
				byte* o = out.Ptr() + ( y * w + x ) * 4;
				o[0] = o[1] = o[2] = ( byte )idMath::Ftoi( r * 255.0f + 0.5f );
				o[3] = 255;
			}
		}
		idStr path;
		if( !StorePicture( name, slotKeys[SLOT_ROUGH], out.Ptr(), w, h, path ) )
		{
			error = va( "Couldn't write the picture into %s.", PictureFolder( name ).c_str() );
			return false;
		}
		s.pictures[SLOT_ROUGH] = path;
	}
	return true;
}

// roughness, metal and occlusion in one picture, as the renderer wants them (R, G, B)
static bool MakeRmao( const char* name, const settings_t& s, idStr& path )
{
	picture_t rough, metal, ao;
	if( s.pictures[SLOT_ROUGH].Length() > 0 )
	{
		rough.LoadProject( s.pictures[SLOT_ROUGH] );
	}
	if( s.pictures[SLOT_METAL].Length() > 0 )
	{
		metal.LoadProject( s.pictures[SLOT_METAL] );
	}
	if( s.pictures[SLOT_AO].Length() > 0 )
	{
		ao.LoadProject( s.pictures[SLOT_AO] );
	}
	int w = 4, h = 4;
	for( const picture_t* p : { &rough, &metal, &ao } )
	{
		if( p->Valid() && p->width * p->height > w * h )
		{
			w = p->width;
			h = p->height;
		}
	}
	idList<byte> out;
	out.SetNum( w * h * 4 );
	const byte r = ( byte )idMath::ClampInt( 0, 255, idMath::Ftoi( s.roughness * 255.0f + 0.5f ) );
	const byte m = ( byte )idMath::ClampInt( 0, 255, idMath::Ftoi( s.metal * 255.0f + 0.5f ) );
	for( int y = 0; y < h; y++ )
	{
		for( int x = 0; x < w; x++ )
		{
			byte* p = &out[( y * w + x ) * 4];
			p[0] = rough.Valid() ? rough.Sample( x, y, w, h, 0 ) : r;
			p[1] = metal.Valid() ? metal.Sample( x, y, w, h, 0 ) : m;
			p[2] = ao.Valid() ? ao.Sample( x, y, w, h, 0 ) : 255;
			p[3] = 128;		// room for a height map later
		}
	}
	return StorePicture( name, "rmao", out.Ptr(), w, h, path );
}

/*
-------------------------------------------------------------------------------
	writing
-------------------------------------------------------------------------------
*/

static idStr SettingsLine( const settings_t& s )
{
	idStr line = va( "%s preset \"%s\" tint \"%g %g %g\" roughness %g metalness %g glow %g transparency %d opacity %g twosided %d shadows %d solid %d surface \"%s\"",
					 SETTINGS_MARK, s.preset.c_str(), s.tint.x, s.tint.y, s.tint.z, s.roughness, s.metal, s.glow, s.transparency, s.opacity,
					 s.twoSided ? 1 : 0, s.shadows ? 1 : 0, s.solid ? 1 : 0, s.surface.c_str() );
	for( int i = 0; i < SLOT_COUNT; i++ )
	{
		line += va( " %s_picture \"%s\"", slotKeys[i], s.pictures[i].c_str() );
	}
	return line;
}

static idStr MaterialText( const char* name, const settings_t& s, const idStr& rmao )
{
	const idStr color = s.pictures[SLOT_COLOR].Length() > 0 ? MapName( s.pictures[SLOT_COLOR] ) : idStr( "_white" );
	idStr t;
	t += "// Made in the Daedalus Material panel: one material per file.\n";
	t += "// The panel reads the settings line inside it and writes everything else, so\n";
	t += "// change that line (or the panel) rather than the rest.\n\n";
	t += va( "%s\n{\n", DeclName( name ).c_str() );
	t += va( "\t// %s\n", SettingsLine( s ).c_str() );
	if( s.pictures[SLOT_COLOR].Length() > 0 )
	{
		t += va( "\tqer_editorimage\t%s\n", color.c_str() );
	}
	if( s.surface.Length() > 0 && s.surface.Icmp( "none" ) != 0 )
	{
		t += va( "\t%s\n", s.surface.c_str() );
	}
	if( s.twoSided )
	{
		t += "\ttwoSided\n";
	}
	if( !s.shadows || s.transparency == SEE_THROUGH )
	{
		t += "\tnoShadows\n";
	}
	if( !s.solid )
	{
		t += "\tnonsolid\n";
	}
	const bool tinted = !s.tint.Compare( idVec3( 1, 1, 1 ), 0.002f );
	if( s.transparency == SEE_THROUGH )
	{
		t += "\ttranslucent\n";
		t += "\t{\n\t\tblend\tblend\n";
		t += va( "\t\tmap\t\t%s\n", color.c_str() );
		t += va( "\t\tcolor\t%.3f, %.3f, %.3f, %.3f\n", s.tint.x, s.tint.y, s.tint.z, s.opacity );
		t += "\t}\n";
	}
	else
	{
		t += "\t{\n\t\tblend\tbasecolormap\n";
		t += va( "\t\tmap\t\t%s\n", color.c_str() );
		if( tinted )
		{
			t += va( "\t\tcolor\t%.3f, %.3f, %.3f, 1\n", s.tint.x, s.tint.y, s.tint.z );
		}
		if( s.transparency == SEE_CUTOUT )
		{
			t += "\t\talphaTest\t0.5\n";
		}
		t += "\t}\n";
		t += va( "\tnormalmap\t%s\n", s.pictures[SLOT_NORMAL].Length() > 0 ? MapName( s.pictures[SLOT_NORMAL] ).c_str() : "_flat" );
		if( rmao.Length() > 0 )
		{
			t += va( "\trmaomap\t\t%s\n", MapName( rmao ).c_str() );
		}
	}
	if( s.glow > 0.0f )
	{
		const idStr glowMap = s.pictures[SLOT_GLOW].Length() > 0 ? MapName( s.pictures[SLOT_GLOW] ) : color;
		t += "\t{\n\t\tblend\tadd\n";
		t += va( "\t\tmap\t\t%s\n", glowMap.c_str() );
		t += va( "\t\tcolor\t%.3f, %.3f, %.3f, 1\n", s.glow * ( s.pictures[SLOT_GLOW].Length() > 0 ? 1.0f : s.tint.x ),
				 s.glow * ( s.pictures[SLOT_GLOW].Length() > 0 ? 1.0f : s.tint.y ), s.glow * ( s.pictures[SLOT_GLOW].Length() > 0 ? 1.0f : s.tint.z ) );
		t += "\t}\n";
	}
	t += "}\n";
	return t;
}

bool Save( const char* name, const settings_t& s, idStr& error, idStr* before, idStr* after )
{
	idStr old;
	const bool existed = ReadText( FilePath( name ), old );
	if( before != NULL )
	{
		*before = existed ? old : idStr();
	}
	idStr rmao;
	if( s.transparency != SEE_THROUGH && !MakeRmao( name, s, rmao ) )
	{
		error = va( "Couldn't write the roughness picture into %s.", PictureFolder( name ).c_str() );
		return false;
	}
	const idStr text = MaterialText( name, s, rmao );
	if( after != NULL )
	{
		*after = text;
	}
	if( existed && old == text )
	{
		return true;
	}
	if( !WriteText( FilePath( name ), text ) )
	{
		error = va( "Couldn't write %s.", FilePath( name ).c_str() );
		return false;
	}
	editableCache.Delete( DeclName( name ) );
	Reload( name, true );
	return true;
}

bool Create( const char* name, const char* preset, idStr& error )
{
	settings_t s;
	ApplyPreset( s, preset );
	return Save( name, s, error );
}

void Reload( const char* name, bool picturesChanged )
{
	// new pictures have new names, so there's nothing to reload but the material itself
	declManager->ReloadFile( FilePath( name ), true );
	editableCache.Delete( DeclName( name ) );
}

/*
-------------------------------------------------------------------------------
	from pictures, and from other materials
-------------------------------------------------------------------------------
*/

static const char* PresetForName( const idStr& name, bool hasMetalPicture, bool hasGlow )
{
	struct guess_t
	{
		const char* words;
		const char* preset;
	};
	static const guess_t guesses[] =
	{
		{ " wood plank planks timber bark log floorboard parquet ", "Wood" },
		{ " brick bricks blockwork ", "Brick" },
		{ " stone rock concrete plaster marble tile tiles cobble cobblestone granite cement asphalt ", "Stone" },
		{ " glass window ", "Glass" },
		{ " water ", "Water" },
		{ " fabric cloth carpet leather rug curtain ", "Fabric" },
		{ " grass dirt ground sand mud soil gravel ", "Ground" },
		{ " leaf leaves foliage plant ivy ", "Foliage" },
		{ " plastic rubber ", "Plastic" },
		{ " lamp light screen neon sign ", "Glowing" },
		{ " metal steel iron rust brass copper aluminium aluminum chrome ", NULL },
	};
	idStrList words;
	Words( name, words );
	for( const guess_t& g : guesses )
	{
		for( int i = 0; i < words.Num(); i++ )
		{
			if( strstr( g.words, ( " " + words[i] + " " ).c_str() ) != NULL )
			{
				return g.preset != NULL ? g.preset : ( hasMetalPicture ? "Bare metal" : "Painted metal" );
			}
		}
	}
	if( hasGlow )
	{
		return "Glowing";
	}
	return hasMetalPicture ? "Bare metal" : "Stone";
}

bool CreateFromPictures( const idStrList& osPaths, idStrList& made, idStr& message )
{
	made.Clear();
	message.Clear();

	// group the pictures by the name they share
	idStrList groups;
	idList<idStrList> files;
	for( int i = 0; i < osPaths.Num(); i++ )
	{
		if( !IsPictureFile( osPaths[i] ) )
		{
			continue;
		}
		const idStr name = NameFromPicture( osPaths[i] );
		int at = groups.FindIndex( name );
		if( at < 0 )
		{
			at = groups.Num();
			groups.Append( name );
			files.Alloc();
		}
		files[at].Append( osPaths[i] );
	}

	idStrList skipped;
	for( int g = 0; g < groups.Num(); g++ )
	{
		// which picture goes where (the first of each kind wins)
		idStr bySlot[SLOT_COUNT];
		idStr unknown;
		for( int i = 0; i < files[g].Num(); i++ )
		{
			idStrList words;
			Words( files[g][i], words );
			int at;
			const int slot = FindSlotWord( words, at );
			if( slot == SLOT_COUNT )
			{
				skipped.Append( files[g][i] );
				continue;
			}
			if( slot < 0 )
			{
				if( unknown.Length() == 0 )
				{
					unknown = files[g][i];
				}
				continue;
			}
			if( bySlot[slot].Length() == 0 )
			{
				bySlot[slot] = files[g][i];
			}
		}
		// a picture with no slot word is the colour, when there isn't one already
		if( bySlot[SLOT_COLOR].Length() == 0 )
		{
			bySlot[SLOT_COLOR] = unknown;
		}

		const idStr name = UniqueName( groups[g] );
		settings_t s;
		ApplyPreset( s, PresetForName( groups[g], bySlot[SLOT_METAL].Length() > 0, bySlot[SLOT_GLOW].Length() > 0 ) );
		if( bySlot[SLOT_GLOW].Length() > 0 && s.glow <= 0.0f )
		{
			s.glow = 1.0f;
		}
		idStr error;
		bool ok = true;
		for( int slot = 0; slot < SLOT_COUNT && ok; slot++ )
		{
			if( bySlot[slot].Length() > 0 )
			{
				ok = SetPicture( name, s, ( slot_t )slot, bySlot[slot], error );
			}
		}
		if( ok )
		{
			ok = Save( name, s, error );
		}
		if( !ok )
		{
			message += error + " ";
			continue;
		}
		made.Append( name );
	}
	if( made.Num() > 0 )
	{
		idStr list;
		for( int i = 0; i < made.Num(); i++ )
		{
			list += ( i > 0 ? ", " : "" ) + made[i];
		}
		const idStr errors = message;
		message = va( "Made %s %s from the pictures.", made.Num() == 1 ? "the material" : "the materials", list.c_str() );
		if( errors.Length() > 0 )
		{
			message += " " + errors;
		}
	}
	if( skipped.Num() > 0 )
	{
		idStr name = skipped[0];
		name.StripPath();
		message += va( " %d picture%s weren't used (height, opacity and the like aren't supported yet), e.g. %s.", skipped.Num(), skipped.Num() == 1 ? "" : "s", name.c_str() );
	}
	return made.Num() > 0;
}

bool MakeEditableCopy( const char* declName, idStr& name, idStr& error )
{
	const idMaterial* mat = declManager->FindMaterial( declName, false );
	if( mat == NULL )
	{
		error = va( "There's no material called %s.", declName );
		return false;
	}
	idStr base = declName;
	base.StripPath();
	name = UniqueName( base );

	settings_t s;
	ApplyPreset( s, "Custom" );
	s.roughness = 0.7f;
	s.twoSided = mat->GetCullType() == CT_TWO_SIDED;
	s.shadows = mat->SurfaceCastsShadow();
	switch( mat->GetSurfaceType() )
	{
		case SURFTYPE_METAL:
			s.surface = "metal";
			break;
		case SURFTYPE_STONE:
			s.surface = "stone";
			break;
		case SURFTYPE_FLESH:
			s.surface = "flesh";
			break;
		case SURFTYPE_WOOD:
			s.surface = "wood";
			break;
		case SURFTYPE_CARDBOARD:
			s.surface = "cardboard";
			break;
		case SURFTYPE_LIQUID:
			s.surface = "liquid";
			break;
		case SURFTYPE_GLASS:
			s.surface = "glass";
			break;
		case SURFTYPE_PLASTIC:
			s.surface = "plastic";
			break;
		case SURFTYPE_RICOCHET:
			s.surface = "ricochet";
			break;
		default:
			s.surface = "none";
			break;
	}

	// take its pictures: colour, normal map, and the rmao picture split back into three
	for( int i = 0; i < mat->GetNumStages(); i++ )
	{
		const shaderStage_t* stage = mat->GetStage( i );
		if( stage->texture.image == NULL )
		{
			continue;
		}
		const char* imageName = stage->texture.image->GetName();
		if( imageName[0] == '_' || strchr( imageName, '(' ) != NULL )
		{
			continue;
		}
		const idStr file = PictureFile( imageName );
		if( file.Length() == 0 )
		{
			continue;
		}
		if( stage->lighting == SL_DIFFUSE && s.pictures[SLOT_COLOR].Length() == 0 )
		{
			SetPicture( name, s, SLOT_COLOR, file, error );
		}
		else if( stage->lighting == SL_BUMP && s.pictures[SLOT_NORMAL].Length() == 0 )
		{
			SetPicture( name, s, SLOT_NORMAL, file, error );
		}
		else if( stage->lighting == SL_SPECULAR && idStr::FindText( imageName, "rmao", false ) >= 0 )
		{
			picture_t rmao;
			if( rmao.LoadProject( file ) )
			{
				idList<byte> channel;
				channel.SetNum( rmao.width * rmao.height * 4 );
				for( int c = 0; c < 3; c++ )
				{
					for( int p = 0; p < rmao.width * rmao.height; p++ )
					{
						const byte v = rmao.pixels[p * 4 + c];
						channel[p * 4 + 0] = channel[p * 4 + 1] = channel[p * 4 + 2] = v;
						channel[p * 4 + 3] = 255;
					}
					const slot_t slot = c == 0 ? SLOT_ROUGH : ( c == 1 ? SLOT_METAL : SLOT_AO );
					idStr path;
					if( StorePicture( name, slotKeys[slot], channel.Ptr(), rmao.width, rmao.height, path ) )
					{
						s.pictures[slot] = path;
					}
				}
			}
		}
	}
	error.Clear();
	return Save( name, s, error );
}

/*
-------------------------------------------------------------------------------
	thumbnails, use counts, dropped pictures
-------------------------------------------------------------------------------
*/

const idMaterial* Thumbnail( const char* declName )
{
	const idMaterial* mat = declManager->FindMaterial( declName, false );
	if( mat == NULL )
	{
		return NULL;
	}
	idImage* image = mat->GetEditorImage();
	if( image == NULL || image->IsDefaulted() )
	{
		return NULL;
	}
	// "editorpic/<picture>" draws the picture flat (see idMaterial::SetDefaultText)
	return declManager->FindMaterial( va( "editorpic/%s", image->GetName() ) );
}

int CountUses( const idMapFile* map, const char* declName )
{
	if( map == NULL )
	{
		return 0;
	}
	int uses = 0;
	for( int e = 0; e < map->GetNumEntities(); e++ )
	{
		const idMapEntity* ent = map->GetEntity( e );
		for( int p = 0; p < ent->GetNumPrimitives(); p++ )
		{
			const idMapPrimitive* prim = ent->GetPrimitive( p );
			if( prim->GetType() == idMapPrimitive::TYPE_BRUSH )
			{
				const idMapBrush* brush = static_cast<const idMapBrush*>( prim );
				for( int s = 0; s < brush->GetNumSides(); s++ )
				{
					uses += idStr::Icmp( brush->GetSide( s )->GetMaterial(), declName ) == 0 ? 1 : 0;
				}
			}
			else if( prim->GetType() == idMapPrimitive::TYPE_PATCH )
			{
				uses += idStr::Icmp( static_cast<const idMapPatch*>( prim )->GetMaterial(), declName ) == 0 ? 1 : 0;
			}
		}
	}
	return uses;
}

static idStrList droppedPictures;
static int lastDropTime = 0;

void QueueDroppedPicture( const char* osPath )
{
	droppedPictures.Append( osPath );
	lastDropTime = Sys_Milliseconds();
}

bool TakeDroppedPictures( idStrList& osPaths )
{
	// files dropped together arrive one after another: wait for the last
	if( droppedPictures.Num() == 0 || Sys_Milliseconds() - lastDropTime < 400 )
	{
		return false;
	}
	osPaths = droppedPictures;
	droppedPictures.Clear();
	return true;
}

} // namespace Materials
} // namespace DaedalusEditor

#endif // DAEDALUS_EDITOR
