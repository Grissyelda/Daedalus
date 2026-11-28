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

#include "EditorTheme.h"
#include "../../../libs/imgui/imgui_internal.h"

idCVar editor_uiScale( "editor_uiScale", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT | CVAR_NOCHEAT, "size of the editor's text and controls (1 = normal); takes effect after a restart", 0.5f, 3.0f );

namespace DaedalusEditor
{
namespace UI
{

static ImFont*	fonts[FONT_COUNT];
static float	scale = 1.0f;

// one accent on neutral greys
static const ImVec4 ACCENT( 0.204f, 0.827f, 0.600f, 1.0f );	// emerald (#34D399)
static const ImVec4 TEXT_COLOR( 0.90f, 0.91f, 0.92f, 1.0f );
static const ImVec4 MUTED( 0.56f, 0.58f, 0.61f, 1.0f );
static const ImVec4 GOOD( 0.55f, 0.85f, 0.70f, 1.0f );		// mint, kept apart from the emerald accent
static const ImVec4 BAD( 0.89f, 0.34f, 0.34f, 1.0f );
static const ImVec4 WARN( 0.98f, 0.76f, 0.32f, 1.0f );

static ImVec4 Grey( float v, float a = 1.0f )
{
	// slightly cool greys
	return ImVec4( v, v + 0.004f, v + 0.012f, a );
}

/*
===============================================================================

	Fonts

===============================================================================
*/

// a copy of a file in memory that the ImGui atlas owns (and frees)
static void* LoadFontFile( const char* path, int& size )
{
	void* buffer = NULL;
	size = fileSystem->ReadFile( path, &buffer );
	if( size <= 0 || buffer == NULL )
	{
		common->Warning( "Editor: couldn't load %s; using the built-in font", path );
		return NULL;
	}
	void* copy = IM_ALLOC( size );
	memcpy( copy, buffer, size );
	fileSystem->FreeFile( buffer );
	return copy;
}

static ImFont* AddFont( const char* path, float size, const ImWchar* ranges, const ImFontConfig& base )
{
	int bytes = 0;
	void* data = LoadFontFile( path, bytes );
	if( data == NULL )
	{
		return NULL;
	}
	ImFontConfig config = base;
	config.FontDataOwnedByAtlas = true;
	return ImGui::GetIO().Fonts->AddFontFromMemoryTTF( data, bytes, size, &config, ranges );
}

static const ImWchar* IconRanges()
{
	static const unsigned int codepoints[] = { ICON_CODEPOINT_LIST };
	static ImWchar ranges[sizeof( codepoints ) / sizeof( codepoints[0] ) * 2 + 1];
	if( ranges[0] == 0 )
	{
		int n = 0;
		for( int i = 0; codepoints[i] != 0; i++ )
		{
			ranges[n++] = ( ImWchar )codepoints[i];
			ranges[n++] = ( ImWchar )codepoints[i];
		}
		ranges[n] = 0;
	}
	return ranges;
}

static const ImWchar TEXT_RANGES[] =
{
	0x0020, 0x00FF,		// Latin-1
	0x2013, 0x2026,		// dashes, quotes, bullet, ellipsis
	0x2190, 0x2193,		// arrows
	0x2212, 0x2212,		// minus
	0
};

static ImFont* AddTextFont( const char* file, float size )
{
	ImFontConfig config;
	config.OversampleH = 2;
	config.OversampleV = 1;
	config.PixelSnapH = true;
	ImFont* font = AddFont( va( "editor/fonts/%s", file ), size, TEXT_RANGES, config );
	if( font == NULL )
	{
		return NULL;
	}

	// icons merged in, so labels can mix the two
	ImFontConfig icons;
	icons.MergeMode = true;
	icons.PixelSnapH = true;
	icons.OversampleH = 2;
	icons.OversampleV = 1;
	icons.GlyphMinAdvanceX = size * 1.1f;
	icons.GlyphOffset = ImVec2( 0.0f, size * 0.12f );
	AddFont( "editor/fonts/lucide.ttf", size, IconRanges(), icons );
	return font;
}

static void LoadFonts()
{
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Clear();

	fonts[FONT_BODY] = AddTextFont( "Inter-Regular.ttf", idMath::Rint( 15.0f * scale ) );
	if( fonts[FONT_BODY] == NULL )
	{
		// no font files (e.g. an old content folder): keep ImGui's own
		io.Fonts->Clear();
		io.Fonts->AddFontDefault();
		for( int i = 0; i < FONT_COUNT; i++ )
		{
			fonts[i] = io.Fonts->Fonts[0];
		}
		return;
	}
	fonts[FONT_BOLD] = AddTextFont( "Inter-SemiBold.ttf", idMath::Rint( 15.0f * scale ) );
	fonts[FONT_TITLE] = AddTextFont( "Inter-SemiBold.ttf", idMath::Rint( 19.0f * scale ) );
	fonts[FONT_SMALL] = AddTextFont( "Inter-Regular.ttf", idMath::Rint( 12.0f * scale ) );

	ImFontConfig big;
	big.PixelSnapH = true;
	big.OversampleH = 2;
	big.OversampleV = 2;
	fonts[FONT_ICON] = AddFont( "editor/fonts/lucide.ttf", idMath::Rint( 22.0f * scale ), IconRanges(), big );

	for( int i = 0; i < FONT_COUNT; i++ )
	{
		if( fonts[i] == NULL )
		{
			fonts[i] = fonts[FONT_BODY];
		}
	}
	io.FontDefault = fonts[FONT_BODY];
}

/*
===============================================================================

	Style

===============================================================================
*/

static void ApplyStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();
	style = ImGuiStyle();

