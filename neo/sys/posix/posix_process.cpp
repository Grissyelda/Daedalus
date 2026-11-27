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


#include "precompiled.h"
#pragma hdrstop

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
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
