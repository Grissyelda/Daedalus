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

#ifndef __EDITOR_PROBLEMS_H__
#define __EDITOR_PROBLEMS_H__

#include "EditorDocument.h"

/*
===============================================================================

	Problems

	Checks a level (the open one, or any .map file) and says what's wrong in
	plain words, how bad it is, where it is, and what to do about it. The checks
	only read the map; fixing is the shell's business (it owns undo).

	Severity:
		error		the game will break, or the thing won't be there at all
		warning		it'll look or behave wrong
		note		worth knowing, nothing is broken

	Every problem has a code (for tests and scripting), the thing at fault when
	there is one, and a next step. Problems that can be fixed in one click name
	the fix; the shell carries it out.

===============================================================================
*/

namespace DaedalusEditor
{
namespace Problems
{

enum severity_t
{
	SEV_NOTE,
	SEV_WARNING,
	SEV_ERROR
};

enum fix_t
{
	FIX_NONE,
	FIX_ADD_START,			// place a player start
	FIX_MODEL_NAME,			// set a brush thing's "model" to its name
	FIX_RENAME,				// give the thing a name nothing else has
	FIX_REMOVE_KEY,			// remove the setting named by subject
	FIX_USE_MATERIAL,		// repaint every face using subject with the paint material
	FIX_UNLINK_PIECE,		// turn a copy of a missing piece into ordinary things
	FIX_SHOW_LEAK,			// draw the build's leak line and look along it
	FIX_BUILD_LOG			// open the build log
};

struct problem_t
{
	severity_t	severity = SEV_WARNING;
	idStr		code;				// "no_start", "unknown_type", ...
	idStr		text;				// what's wrong
	idStr		next;				// what to do about it
	Selection	where;				// the thing at fault; entity -1 when it isn't one thing
	int			count = 1;			// how many places it happens (faces with a missing material, say)
	idStr		subject;			// the material, setting, model or name involved
	fix_t		fix = FIX_NONE;
};

enum levelKind_t
{
	LEVEL,					// a level you play: needs a player start
	PIECE					// a piece's own map (maps/prefabs): doesn't
};

// everything wrong with a map, errors first
void			CheckLevel( const idMapFile* map, levelKind_t kind, idList<problem_t>& out );

// errors first, then warnings, then notes; keeps the order within each
void			Sort( idList<problem_t>& list );

// how many of each severity
void			Count( const idList<problem_t>& list, int& errors, int& warnings, int& notes );

const char* 	SeverityName( severity_t severity );		// "error", "warning", "note"
const char* 	SeverityIcon( severity_t severity );

// "2 errors, 1 warning" (or "no problems")
idStr			Summary( const idList<problem_t>& list );

// every .map under maps/ in the game folders, pieces included (sorted)
void			ListLevels( idStrList& maps );

// is there a picture file for this image name (any of the usual extensions)?
bool			ImageFileExists( const char* name );

} // namespace Problems
} // namespace DaedalusEditor

#endif