	style.WindowPadding = ImVec2( 10, 10 );
	style.FramePadding = ImVec2( 8, 5 );
	style.CellPadding = ImVec2( 6, 4 );
	style.ItemSpacing = ImVec2( 8, 6 );
	style.ItemInnerSpacing = ImVec2( 6, 4 );
	style.IndentSpacing = 16;
	style.ScrollbarSize = 12;
	style.GrabMinSize = 10;

	style.WindowBorderSize = 1;
	style.ChildBorderSize = 0;
	style.PopupBorderSize = 1;
	style.FrameBorderSize = 0;
	style.TabBorderSize = 0;
	style.SeparatorTextBorderSize = 1;
	style.DockingSeparatorSize = 2;

	style.WindowRounding = 8;
	style.ChildRounding = 6;
	style.FrameRounding = 6;
	style.PopupRounding = 8;
	style.ScrollbarRounding = 8;
	style.GrabRounding = 6;
	style.TabRounding = 6;

	style.WindowTitleAlign = ImVec2( 0.0f, 0.5f );
	style.WindowMenuButtonPosition = ImGuiDir_None;
	style.SelectableTextAlign = ImVec2( 0.0f, 0.5f );

	ImVec4* c = style.Colors;
	c[ImGuiCol_Text]					= TEXT_COLOR;
	c[ImGuiCol_TextDisabled]			= MUTED;
	c[ImGuiCol_WindowBg]				= Grey( 0.118f );
	c[ImGuiCol_ChildBg]					= Grey( 0.0f, 0.0f );
	c[ImGuiCol_PopupBg]					= Grey( 0.140f, 0.98f );
	c[ImGuiCol_Border]					= Grey( 0.200f );
	c[ImGuiCol_BorderShadow]			= Grey( 0.0f, 0.0f );
	c[ImGuiCol_FrameBg]					= Grey( 0.165f );
	c[ImGuiCol_FrameBgHovered]			= Grey( 0.200f );
	c[ImGuiCol_FrameBgActive]			= Grey( 0.225f );
	c[ImGuiCol_TitleBg]					= Grey( 0.090f );
	c[ImGuiCol_TitleBgActive]			= Grey( 0.090f );
	c[ImGuiCol_TitleBgCollapsed]		= Grey( 0.090f );
	c[ImGuiCol_MenuBarBg]				= Grey( 0.090f );
	c[ImGuiCol_ScrollbarBg]				= Grey( 0.0f, 0.0f );
	c[ImGuiCol_ScrollbarGrab]			= Grey( 0.240f );
	c[ImGuiCol_ScrollbarGrabHovered]	= Grey( 0.300f );
	c[ImGuiCol_ScrollbarGrabActive]		= Grey( 0.340f );
	c[ImGuiCol_CheckMark]				= ACCENT;
	c[ImGuiCol_SliderGrab]				= Accent( 0.85f );
	c[ImGuiCol_SliderGrabActive]		= ACCENT;
	c[ImGuiCol_Button]					= Grey( 0.180f );
	c[ImGuiCol_ButtonHovered]			= Grey( 0.225f );
	c[ImGuiCol_ButtonActive]			= Grey( 0.260f );
	c[ImGuiCol_Header]					= Grey( 0.190f );
	c[ImGuiCol_HeaderHovered]			= Grey( 0.215f );
	c[ImGuiCol_HeaderActive]			= Accent( 0.30f );
	c[ImGuiCol_Separator]				= Grey( 0.200f );
	c[ImGuiCol_SeparatorHovered]		= Accent( 0.60f );
	c[ImGuiCol_SeparatorActive]			= ACCENT;
	c[ImGuiCol_ResizeGrip]				= Grey( 0.0f, 0.0f );
	c[ImGuiCol_ResizeGripHovered]		= Accent( 0.50f );
	c[ImGuiCol_ResizeGripActive]		= ACCENT;
	c[ImGuiCol_Tab]						= Grey( 0.090f );
	c[ImGuiCol_TabHovered]				= Grey( 0.165f );
	c[ImGuiCol_TabActive]				= Grey( 0.118f );
	c[ImGuiCol_TabUnfocused]			= Grey( 0.090f );
	c[ImGuiCol_TabUnfocusedActive]		= Grey( 0.118f );
	c[ImGuiCol_DockingPreview]			= Accent( 0.45f );
	c[ImGuiCol_DockingEmptyBg]			= Grey( 0.070f );
	c[ImGuiCol_PlotLines]				= MUTED;
	c[ImGuiCol_PlotLinesHovered]		= ACCENT;
	c[ImGuiCol_PlotHistogram]			= ACCENT;
	c[ImGuiCol_PlotHistogramHovered]	= ACCENT;
	c[ImGuiCol_TableHeaderBg]			= Grey( 0.150f );
	c[ImGuiCol_TableBorderStrong]		= Grey( 0.200f );
	c[ImGuiCol_TableBorderLight]		= Grey( 0.170f );
	c[ImGuiCol_TableRowBg]				= Grey( 0.0f, 0.0f );
	c[ImGuiCol_TableRowBgAlt]			= Grey( 1.0f, 0.025f );
	c[ImGuiCol_TextSelectedBg]			= Accent( 0.30f );
	c[ImGuiCol_DragDropTarget]			= ACCENT;
	c[ImGuiCol_NavHighlight]			= ACCENT;
	c[ImGuiCol_NavWindowingHighlight]	= Grey( 1.0f, 0.70f );
	c[ImGuiCol_NavWindowingDimBg]		= Grey( 0.0f, 0.40f );
	c[ImGuiCol_ModalWindowDimBg]		= Grey( 0.0f, 0.55f );

