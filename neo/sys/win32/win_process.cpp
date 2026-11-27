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

#include "win_local.h"

#include <shellapi.h>
#include <commdlg.h>

#pragma comment( lib, "comdlg32.lib" )
#pragma comment( lib, "shell32.lib" )

/*
===============================================================================

	Child processes (Win32)

	Used by the editor to build levels without freezing. The child gets no
	console window; what it prints goes to a file.

===============================================================================
*/

struct winProcess_t
{
	HANDLE	process;
	HANDLE	output;		// a reading handle on the output file
	bool	finished;
	int		exitCode;
};

// one argument, quoted the way the C runtime's command line parser reads it back
static void AppendArgument( idStr& cmdLine, const char* arg )
{
	if( cmdLine.Length() > 0 )
	{
		cmdLine += " ";
	}
	const bool needsQuotes = ( arg[0] == '\0' ) || strpbrk( arg, " \t\"" ) != NULL;
	if( !needsQuotes )
	{
		cmdLine += arg;
		return;
	}
	cmdLine += "\"";
	int backslashes = 0;
	for( const char* c = arg; *c != '\0'; c++ )
	{
		if( *c == '\\' )
		{
			backslashes++;
			continue;
		}
		if( *c == '"' )
		{
			for( int i = 0; i < backslashes * 2 + 1; i++ )
			{
				cmdLine += "\\";
			}
		}
		else
		{
			for( int i = 0; i < backslashes; i++ )
			{
				cmdLine += "\\";
			}
		}
		backslashes = 0;
		cmdLine += *c;
	}
	for( int i = 0; i < backslashes * 2; i++ )
	{
		cmdLine += "\\";
	}
	cmdLine += "\"";
}

// engine paths are UTF-8
static idList<wchar_t> Widen( const char* text )
{
	idList<wchar_t> wide;
	const int n = MultiByteToWideChar( CP_UTF8, 0, text, -1, NULL, 0 );
	wide.SetNum( Max( n, 1 ) );
	wide[0] = 0;
	if( n > 0 )
	{
		MultiByteToWideChar( CP_UTF8, 0, text, -1, wide.Ptr(), n );
	}
	return wide;
}

