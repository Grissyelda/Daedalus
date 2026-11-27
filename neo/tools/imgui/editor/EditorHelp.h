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

#ifndef __EDITOR_HELP_H__
#define __EDITOR_HELP_H__

#include "../../../libs/imgui/imgui.h"

/*
===============================================================================

	Help

	Help pages and lessons are Markdown files under editor/help/ (the engine's
	in content/, a project's own in its folder, which can add pages and replace
	the engine's). The page id is the path without the extension, such as
	"tools/move". The Help panel shows them, with contents and search; "?"
	buttons all over the editor open the page for what they sit on.

	Lines at the top starting with "@" are settings, not text:
		@order 3			where the page goes in the contents (lower first)
		@alias cards/look	other ids that open this page
		@lesson				the page is a lesson
		@next lessons/x		the lesson to offer when this one is done

	Markdown: # ## ### headings, paragraphs, "- " bullets, "1. " numbers,
	"> " callouts, **bold**, `keys and names`, [links](tools/move) to pages or
	to https:// addresses.

	A lesson's steps are lines like
		- [ ] **Look around.** Hold the right mouse button... {check: camera.look} {show: build/Move}
	The step ticks itself off when the editor reports an event starting with
	the check ("camera.look", "did:Add light", ...; see Event). "Show me" opens
	the dock tab before the slash and makes the control after it glow.

===============================================================================
*/

namespace GrizzEditor
{
namespace Help
{

extern const char* WINDOW_HELP;

bool&		Visible();							// the Help panel is open
void		Open( const char* page );			// opens the panel on a page (NULL: the contents)
bool		PageExists( const char* page );
void		Reload();							// read the pages again (a project was opened)

// the panel; call every frame while Visible()
void		Draw();

// Something happened in the editor. Steps of the lesson under way whose check this name
// starts with are done. Names: camera.look, camera.fly, camera.pan, camera.orbit, tool.<name>,
// mode.<name>, view.top, view.3d, select.any, select.part, did:<undo label> (for every change,
// such as "did:Add light"), play.start, play.pause, play.resume, play.stop, build.done,
// build.leak, piece.made, piece.placed, piece.edit, piece.saved.all, piece.saved.copy,
// piece.override, help.open.
void		Event( const char* name );

// The lesson under way, its next step and "Show me", over the view while the Help panel
// isn't showing. topRight: the corner of the view.
void		DrawLessonStrip( const ImVec2& topRight );

// the shell's "Show me": "<dock tab>/<control>" or just "<control>"
extern void	( *showMe )( const char* target );

} // namespace Help
} // namespace GrizzEditor

#endif // !__EDITOR_HELP_H__