	style.ScaleAllSizes( scale );
}

void Init()
{
	scale = editor_uiScale.GetFloat();

	// a layout file of its own, so changes to the editor's panels start from a clean layout
	static idStr iniPath;
	iniPath = fileSystem->BuildOSPath( cvarSystem->GetCVarString( "fs_savepath" ), "editor_layout.ini" );
	ImGui::GetIO().IniFilename = iniPath.c_str();

	LoadFonts();
	ApplyStyle();
}

ImFont* Font( font_t font )
{
	return fonts[font] != NULL ? fonts[font] : ImGui::GetFont();
}

float Scale()
{
	return scale;
}

ImVec4 Accent( float alpha )
{
	return ImVec4( ACCENT.x, ACCENT.y, ACCENT.z, alpha );
}

ImU32 AccentU32( float alpha )
{
	return ImGui::ColorConvertFloat4ToU32( Accent( alpha ) );
}

ImVec4 Muted()
{
	return MUTED;
}

ImVec4 Good()
{
	return GOOD;
}

ImVec4 Bad()
{
	return BAD;
}

/*
================
Emblem

The Daedalus labyrinth (as in scripts/content/make_icon.py): a square spiral
running clockwise and inward from the bottom left, drawn at the cursor.
================
*/
void Emblem( float size )
{
	EmblemAt( ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), size );
	ImGui::Dummy( ImVec2( size, size ) );
}

void EmblemAt( ImDrawList* draw, const ImVec2& at, float size )
{
	const ImU32 color = ImGui::GetColorU32( ACCENT );
	const int rings = size < 28.0f ? 1 : ( size < 64.0f ? 2 : 3 );
	const float gap = size / ( 2 * rings + 1 );
	const float width = Max( 1.0f, gap * 0.5f );
	float left = at.x + width * 0.5f, top = at.y + width * 0.5f;
	float right = at.x + size - width * 0.5f, bottom = at.y + size - width * 0.5f;

	ImVec2 pts[64];
	int count = 0;
	pts[count++] = ImVec2( left, bottom );
	for( int i = 0; i <= rings && count < 60; i++ )
	{
		pts[count++] = ImVec2( left, top );
		pts[count++] = ImVec2( right, top );
		pts[count++] = ImVec2( right, bottom );
		left += gap;
		if( right - left < gap * 0.6f )
		{
			break;
		}
		pts[count++] = ImVec2( left, bottom );
		top += gap;
		right -= gap;
		bottom -= gap;
	}
	for( int i = 0; i + 1 < count; i++ )
	{
		// square ends, so the corners meet cleanly
		const ImVec2 a = pts[i], b = pts[i + 1];
		const float h = width * 0.5f;
		draw->AddRectFilled( ImVec2( Min( a.x, b.x ) - h, Min( a.y, b.y ) - h ), ImVec2( Max( a.x, b.x ) + h, Max( a.y, b.y ) + h ), color );
	}
}

ImVec4 Warn()
{
	return WARN;
}

ImU32 CardBg()
{
	return ImGui::ColorConvertFloat4ToU32( Grey( 0.150f ) );
}

ImU32 RaisedBg()
{
	return ImGui::ColorConvertFloat4ToU32( Grey( 0.190f ) );
}

/*
===============================================================================

	Widgets

===============================================================================
*/

void Hint( const char* text )
{
	ImGui::PushStyleColor( ImGuiCol_Text, MUTED );
	ImGui::TextWrapped( "%s", text );
	ImGui::PopStyleColor();
}

void SectionLabel( const char* text )
{
	ImGui::PushFont( Font( FONT_SMALL ) );
	ImGui::PushStyleColor( ImGuiCol_Text, MUTED );
	idStr upper = text;
	upper.ToUpper();
	ImGui::TextUnformatted( upper.c_str() );
	ImGui::PopStyleColor();
	ImGui::PopFont();
}

void FieldLabel( const char* text, float width )
{
	if( width <= 0.0f )
	{
		width = 82.0f * scale;
	}
	ImGui::AlignTextToFramePadding();
	ImGui::PushStyleColor( ImGuiCol_Text, MUTED );
	ImGui::TextUnformatted( text );
	ImGui::PopStyleColor();
	ImGui::SameLine( width );
	ImGui::SetNextItemWidth( -FLT_MIN );
}

