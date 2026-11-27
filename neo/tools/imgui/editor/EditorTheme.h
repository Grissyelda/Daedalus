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

#ifndef __DAEDALUS_EDITOR_THEME_H__
#define __DAEDALUS_EDITOR_THEME_H__

#include "../../../libs/imgui/imgui.h"
#include "EditorIcons.h"

/*
===============================================================================

	The editor's look: fonts, colours and the handful of widgets every
	panel shares (tool tiles, icon buttons, cards, help markers).

	Fonts are Inter for text and Lucide for icons (content/editor/fonts).
	Icons are UTF-8 strings (ICON_* in EditorIcons.h) and can be mixed into
	any label: ImGui::Button( ICON_SAVE " Save" ).

===============================================================================
*/

namespace DaedalusEditor
{
namespace UI
{

enum font_t
{
	FONT_BODY,		// Inter + icons, the default
	FONT_BOLD,		// Inter SemiBold + icons: headings, card titles
	FONT_TITLE,		// bigger SemiBold: dialog titles, the selection's name
	FONT_SMALL,		// small Inter: tile labels, badges
	FONT_ICON,		// big icons for tool tiles
	FONT_COUNT
};

// loads the fonts into the ImGui atlas and sets the style; call once after ImGui::CreateContext
void		Init();

ImFont*		Font( font_t font );
float		Scale();				// editor_uiScale

// colours
ImVec4		Accent( float alpha = 1.0f );
void		Emblem( float size );		// the Daedalus labyrinth, at the cursor
void		EmblemAt( ImDrawList* draw, const ImVec2& at, float size );
ImU32		AccentU32( float alpha = 1.0f );
ImVec4		Muted();				// secondary text
ImVec4		Good();					// green: Play, success
ImVec4		Bad();					// red: Stop, errors
ImVec4		Warn();					// warm yellow: warnings
ImU32		CardBg();
ImU32		RaisedBg();

// quiet, wrapped secondary text
void		Hint( const char* text );

// small muted capitals above a group of controls
void		SectionLabel( const char* text );

// a field label in a fixed-width column, then the control on the same line
void		FieldLabel( const char* text, float width = 0.0f );

// "?" that shows help on hover
void		HelpMarker( const char* text );

// tooltip on the last item, also when it's disabled
void		Tip( const char* text );

// a big square tool button: icon above a short label. active = the tool in use.
// size 0 = the default tile size.
bool		ToolTile( const char* id, const char* icon, const char* label, bool active, const char* tooltip, bool enabled = true, float size = 0.0f );

// default tile size for the current scale
float		TileSize();

// a frame-height square button with just an icon
bool		IconButton( const char* id, const char* icon, const char* tooltip, bool active = false, bool enabled = true );

// an icon button that flips a bool
bool		IconToggle( const char* id, const char* iconOn, const char* iconOff, bool* value, const char* tooltip );

// one segment of a row of mode tabs; returns true when clicked
bool		ModeTab( const char* icon, const char* label, bool active, const char* tooltip );

// filled accent / coloured button
bool		ColorButton( const char* label, const ImVec4& color, const ImVec2& size = ImVec2( 0, 0 ) );

// a rounded panel with an icon title, optional help, and a fold arrow.
// Content goes between BeginCard and EndCard only when BeginCard returns true; always call EndCard.
bool		BeginCard( const char* id, const char* icon, const char* title, const char* help = NULL, bool defaultOpen = true );
void		EndCard();

// a search box with a magnifier icon
bool		SearchField( const char* id, char* buffer, int bufferSize, const char* hint, float width = -1.0f );

// a thin vertical divider between groups on one line
void		VerticalDivider();

// "Show me": widgets mark where they are each frame (tool tiles by their id, icon buttons by
// theirs, mode tabs as "tab:<label>"); Highlight makes the one with that name glow for a while.
void		Mark( const char* name );			// the last item
void		MarkRect( const char* name, const ImVec2& min, const ImVec2& max );
void		Highlight( const char* name, float seconds = 6.0f );
void		DrawHighlights();					// once a frame, after everything else is drawn

// Help pages: the shell connects these. A card's "?" opens "cards/<card id>" when that page exists.
extern void	( *openHelpPage )( const char* page );
extern bool	( *helpPageExists )( const char* page );

// a small "?" button that opens a help page
bool		HelpButton( const char* page, const char* tooltip = NULL );

} // namespace UI
} // namespace DaedalusEditor

#endif /* !__DAEDALUS_EDITOR_THEME_H__ */
