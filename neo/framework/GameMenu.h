/*
===========================================================================

Daedalus: the game menu (pause, new game, settings)

Copyright (C) 2026 the Daedalus authors

This file is part of Daedalus, which is based on the Doom 3 BFG Edition GPL
Source Code and RBDOOM-3-BFG. It is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version.

===========================================================================
*/

#ifndef __GAMEMENU_H__
#define __GAMEMENU_H__

/*
===============================================================================

	Game menu

	A menu drawn with ImGui for games that have none of their own: new
	Daedalus projects, and the original DOOM 3 data (whose menus are .gui
	files this engine doesn't use). Esc opens it while playing; it starts
	open when the game starts without a level. Settings cover display,
	sound and controls, including key bindings.

	The BFG Edition's own menus are used when its data is there.

===============================================================================
*/

namespace GameMenu
{
bool	Wanted();				// no menus of the game's own, and not playing from the editor
bool	IsOpen();
void	Open();
void	Close();
void	Toggle();
void	Draw();					// once a frame, inside the ImGui frame
bool	CaptureEvent( const sysEvent_t* event );	// a key being bound takes the next key press
void	RegisterCommands();
}

#endif