void Tip( const char* text )
{
	if( text != NULL && text[0] != '\0' && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort ) )
	{
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 10 * scale, 8 * scale ) );
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos( 340.0f * scale );
		ImGui::TextUnformatted( text );
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
		ImGui::PopStyleVar();
	}
}

void HelpMarker( const char* text )
{
	ImGui::PushStyleColor( ImGuiCol_Text, MUTED );
	ImGui::TextUnformatted( ICON_HELP );
	ImGui::PopStyleColor();
	Tip( text );
}

float TileSize()
{
	return idMath::Rint( 62.0f * scale );
}

static void DrawCentered( ImDrawList* draw, ImFont* font, const ImVec2& center, ImU32 color, const char* text, float maxWidth )
{
	const ImVec2 size = font->CalcTextSizeA( font->FontSize, FLT_MAX, 0.0f, text );
	ImVec2 pos( center.x - size.x * 0.5f, center.y - size.y * 0.5f );
	if( size.x > maxWidth )
	{
		// too long: left-align inside the tile and clip
		pos.x = center.x - maxWidth * 0.5f;
		const ImVec4 clip( pos.x, pos.y, pos.x + maxWidth, pos.y + size.y );
		draw->AddText( font, font->FontSize, pos, color, text, NULL, 0.0f, &clip );
		return;
	}
	draw->AddText( font, font->FontSize, ImVec2( idMath::Rint( pos.x ), idMath::Rint( pos.y ) ), color, text );
}

void MarkRect( const char* name, const ImVec2& min, const ImVec2& max );

bool ToolTile( const char* id, const char* icon, const char* label, bool active, const char* tooltip, bool enabled, float size )
{
	const float s = size > 0.0f ? size : TileSize();
	ImGui::PushID( id );
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::BeginDisabled( !enabled );
	const bool pressed = ImGui::InvisibleButton( "##tile", ImVec2( s, s ) );
	ImGui::EndDisabled();
	const bool hovered = ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled );
	const bool held = ImGui::IsItemActive();

	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 max( pos.x + s, pos.y + s );
	const float rounding = 8.0f * scale;
	if( active )
	{
		draw->AddRectFilled( pos, max, AccentU32( 0.16f ), rounding );
		draw->AddRect( pos, max, AccentU32( 0.75f ), rounding, 0, 1.5f * scale );
	}
	else if( enabled && ( hovered || held ) )
	{
		draw->AddRectFilled( pos, max, ImGui::ColorConvertFloat4ToU32( Grey( held ? 0.24f : 0.20f ) ), rounding );
	}

	const ImU32 iconColor = !enabled ? ImGui::ColorConvertFloat4ToU32( Grey( 0.36f ) ) : active ? AccentU32() : ImGui::ColorConvertFloat4ToU32( TEXT_COLOR );
	const ImU32 labelColor = !enabled ? ImGui::ColorConvertFloat4ToU32( Grey( 0.36f ) ) : active ? AccentU32() : ImGui::ColorConvertFloat4ToU32( MUTED );
	const bool hasLabel = label != NULL && label[0] != '\0';
	DrawCentered( draw, Font( FONT_ICON ), ImVec2( pos.x + s * 0.5f, pos.y + s * ( hasLabel ? 0.40f : 0.5f ) ), iconColor, icon, s - 4.0f );
	if( hasLabel )
	{
		// a label too long for the tile keeps its first word ("Player start" -> "Player")
		ImFont* smallFont = Font( FONT_SMALL );
		idStr text = label;
		if( smallFont->CalcTextSizeA( smallFont->FontSize, FLT_MAX, 0.0f, text.c_str() ).x > s - 6.0f * scale && text.Find( ' ' ) > 0 )
		{
			text = text.Left( text.Find( ' ' ) );
		}
		DrawCentered( draw, smallFont, ImVec2( pos.x + s * 0.5f, pos.y + s * 0.80f ), labelColor, text.c_str(), s - 6.0f * scale );
	}

	if( tooltip != NULL && tooltip[0] != '\0' && !held )
	{
		Tip( tooltip );
	}
	ImGui::PopID();
	MarkRect( id, pos, max );
	return pressed && enabled;
}

bool IconButton( const char* id, const char* icon, const char* tooltip, bool active, bool enabled )
{
	const float h = ImGui::GetFrameHeight();
	ImGui::PushID( id );
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::BeginDisabled( !enabled );
	const bool pressed = ImGui::InvisibleButton( "##icon", ImVec2( h, h ) );
	ImGui::EndDisabled();
	const bool hovered = ImGui::IsItemHovered();
	const bool held = ImGui::IsItemActive();

	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 max( pos.x + h, pos.y + h );
	if( active )
	{
		draw->AddRectFilled( pos, max, AccentU32( 0.18f ), ImGui::GetStyle().FrameRounding );
	}
	else if( enabled && ( hovered || held ) )
	{
		draw->AddRectFilled( pos, max, ImGui::ColorConvertFloat4ToU32( Grey( held ? 0.26f : 0.21f ) ), ImGui::GetStyle().FrameRounding );
	}
	const ImU32 color = !enabled ? ImGui::ColorConvertFloat4ToU32( Grey( 0.36f ) ) : active ? AccentU32() : ImGui::ColorConvertFloat4ToU32( TEXT_COLOR );
	IconCentered( draw, pos, max, icon, color );

	Tip( tooltip );
	ImGui::PopID();
	MarkRect( id, pos, max );
	return pressed && enabled;
}

