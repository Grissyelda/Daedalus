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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/wait.h>
#include <unistd.h>

/*
===============================================================================

	Child processes (POSIX)

	Used by the editor to build levels without freezing. Everything the child
	needs is prepared before fork(), so between fork() and exec() it only makes
	calls that are safe in the child of a multithreaded process.

===============================================================================
*/

struct posixProcess_t
{
	pid_t	pid;
	int		output;		// read end of the output file
	bool	finished;
	int		exitCode;
};

void* Sys_ProcessStart( const char* exePath, const char* const* args, int numArgs, const char* workingDir, const char* outputPath )
{
	char** argv = new char*[ numArgs + 2 ];
	argv[0] = strdup( exePath );
	for( int i = 0; i < numArgs; i++ )
	{
		argv[i + 1] = strdup( args[i] );
	}
	argv[numArgs + 1] = NULL;

	const int out = open( outputPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644 );
	const pid_t pid = ( out >= 0 ) ? fork() : -1;
	if( pid == 0 )
	{
		// the child
		if( workingDir != NULL && workingDir[0] != '\0' && chdir( workingDir ) != 0 )
		{
			_exit( 126 );
		}
		const int devNull = open( "/dev/null", O_RDONLY );
		if( devNull >= 0 )
		{
			dup2( devNull, STDIN_FILENO );
		}
		dup2( out, STDOUT_FILENO );
		dup2( out, STDERR_FILENO );
		execv( argv[0], argv );
		_exit( 127 );
	}

	if( out >= 0 )
	{
		close( out );
	}
	for( int i = 0; argv[i] != NULL; i++ )
	{
		free( argv[i] );
	}
	delete[] argv;

	if( pid < 0 )
	{
		idLib::Warning( "Couldn't start %s: %s", exePath, strerror( errno ) );
		return NULL;
	}
	posixProcess_t* process = new posixProcess_t;
	process->pid = pid;
	process->output = open( outputPath, O_RDONLY | O_CLOEXEC );
	process->finished = false;
	process->exitCode = 0;
	return process;
}

bool Sys_ProcessFinished( void* handle, int& exitCode )
{
	posixProcess_t* process = static_cast<posixProcess_t*>( handle );
	if( process == NULL )
	{
		exitCode = -1;
		return true;
	}
	if( !process->finished )
	{
		int status = 0;
		const pid_t r = waitpid( process->pid, &status, WNOHANG );
		if( r == process->pid )
		{
			process->finished = true;
			process->exitCode = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
		}
		else if( r < 0 )
		{
			process->finished = true;
			process->exitCode = -1;
		}
	}
	exitCode = process->exitCode;
	return process->finished;
}

int Sys_ProcessReadOutput( void* handle, char* buffer, int size )
{
	posixProcess_t* process = static_cast<posixProcess_t*>( handle );
	if( process == NULL || process->output < 0 || size <= 0 )
	{
		return 0;
	}
	const ssize_t got = read( process->output, buffer, size );
	return got > 0 ? ( int )got : 0;
}

void Sys_ProcessKill( void* handle )
{
	posixProcess_t* process = static_cast<posixProcess_t*>( handle );
	if( process == NULL || process->finished )
	{
		return;
	}
	kill( process->pid, SIGKILL );
	waitpid( process->pid, NULL, 0 );
	process->finished = true;
	process->exitCode = -1;
}

void Sys_ProcessFree( void* handle )
{
	posixProcess_t* process = static_cast<posixProcess_t*>( handle );
	if( process == NULL )
	{
		return;
	}
	if( !process->finished )
	{
		Sys_ProcessKill( process );
	}
	if( process->output >= 0 )
	{
		close( process->output );
	}
	delete process;
}

/*
===============================================================================

	The desktop (POSIX)

	Showing a folder uses xdg-open (open on macOS); asking for a file uses
	zenity or kdialog when one is installed. Without them the editor falls
	back to typing a path or dropping the file on the window.

===============================================================================
*/

void ( *Sys_FileDropped )( const char* osPath ) = NULL;

// run a program without waiting for it and without a shell
static void LaunchDetached( const char* program, const char* arg )
{
	const pid_t pid = fork();
	if( pid == 0 )
	{
		// a grandchild, so nothing is left to reap
		if( fork() == 0 )
		{
			setsid();
			const int devNull = open( "/dev/null", O_RDWR );
			if( devNull >= 0 )
			{
				dup2( devNull, STDIN_FILENO );
				dup2( devNull, STDOUT_FILENO );
				dup2( devNull, STDERR_FILENO );
			}
			execlp( program, program, arg, ( char* )NULL );
		}
		_exit( 0 );
	}
	if( pid > 0 )
	{
		int status;
		waitpid( pid, &status, 0 );
	}
}

void Sys_OpenFolder( const char* osPath )
{
#if defined( __APPLE__ )
	LaunchDetached( "open", osPath );
#else
	LaunchDetached( "xdg-open", osPath );
#endif
}

static bool HaveProgram( const char* program )
{
	const char* path = getenv( "PATH" );
	if( path == NULL )
	{
		return false;
	}
	idStr dirs = path;
	int start = 0;
	while( start <= dirs.Length() )
	{
		int end = dirs.Find( ':', start );
		if( end < 0 )
		{
			end = dirs.Length();
		}
		idStr candidate( dirs, start, end );
		candidate += "/";
		candidate += program;
		if( access( candidate.c_str(), X_OK ) == 0 )
		{
			return true;
		}
		start = end + 1;
	}
	return false;
}

