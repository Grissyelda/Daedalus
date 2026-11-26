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

#ifndef __GRIZZ_EDITOR_SHELL_H__
#define __GRIZZ_EDITOR_SHELL_H__

/*
===============================================================================

	GrizzTech4 editor shell

	The executable starts in the editor: a docked workspace (menu bar,
	Outliner, Inspector, Content Browser, Problems) around the game view.
	Play runs the selected map in the same window; Stop returns to the editor.

	The editor is compiled in when GRIZZ_EDITOR is defined (CMake option
	GRIZZ_EDITOR, on by default) and can be switched off at runtime with
	+set editor 0. Shipped games build with GRIZZ_EDITOR off.

===============================================================================
*/

namespace GrizzEditor
{

#if defined( GRIZZ_EDITOR )

// true when the editor is compiled in and the "editor" cvar is on
bool	IsEditorMode();

// true while the editor UI owns the mouse (editor mode and not playing a map)
bool	IsEditing();

// main menu bar; drawn before the main dockspace so the dockspace sits below it
void	DrawMainMenuBar();

// the editor's panels, docked into the main dockspace
void	Draw( unsigned int mainDockSpaceId );

#else

inline bool	IsEditorMode()
{
	return false;
}
inline bool	IsEditing()
{
	return false;
}
inline void	DrawMainMenuBar() {}
inline void	Draw( unsigned int ) {}

#endif

}

#endif /* !__GRIZZ_EDITOR_SHELL_H__ */