bool IconToggle( const char* id, const char* iconOn, const char* iconOff, bool* value, const char* tooltip )
{
	if( IconButton( id, *value ? iconOn : ( iconOff != NULL ? iconOff : iconOn ), tooltip, *value ) )
	{
		*value = !*value;
		return true;
	}
	return false;
}

bool ModeTab( const char* icon, const char* label, bool active, const char* tooltip )
{
	ImGui::PushID( label );
	ImFont* font = ImGui::GetFont();
	const idStr text = va( "%s  %s", icon, label );
	const ImVec2 textSize = font->CalcTextSizeA( font->FontSize, FLT_MAX, 0.0f, text.c_str() );
	const ImVec2 padding( 14.0f * scale, 7.0f * scale );
	const ImVec2 size( textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f );
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const bool pressed = ImGui::InvisibleButton( "##mode", size );
	const bool hovered = ImGui::IsItemHovered();

	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 max( pos.x + size.x, pos.y + size.y );
	if( active )
	{
		draw->AddRectFilled( pos, max, AccentU32( 0.95f ), size.y * 0.5f );
	}
	else if( hovered )
	{
		draw->AddRectFilled( pos, max, ImGui::ColorConvertFloat4ToU32( Grey( 0.22f ) ), size.y * 0.5f );
	}
	const ImU32 color = active ? ImGui::ColorConvertFloat4ToU32( Grey( 0.08f ) ) : ImGui::ColorConvertFloat4ToU32( hovered ? TEXT_COLOR : MUTED );
	draw->AddText( font, font->FontSize, ImVec2( pos.x + padding.x, pos.y + padding.y ), color, text.c_str() );

	Tip( tooltip );
	ImGui::PopID();
	MarkRect( va( "tab:%s", label ), pos, max );
	return pressed;
}

void IconCentered( ImDrawList* draw, const ImVec2& min, const ImVec2& max, const char* icon, ImU32 color )
{
	ImFont* font = ImGui::GetFont();
	unsigned int c = 0;
	ImTextCharFromUtf8( &c, icon, NULL );
	const ImFontGlyph* glyph = font->FindGlyphNoFallback( ( ImWchar )c );
	if( glyph == NULL )
	{
		const ImVec2 size = font->CalcTextSizeA( font->FontSize, FLT_MAX, 0.0f, icon );
		draw->AddText( font, font->FontSize, ImVec2( idMath::Rint( ( min.x + max.x - size.x ) * 0.5f ), idMath::Rint( ( min.y + max.y - size.y ) * 0.5f ) ), color, icon );
		return;
	}
	// the glyph's own box, not the font's line box: Lucide icons sit off the line's centre
	const float w = ( glyph->X1 - glyph->X0 );
	const float h = ( glyph->Y1 - glyph->Y0 );
	const ImVec2 at( idMath::Rint( ( min.x + max.x - w ) * 0.5f - glyph->X0 ), idMath::Rint( ( min.y + max.y - h ) * 0.5f - glyph->Y0 ) );
	draw->AddText( font, font->FontSize, at, color, icon, icon + ImTextCountUtf8BytesFromChar( icon, NULL ) );
}

bool PillButton( const char* id, const char* icon, const char* label, const ImVec4& color, float width, float h, bool enabled, bool darkText )
{
	ImGui::PushID( id );
	const bool hasLabel = label != NULL && label[0] != 0;
	ImGui::PushFont( Font( FONT_BOLD ) );
	const float pad = h * 0.42f;
	const float iconBox = h * 0.62f;
	const float gap = h * 0.22f;
	const float labelWidth = hasLabel ? ImGui::CalcTextSize( label ).x : 0.0f;
	if( width <= 0.0f )
	{
		width = hasLabel ? pad + iconBox + gap + labelWidth + pad : h;
	}
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::BeginDisabled( !enabled );
	const bool pressed = ImGui::InvisibleButton( "##pill", ImVec2( width, h ) );
	ImGui::EndDisabled();
	const bool hovered = enabled && ImGui::IsItemHovered();
	const bool held = enabled && ImGui::IsItemActive();

	ImVec4 fill = color;
	if( !enabled )
	{
		fill = ImVec4( color.x * 0.5f, color.y * 0.5f, color.z * 0.5f, 0.6f );
	}
	else if( held )
	{
		fill = ImVec4( color.x * 0.9f, color.y * 0.9f, color.z * 0.9f, 1.0f );
	}
	else if( hovered )
	{
		fill = ImVec4( Min( 1.0f, color.x * 1.12f ), Min( 1.0f, color.y * 1.12f ), Min( 1.0f, color.z * 1.12f ), 1.0f );
	}
	const ImVec2 max( pos.x + width, pos.y + h );
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled( pos, max, ImGui::ColorConvertFloat4ToU32( fill ), h * 0.5f );
	const ImU32 ink = ImGui::ColorConvertFloat4ToU32( !enabled ? Grey( 0.45f ) : darkText ? Grey( 0.06f ) : TEXT_COLOR );
	if( icon != NULL && icon[0] != 0 )
	{
		if( hasLabel )
		{
			// icon and label centred together
			const float total = iconBox + gap + labelWidth;
			const float startX = pos.x + ( width - total ) * 0.5f;
			IconCentered( draw, ImVec2( startX, pos.y ), ImVec2( startX + iconBox, max.y ), icon, ink );
			const ImVec2 ts = ImGui::CalcTextSize( label );
			draw->AddText( ImVec2( idMath::Rint( startX + iconBox + gap ), idMath::Rint( pos.y + ( h - ts.y ) * 0.5f ) ), ink, label );
		}
		else
		{
			IconCentered( draw, pos, max, icon, ink );
		}
	}
	else if( hasLabel )
	{
		const ImVec2 ts = ImGui::CalcTextSize( label );
		draw->AddText( ImVec2( idMath::Rint( pos.x + ( width - ts.x ) * 0.5f ), idMath::Rint( pos.y + ( h - ts.y ) * 0.5f ) ), ink, label );
	}
	ImGui::PopFont();
	ImGui::PopID();
	MarkRect( id, pos, max );
	return pressed && enabled;
}