void* Sys_ProcessStart( const char* exePath, const char* const* args, int numArgs, const char* workingDir, const char* outputPath )
{
	idStr cmdLine;
	AppendArgument( cmdLine, exePath );
	for( int i = 0; i < numArgs; i++ )
	{
		AppendArgument( cmdLine, args[i] );
	}

	SECURITY_ATTRIBUTES sa = {};
	sa.nLength = sizeof( sa );
	sa.bInheritHandle = TRUE;
	const idList<wchar_t> wOutput = Widen( outputPath );
	HANDLE out = CreateFileW( wOutput.Ptr(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if( out == INVALID_HANDLE_VALUE )
	{
		idLib::Warning( "Couldn't create %s for %s", outputPath, exePath );
		return NULL;
	}
	HANDLE in = CreateFileW( L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL );

	STARTUPINFOW si = {};
	si.cb = sizeof( si );
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = in;
	si.hStdOutput = out;
	si.hStdError = out;
	PROCESS_INFORMATION pi = {};

	const idList<wchar_t> wExe = Widen( exePath );
	idList<wchar_t> wCmdLine = Widen( cmdLine.c_str() );		// CreateProcessW may write to it
	const idList<wchar_t> wDir = Widen( ( workingDir != NULL ) ? workingDir : "" );
	const BOOL ok = CreateProcessW( wExe.Ptr(), wCmdLine.Ptr(), NULL, NULL, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, NULL,
									wDir[0] != 0 ? wDir.Ptr() : NULL, &si, &pi );
	CloseHandle( out );
	if( in != INVALID_HANDLE_VALUE )
	{
		CloseHandle( in );
	}
	if( !ok )
	{
		idLib::Warning( "Couldn't start %s (error %u)", exePath, ( unsigned int )GetLastError() );
		return NULL;
	}
	CloseHandle( pi.hThread );

	winProcess_t* process = new winProcess_t;
	process->process = pi.hProcess;
	process->output = CreateFileW( wOutput.Ptr(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	process->finished = false;
	process->exitCode = 0;
	return process;
}

bool Sys_ProcessFinished( void* handle, int& exitCode )
{
	winProcess_t* process = static_cast<winProcess_t*>( handle );
	if( process == NULL )
	{
		exitCode = -1;
		return true;
	}
	if( !process->finished && WaitForSingleObject( process->process, 0 ) == WAIT_OBJECT_0 )
	{
		DWORD code = 0;
		GetExitCodeProcess( process->process, &code );
		process->finished = true;
		process->exitCode = ( int )code;
	}
	exitCode = process->exitCode;
	return process->finished;
}

int Sys_ProcessReadOutput( void* handle, char* buffer, int size )
{
	winProcess_t* process = static_cast<winProcess_t*>( handle );
	if( process == NULL || process->output == INVALID_HANDLE_VALUE || size <= 0 )
	{
		return 0;
	}
	DWORD got = 0;
	if( !ReadFile( process->output, buffer, ( DWORD )size, &got, NULL ) )
	{
		return 0;
	}
	return ( int )got;
}

void Sys_ProcessKill( void* handle )
{
	winProcess_t* process = static_cast<winProcess_t*>( handle );
	if( process == NULL || process->finished )
	{
		return;
	}
	TerminateProcess( process->process, 1 );
	WaitForSingleObject( process->process, 5000 );
	process->finished = true;
	process->exitCode = -1;
}

void Sys_ProcessFree( void* handle )
{
	winProcess_t* process = static_cast<winProcess_t*>( handle );
	if( process == NULL )
	{
		return;
	}
	if( !process->finished )
	{
		Sys_ProcessKill( process );
	}
	CloseHandle( process->process );
	if( process->output != INVALID_HANDLE_VALUE )
	{
		CloseHandle( process->output );
	}
	delete process;
}

/*
===============================================================================

	The desktop (Win32)

===============================================================================
*/

void ( *Sys_FileDropped )( const char* osPath ) = NULL;

void Sys_OpenFolder( const char* osPath )
{
	idStr path = osPath;
	path.SlashesToBackSlashes();
	idList<wchar_t> wide = Widen( path.c_str() );
	ShellExecuteW( NULL, L"open", wide.Ptr(), NULL, NULL, SW_SHOWNORMAL );
}

bool Sys_OpenFileDialog( const char* title, const char* filter, char* path, int pathSize )
{
	if( path == NULL || pathSize <= 1 )
	{
		return false;
	}
	path[0] = '\0';

	// "Label|*.a;*.b" -> "Label\0*.a;*.b\0\0"
	idList<wchar_t> wideFilter = Widen( filter );
	wideFilter.Append( 0 );
	for( int i = 0; i < wideFilter.Num(); i++ )
	{
		if( wideFilter[i] == L'|' )
		{
			wideFilter[i] = 0;
		}
	}
	idList<wchar_t> wideTitle = Widen( title );

	wchar_t file[MAX_PATH * 4];
	file[0] = 0;
	OPENFILENAMEW ofn;
	memset( &ofn, 0, sizeof( ofn ) );
	ofn.lStructSize = sizeof( ofn );
	ofn.hwndOwner = win32.hWnd;
	ofn.lpstrFilter = wideFilter.Ptr();
	ofn.lpstrFile = file;
	ofn.nMaxFile = sizeof( file ) / sizeof( file[0] );
	ofn.lpstrTitle = wideTitle.Ptr();
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
	if( !GetOpenFileNameW( &ofn ) )
	{
		return false;
	}
	const int n = WideCharToMultiByte( CP_UTF8, 0, file, -1, path, pathSize, NULL, NULL );
	return n > 0;
}
