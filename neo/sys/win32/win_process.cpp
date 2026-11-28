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

#include "win_local.h"

#include <shellapi.h>
#include <commdlg.h>
#include <shobjidl.h>

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

static bool RunFileDialog( const char* title, const char* filter, char* path, int pathSize )
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
	ofn.hwndOwner = NULL;	// see RunOnDialogThread
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

static bool RunFolderDialog( const char* title, char* path, int pathSize )
{
	if( path == NULL || pathSize <= 1 )
	{
		return false;
	}
	path[0] = '\0';

	const HRESULT init = CoInitializeEx( NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE );
	bool picked = false;
	IFileOpenDialog* dialog = NULL;
	if( SUCCEEDED( CoCreateInstance( CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &dialog ) ) ) )
	{
		DWORD options = 0;
		dialog->GetOptions( &options );
		dialog->SetOptions( options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST );
		idList<wchar_t> wideTitle = Widen( title );
		dialog->SetTitle( wideTitle.Ptr() );
		if( SUCCEEDED( dialog->Show( NULL ) ) )
		{
			IShellItem* item = NULL;
			if( SUCCEEDED( dialog->GetResult( &item ) ) )
			{
				PWSTR folder = NULL;
				if( SUCCEEDED( item->GetDisplayName( SIGDN_FILESYSPATH, &folder ) ) )
				{
					picked = WideCharToMultiByte( CP_UTF8, 0, folder, -1, path, pathSize, NULL, NULL ) > 0;
					CoTaskMemFree( folder );
				}
				item->Release();
			}
		}
		dialog->Release();
	}
	if( SUCCEEDED( init ) )
	{
		CoUninitialize();
	}
	return picked;
}

/*
The editor asks for dialogs from the game thread, while the main thread waits for that thread and
pumps no messages. A dialog owned by the main window then deadlocks (it sends its owner messages),
and the folder picker needs a single-threaded COM apartment, which the game thread may not have.
So each dialog runs on a thread of its own, with no owner, and the caller waits for it.
*/
struct dialogJob_t
{
	bool		folder;
	const char*	title;
	const char*	filter;
	char*		path;
	int			pathSize;
	bool		result;
};

static DWORD WINAPI DialogThread( LPVOID param )
{
	dialogJob_t* job = ( dialogJob_t* )param;
	job->result = job->folder ? RunFolderDialog( job->title, job->path, job->pathSize ) : RunFileDialog( job->title, job->filter, job->path, job->pathSize );
	return 0;
}

static bool RunOnDialogThread( dialogJob_t& job )
{
	job.result = false;
	if( job.path != NULL && job.pathSize > 0 )
	{
		job.path[0] = '\0';
	}
	HANDLE thread = CreateThread( NULL, 0, DialogThread, &job, 0, NULL );
	if( thread == NULL )
	{
		return false;
	}
	WaitForSingleObject( thread, INFINITE );
	CloseHandle( thread );
	return job.result;
}

bool Sys_OpenFileDialog( const char* title, const char* filter, char* path, int pathSize )
{
	dialogJob_t job = { false, title, filter, path, pathSize, false };
	return RunOnDialogThread( job );
}

bool Sys_OpenFolderDialog( const char* title, char* path, int pathSize )
{
	dialogJob_t job = { true, title, NULL, path, pathSize, false };
	return RunOnDialogThread( job );
}

// a string value from an open registry key, as UTF-8
static bool RegistryString( HKEY key, const wchar_t* subKey, const wchar_t* value, idStr& out )
{
	wchar_t buffer[MAX_PATH * 4];
	DWORD size = sizeof( buffer );
	if( RegGetValueW( key, subKey, value, RRF_RT_REG_SZ, NULL, buffer, &size ) != ERROR_SUCCESS )
	{
		return false;
	}
	char utf8[MAX_PATH * 4];
	if( WideCharToMultiByte( CP_UTF8, 0, buffer, -1, utf8, sizeof( utf8 ), NULL, NULL ) <= 0 )
	{
		return false;
	}
	out = utf8;
	out.BackSlashesToSlashes();
	return true;
}

static void AddFolderIfThere( idStrList& list, const idStr& folder )
{
	if( folder.Length() > 0 && Sys_IsFolder( folder.c_str() ) == FOLDER_YES )
	{
		list.AddUnique( folder );
	}
}

bool Sys_ListFolder( const char* folder, idStrList& folders, idStrList& files )
{
	folders.Clear();
	files.Clear();
	idStr search = folder;
	search.SlashesToBackSlashes();
	search.StripTrailing( '\\' );
	search += "\\*";
	idList<wchar_t> wide = Widen( search.c_str() );
	WIN32_FIND_DATAW data;
	HANDLE find = FindFirstFileW( wide.Ptr(), &data );
	if( find == INVALID_HANDLE_VALUE )
	{
		return false;
	}
	do
	{
		if( wcscmp( data.cFileName, L"." ) == 0 || wcscmp( data.cFileName, L".." ) == 0 )
		{
			continue;
		}
		char name[MAX_PATH * 4];
		if( WideCharToMultiByte( CP_UTF8, 0, data.cFileName, -1, name, sizeof( name ), NULL, NULL ) <= 0 )
		{
			continue;
		}
		if( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
		{
			folders.Append( name );
		}
		else
		{
			files.Append( name );
		}
	}
	while( FindNextFileW( find, &data ) );
	FindClose( find );
	return true;
}

void Sys_GameStoreFolders( idStrList& steamRoots, idStrList& gameFolders )
{
	idStr path;
	if( RegistryString( HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", path ) )
	{
		AddFolderIfThere( steamRoots, path );
	}
	HKEY key;
	if( RegOpenKeyExW( HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", 0, KEY_READ | KEY_WOW64_32KEY, &key ) == ERROR_SUCCESS )
	{
		if( RegistryString( key, NULL, L"InstallPath", path ) )
		{
			AddFolderIfThere( steamRoots, path );
		}
		RegCloseKey( key );
	}
	AddFolderIfThere( steamRoots, "C:/Program Files (x86)/Steam" );

	// GOG keeps one key per game, each with its install path
	if( RegOpenKeyExW( HKEY_LOCAL_MACHINE, L"SOFTWARE\\GOG.com\\Games", 0, KEY_READ | KEY_WOW64_32KEY, &key ) == ERROR_SUCCESS )
	{
		for( DWORD i = 0; ; i++ )
		{
			wchar_t name[256];
			DWORD length = sizeof( name ) / sizeof( name[0] );
			if( RegEnumKeyExW( key, i, name, &length, NULL, NULL, NULL, NULL ) != ERROR_SUCCESS )
			{
				break;
			}
			if( RegistryString( key, name, L"path", path ) )
			{
				AddFolderIfThere( gameFolders, path );
			}
		}
		RegCloseKey( key );
	}
	AddFolderIfThere( gameFolders, "C:/GOG Games" );
}