bool ColorButton( const char* label, const ImVec4& color, const ImVec2& size )
{
	ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( color.x, color.y, color.z, 0.90f ) );
	ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( Min( 1.0f, color.x * 1.12f ), Min( 1.0f, color.y * 1.12f ), Min( 1.0f, color.z * 1.12f ), 1.0f ) );
	ImGui::PushStyleColor( ImGuiCol_ButtonActive, color );
	ImGui::PushStyleColor( ImGuiCol_Text, Grey( 0.06f ) );
	ImGui::PushFont( Font( FONT_BOLD ) );
	const bool pressed = ImGui::Button( label, size );
	ImGui::PopFont();
	ImGui::PopStyleColor( 4 );
	return pressed;
}

// cards: the background is drawn first, at the height the card had last frame
// (no draw-list splitting, so tables and other cards can go inside)
struct cardState_t
{
	ImVec2		start;
	float		width;
	float		oldWorkMax;
	float		oldContentMax;
	bool		open;
	bool		hidden;		// below the current depth: only a "More" line was drawn
	ImGuiID		heightId;
};
static idStaticList<cardState_t, 8> cardStack;

bool BeginCard( const char* id, const char* icon, const char* title, const char* help, bool defaultOpen )
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	ImGui::PushID( id );
	ImGuiStorage* storage = ImGui::GetStateStorage();
	bool* open = storage->GetBoolRef( ImGui::GetID( "##open" ), defaultOpen );

	cardState_t card;
	card.start = ImGui::GetCursorScreenPos();
	card.width = ImGui::GetContentRegionAvail().x;
	card.open = *open;
	card.hidden = false;
	card.heightId = ImGui::GetID( "##height" );

	const float pad = 10.0f * scale;
	const float headerHeight = ImGui::GetFontSize() + pad * 1.6f;
	const float lastHeight = Max( headerHeight, storage->GetFloat( card.heightId, headerHeight ) );
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled( card.start, ImVec2( card.start.x + card.width, card.start.y + lastHeight ), CardBg(), 8.0f * scale );

	// the "?" sits left of the fold arrow; with a help page it opens that instead of folding
	ImFont* bold = Font( FONT_BOLD );
	const idStr helpPage = va( "cards/%s", id );
	const bool hasPage = helpPageExists != NULL && helpPageExists( helpPage );
	const float helpWidth = bold->CalcTextSizeA( bold->FontSize, FLT_MAX, 0.0f, ICON_HELP ).x;
	const float chevronWidthPre = bold->CalcTextSizeA( bold->FontSize, FLT_MAX, 0.0f, ICON_CHEVRON_DOWN ).x;
	const ImVec2 helpAt( card.start.x + card.width - pad - chevronWidthPre - helpWidth - pad, card.start.y + ( headerHeight - bold->FontSize ) * 0.5f );
	const bool showHelp = ( help != NULL && help[0] != '\0' ) || hasPage;
	const bool overHelp = showHelp && ImGui::IsMouseHoveringRect( ImVec2( helpAt.x - 4, helpAt.y - 4 ), ImVec2( helpAt.x + helpWidth + 4, helpAt.y + bold->FontSize + 4 ) );

	// header: the whole row folds the card
	if( ImGui::InvisibleButton( "##header", ImVec2( card.width, headerHeight ) ) )
	{
		if( overHelp && hasPage && openHelpPage != NULL )
		{
			openHelpPage( helpPage );
		}
		else
		{
			*open = !*open;
		}
	}
	const bool headerHovered = ImGui::IsItemHovered() && !overHelp;
	const ImVec2 headerMin = ImGui::GetItemRectMin();
	const float textY = headerMin.y + ( headerHeight - bold->FontSize ) * 0.5f;
	float x = headerMin.x + pad;
	if( icon != NULL )
	{
		draw->AddText( bold, bold->FontSize, ImVec2( x, textY ), AccentU32(), icon );
		x += bold->FontSize * 1.6f;
	}
	draw->AddText( bold, bold->FontSize, ImVec2( x, textY ), ImGui::ColorConvertFloat4ToU32( TEXT_COLOR ), title );

	// fold arrow and help on the right
	const float right = headerMin.x + card.width - pad;
	const char* chevron = *open ? ICON_CHEVRON_DOWN : ICON_CHEVRON_RIGHT;
	const float chevronWidth = bold->CalcTextSizeA( bold->FontSize, FLT_MAX, 0.0f, chevron ).x;
	draw->AddText( bold, bold->FontSize, ImVec2( right - chevronWidth, textY ), ImGui::ColorConvertFloat4ToU32( headerHovered ? TEXT_COLOR : MUTED ), chevron );
	if( showHelp )
	{
		const ImVec2 helpMin( right - chevronWidth - helpWidth - pad, textY );
		draw->AddText( bold, bold->FontSize, helpMin, overHelp ? ( hasPage ? AccentU32() : ImGui::ColorConvertFloat4ToU32( TEXT_COLOR ) ) : ImGui::ColorConvertFloat4ToU32( MUTED ), ICON_HELP );
		if( overHelp )
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos( 340.0f * scale );
			if( help != NULL && help[0] != '\0' )
			{
				ImGui::TextUnformatted( help );
			}
			if( hasPage )
			{
				ImGui::TextColored( Accent(), "Click for the help page" );
			}
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	// the content sits inside the card's padding on both sides
	card.oldWorkMax = window->WorkRect.Max.x;
	card.oldContentMax = window->ContentRegionRect.Max.x;
	if( *open )
	{
		ImGui::Indent( pad );
		window->WorkRect.Max.x = headerMin.x + card.width - pad;
		window->ContentRegionRect.Max.x = headerMin.x + card.width - pad;
		ImGui::PushTextWrapPos( 0.0f );
	}
	cardStack.Append( card );
	return *open;
}