// run a dialog program and read the path it prints (blocks until it's closed)
static bool RunDialog( const char* const* argv, char* path, int pathSize )
{
	int pipeFds[2];
	if( pipe( pipeFds ) != 0 )
	{
		return false;
	}
	const pid_t pid = fork();
	if( pid < 0 )
	{
		close( pipeFds[0] );
		close( pipeFds[1] );
		return false;
	}
	if( pid == 0 )
	{
		dup2( pipeFds[1], STDOUT_FILENO );
		close( pipeFds[0] );
		close( pipeFds[1] );
		execvp( argv[0], ( char* const* )argv );
		_exit( 127 );
	}
	close( pipeFds[1] );
	int length = 0;
	while( length < pathSize - 1 )
	{
		const ssize_t got = read( pipeFds[0], path + length, pathSize - 1 - length );
		if( got <= 0 )
		{
			break;
		}
		length += ( int )got;
	}
	close( pipeFds[0] );
	path[length] = '\0';
	int status = 0;
	waitpid( pid, &status, 0 );
	while( length > 0 && ( path[length - 1] == '\n' || path[length - 1] == '\r' ) )
	{
		path[--length] = '\0';
	}
	return WIFEXITED( status ) && WEXITSTATUS( status ) == 0 && length > 0;
}

bool Sys_OpenFileDialog( const char* title, const char* filter, char* path, int pathSize )
{
	if( path == NULL || pathSize <= 1 )
	{
		return false;
	}
	path[0] = '\0';

	// "Label|*.a;*.b" -> "Label | *.a *.b" for zenity, "*.a *.b|Label" for kdialog
	idStr label = filter;
	idStr patterns;
	const int bar = label.Find( '|' );
	if( bar >= 0 )
	{
		patterns = label.c_str() + bar + 1;
		label.CapLength( bar );
		patterns.Replace( ";", " " );
	}

	if( HaveProgram( "zenity" ) )
	{
		const idStr titleArg = va( "--title=%s", title );
		const idStr filterArg = va( "--file-filter=%s | %s", label.c_str(), patterns.c_str() );
		const char* argv[] = { "zenity", "--file-selection", titleArg.c_str(), filterArg.c_str(), NULL };
		return RunDialog( argv, path, pathSize );
	}
	if( HaveProgram( "kdialog" ) )
	{
		const idStr filterArg = va( "%s|%s", patterns.c_str(), label.c_str() );
		const char* argv[] = { "kdialog", "--title", title, "--getopenfilename", ".", filterArg.c_str(), NULL };
		return RunDialog( argv, path, pathSize );
	}
	return false;
}

bool Sys_OpenFolderDialog( const char* title, char* path, int pathSize )
{
	if( path == NULL || pathSize <= 1 )
	{
		return false;
	}
	path[0] = '\0';
	if( HaveProgram( "zenity" ) )
	{
		const idStr titleArg = va( "--title=%s", title );
		const char* argv[] = { "zenity", "--file-selection", "--directory", titleArg.c_str(), NULL };
		return RunDialog( argv, path, pathSize );
	}
	if( HaveProgram( "kdialog" ) )
	{
		const char* argv[] = { "kdialog", "--title", title, "--getexistingdirectory", ".", NULL };
		return RunDialog( argv, path, pathSize );
	}
	return false;
}

bool Sys_ListFolder( const char* folder, idStrList& folders, idStrList& files )
{
	folders.Clear();
	files.Clear();
	DIR* dir = opendir( folder );
	if( dir == NULL )
	{
		return false;
	}
	struct dirent* entry;
	while( ( entry = readdir( dir ) ) != NULL )
	{
		if( strcmp( entry->d_name, "." ) == 0 || strcmp( entry->d_name, ".." ) == 0 )
		{
			continue;
		}
		idStr path = folder;
		path.AppendPath( entry->d_name );
		struct stat st;
		if( stat( path.c_str(), &st ) != 0 )
		{
			continue;
		}
		if( S_ISDIR( st.st_mode ) )
		{
			folders.Append( entry->d_name );
		}
		else if( S_ISREG( st.st_mode ) )
		{
			files.Append( entry->d_name );
		}
	}
	closedir( dir );
	return true;
}

void Sys_GameStoreFolders( idStrList& steamRoots, idStrList& gameFolders )
{
	const char* home = getenv( "HOME" );
	if( home == NULL || home[0] == 0 )
	{
		return;
	}
	const char* steam[] =
	{
		"/.steam/steam",
		"/.local/share/Steam",
		"/.var/app/com.valvesoftware.Steam/.local/share/Steam",
		"/Library/Application Support/Steam",
	};
	for( int i = 0; i < ( int )( sizeof( steam ) / sizeof( steam[0] ) ); i++ )
	{
		idStr path = idStr( home ) + steam[i];
		if( Sys_IsFolder( path.c_str() ) == FOLDER_YES )
		{
			steamRoots.AddUnique( path );
		}
	}
	const char* games[] = { "/GOG Games", "/Games" };
	for( int i = 0; i < ( int )( sizeof( games ) / sizeof( games[0] ) ); i++ )
	{
		idStr path = idStr( home ) + games[i];
		if( Sys_IsFolder( path.c_str() ) == FOLDER_YES )
		{
			gameFolders.AddUnique( path );
		}
	}
}
