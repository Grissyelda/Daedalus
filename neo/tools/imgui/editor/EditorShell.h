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

#ifndef __DAEDALUS_EDITOR_SHELL_H__
#define __DAEDALUS_EDITOR_SHELL_H__

/*
===============================================================================

	Daedalus editor shell

	The executable starts in the editor: a docked workspace (menu bar,
	Outliner, Inspector, Content Browser, Problems) around the game view.
	Play runs the selected map in the same window; Stop returns to the editor.

	The editor is compiled in when DAEDALUS_EDITOR is defined (CMake option
	DAEDALUS_EDITOR, on by default) and can be switched off at runtime with
	+set editor 0. Shipped games build with DAEDALUS_EDITOR off.

===============================================================================
*/

namespace DaedalusEditor
{

#if defined( DAEDALUS_EDITOR )

// true when the editor is compiled in and the "editor" cvar is on
bool	IsEditorMode();

// true while the editor UI owns the mouse (editor mode and not playing a map, or Play paused)
bool	IsEditing();

// Play is paused for live editing: the game stays loaded but doesn't run, and the editor is back
bool	IsPlayPaused();

// main menu bar; drawn before the main dockspace so the dockspace sits below it
void	DrawMainMenuBar();

// the editor's panels, docked into the main dockspace
void	Draw( unsigned int mainDockSpaceId );

// the 3D view of the level being edited; drawn in place of the game view while editing
void	DrawWorld();

#else

inline bool	IsEditorMode()
{
	return false;
}
inline bool	IsEditing()
{
	return false;
}
inline bool	IsPlayPaused()
{
	return false;
}
inline void	DrawMainMenuBar() {}
inline void	Draw( unsigned int ) {}
inline void	DrawWorld() {}

#endif

}

#endif /* !__DAEDALUS_EDITOR_SHELL_H__ */