void EndCard()
{
	if( cardStack.Num() == 0 )
	{
		return;
	}
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	const cardState_t card = cardStack[cardStack.Num() - 1];
	cardStack.SetNum( cardStack.Num() - 1 );
	if( card.hidden )
	{
		ImGui::PopID();
		return;
	}

	const float pad = 10.0f * scale;
	if( card.open )
	{
		ImGui::PopTextWrapPos();
		ImGui::Dummy( ImVec2( 0.0f, pad * 0.4f ) );
		ImGui::Unindent( pad );
		window->WorkRect.Max.x = card.oldWorkMax;
		window->ContentRegionRect.Max.x = card.oldContentMax;
	}

	const float bottom = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;
	ImGui::GetStateStorage()->SetFloat( card.heightId, bottom - card.start.y );

	ImGui::Dummy( ImVec2( 0.0f, 2.0f * scale ) );
	ImGui::PopID();
}

/*
===============================================================================

	Depth: Create, Understand, Control

===============================================================================
*/

static idCVar editor_depth( "editor_depth", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER | CVAR_NOCHEAT, "how much the editor shows: 0 Create, 1 Understand, 2 Control", 0, 2 );

int Depth()
{
	return idMath::ClampInt( 0, 2, editor_depth.GetInteger() );
}

void SetDepth( int depth )
{
	editor_depth.SetInteger( idMath::ClampInt( 0, 2, depth ) );
}

const char* DepthName( int depth )
{
	static const char* names[] = { "Create", "Understand", "Control" };
	return names[idMath::ClampInt( 0, 2, depth )];
}

bool BeginCardAt( int depth, const char* id, const char* icon, const char* title, const char* help, bool defaultOpen )
{
	if( Depth() >= depth )
	{
		// in Control everything deeper starts open
		return BeginCard( id, icon, title, help, defaultOpen || Depth() == 2 );
	}

	// below its depth: one quiet line that opens the card here, for this session
	ImGui::PushID( id );
	ImGuiStorage* storage = ImGui::GetStateStorage();
	bool* revealed = storage->GetBoolRef( ImGui::GetID( "##revealed" ), false );
	if( *revealed )
	{
		ImGui::PopID();
		return BeginCard( id, icon, title, help, true );
	}
	ImGui::PushStyleColor( ImGuiCol_Text, MUTED );
	if( ImGui::Selectable( va( ICON_CHEVRON_RIGHT "  More: %s", title ), false ) )
	{
		*revealed = true;
	}
	ImGui::PopStyleColor();
	Tip( va( "Shown in %s and up. Click to open it here, or change the depth on the top bar.", DepthName( depth ) ) );
	cardState_t card;
	memset( &card, 0, sizeof( card ) );
	card.hidden = true;
	cardStack.Append( card );
	return false;
}

bool DepthSwitch()
{
	bool changed = false;
	const float h = ImGui::GetFrameHeight();
	ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 1.0f * scale, 0.0f ) );
	for( int i = 0; i < 3; i++ )
	{
		const bool active = Depth() == i;
		const char* name = DepthName( i );
		const float w = ImGui::CalcTextSize( name ).x + 16.0f * scale;
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::PushID( i );
		if( ImGui::InvisibleButton( "##depth", ImVec2( w, h ) ) && !active )
		{
			SetDepth( i );
			changed = true;
		}
		const bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImDrawFlags corners = i == 0 ? ImDrawFlags_RoundCornersLeft : i == 2 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone;
		draw->AddRectFilled( pos, ImVec2( pos.x + w, pos.y + h ), active ? AccentU32( 0.85f ) : ImGui::ColorConvertFloat4ToU32( Grey( hovered ? 0.24f : 0.18f ) ), h * 0.5f, corners );
		const ImVec2 ts = ImGui::CalcTextSize( name );
		draw->AddText( ImVec2( idMath::Rint( pos.x + ( w - ts.x ) * 0.5f ), idMath::Rint( pos.y + ( h - ts.y ) * 0.5f ) ), ImGui::ColorConvertFloat4ToU32( active ? Grey( 0.06f ) : TEXT_COLOR ), name );
		static const char* tips[] =
		{
			"Create: just what you need to make things. Deeper settings show as a 'More' line you can open.",
			"Understand: also the settings behind each thing, folded, and the text files they make.",
			"Control: everything open, raw keys and files included, for when you want full control.",
		};
		Tip( tips[i] );
		if( i < 2 )
		{
			ImGui::SameLine();
		}
	}
	ImGui::PopStyleVar();
	return changed;
}

bool SearchField( const char* id, char* buffer, int bufferSize, const char* hint, float width )
{
	const float iconSpace = ImGui::GetFontSize() * 1.6f;
	ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( ImGui::GetStyle().FramePadding.x + iconSpace, ImGui::GetStyle().FramePadding.y ) );
	ImGui::SetNextItemWidth( width );
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const bool changed = ImGui::InputTextWithHint( id, hint, buffer, bufferSize );
	ImGui::PopStyleVar();
	ImGui::GetWindowDrawList()->AddText( ImVec2( pos.x + ImGui::GetStyle().FramePadding.x + 2.0f * scale, pos.y + ImGui::GetStyle().FramePadding.y ),
										 ImGui::ColorConvertFloat4ToU32( MUTED ), ICON_SEARCH );
	return changed;
}

void VerticalDivider()
{
	ImGui::SameLine();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const float h = ImGui::GetFrameHeight();
	ImGui::GetWindowDrawList()->AddLine( ImVec2( pos.x, pos.y + h * 0.2f ), ImVec2( pos.x, pos.y + h * 0.8f ), ImGui::ColorConvertFloat4ToU32( Grey( 0.26f ) ), 1.0f );
	ImGui::Dummy( ImVec2( 1.0f, h ) );
	ImGui::SameLine();
}

/*
-------------------------------------------------------------------------------
	marks and highlights ("Show me")
-------------------------------------------------------------------------------
*/

struct mark_t
{
	idStr			name;
	ImVec2			min, max;
	ImGuiViewport* 	viewport;
	int				frame;
};
static idList<mark_t> marks;
static idStr		highlightName;
static int			highlightUntil = 0;

void ( *openHelpPage )( const char* page ) = NULL;
bool ( *helpPageExists )( const char* page ) = NULL;

void Mark( const char* name )
{
	MarkRect( name, ImGui::GetItemRectMin(), ImGui::GetItemRectMax() );
}

void MarkRect( const char* name, const ImVec2& min, const ImVec2& max )
{
	if( name == NULL || name[0] == '\0' )
	{
		return;
	}
	mark_t* mark = NULL;
	for( int i = 0; i < marks.Num(); i++ )
	{
		if( marks[i].name == name )
		{
			mark = &marks[i];
			break;
		}
	}
	if( mark == NULL )
	{
		mark = &marks.Alloc();
		mark->name = name;
	}
	mark->min = min;
	mark->max = max;
	mark->viewport = ImGui::GetWindowViewport();
	mark->frame = ImGui::GetFrameCount();

	// the glow is drawn with the widget, on top of it, in its own window
	if( highlightName.Length() > 0 && Sys_Milliseconds() <= highlightUntil && highlightName == name )
	{
		const float t = Sys_Milliseconds() * 0.006f;
		const float pulse = 0.5f + 0.5f * idMath::Sin( t );
		const float grow = 2.0f * scale + pulse * 3.0f * scale;
		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImGuiViewport* viewport = ImGui::GetWindowViewport();
		draw->PushClipRect( viewport->Pos, ImVec2( viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y ), false );
		draw->AddRectFilled( ImVec2( min.x - grow, min.y - grow ), ImVec2( max.x + grow, max.y + grow ), AccentU32( 0.10f + 0.12f * pulse ), 10.0f * scale );
		draw->AddRect( ImVec2( min.x - grow, min.y - grow ), ImVec2( max.x + grow, max.y + grow ), AccentU32( 0.55f + 0.45f * pulse ), 10.0f * scale, 0, 2.5f * scale );
		draw->PopClipRect();
	}
}

void Highlight( const char* name, float seconds )
{
	highlightName = name != NULL ? name : "";
	highlightUntil = Sys_Milliseconds() + ( int )( seconds * 1000.0f );
}

void DrawHighlights()
{
	// the glow is drawn as each widget marks itself (see MarkRect); kept for callers
}

bool HelpButton( const char* page, const char* tooltip )
{
	ImGui::PushID( page );
	const bool pressed = IconButton( "help", ICON_HELP, tooltip != NULL ? tooltip : "Help on this" );
	ImGui::PopID();
	if( pressed && openHelpPage != NULL )
	{
		openHelpPage( page );
	}
	return pressed;
}

} // namespace UI
} // namespace DaedalusEditor

#endif // DAEDALUS_EDITOR
