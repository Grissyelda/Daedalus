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

#if defined( DAEDALUS_EDITOR )

#include "EditorTheme.h"
#include "EditorIcons.h"
#include "EditorHelp.h"
#include "../../../libs/imgui/imgui_internal.h"

idCVar editor_lesson( "editor_lesson", "", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT, "the lesson under way (a help page id), or empty" );

namespace DaedalusEditor
{
namespace Help
{

const char* WINDOW_HELP = ICON_HELP "  Help###Help";
void ( *showMe )( const char* target ) = NULL;

/*
===============================================================================

	Pages

===============================================================================
*/

struct step_t
{
	idStr		text;		// Markdown, without the check and show parts
	idStr		check;
	idStr		show;
};

struct page_t
{
	idStr			id;
	idStr			title;
	idStr			folder;
	int				order = 100;
	idStrList		aliases;
	bool			lesson = false;
	idStr			next;
	idStrList		lines;		// the body; a lesson step is "@@step <n>"
	idList<step_t>	steps;
	idStr			plain;		// lower-case text, for search
};

static idList<page_t>	pages;
static bool				loaded = false;
static bool				visible = false;
static bool				focusRequest = false;
static idStr			current;			// "" = contents
static idStrList		history;
static char				searchText[128] = "";
static idDict			progress;			// lesson id -> the done steps, "0 2 3"
static bool				progressLoaded = false;
static int				completedTime = 0;	// when the lesson under way was finished
static bool				panelShown = false;	// the panel was drawn last frame

static const char* PROGRESS_FILE = "editor/lessons.txt";

static idStr TakeTag( idStr& line, const char* tag )
{
	const int start = line.Find( va( "{%s:", tag ), false );
	if( start < 0 )
	{
		return "";
	}
	const int end = line.Find( '}', start );
	if( end < 0 )
	{
		return "";
	}
	idStr value = line.Mid( start + idStr::Length( tag ) + 2, end - start - idStr::Length( tag ) - 2 );
	value.Strip( ' ' );
	line = line.Left( start ) + line.Right( line.Length() - end - 1 );
	line.StripTrailingWhitespace();
	return value;
}

static void ParsePage( const char* id, const char* text, page_t& page )
{
	page.id = id;
	page.folder = "";
	const char* slash = strrchr( id, '/' );
	if( slash != NULL )
	{
		page.folder = idStr( id ).Left( ( int )( slash - id ) );
	}

	idStrList lines;
	idStr line;
	for( const char* c = text; ; c++ )
	{
		if( *c == '\n' || *c == '\0' )
		{
			line.StripTrailing( '\r' );
			lines.Append( line );
			line.Clear();
			if( *c == '\0' )
			{
				break;
			}
			continue;
		}
		line += *c;
	}

	bool inSettings = true;
	for( int i = 0; i < lines.Num(); i++ )
	{
		idStr l = lines[i];
		if( inSettings && l.Length() > 0 && l[0] == '@' )
		{
			idStr key = l.Right( l.Length() - 1 );
			idStr value;
			const int space = key.Find( ' ' );
			if( space > 0 )
			{
				value = key.Right( key.Length() - space - 1 );
				key = key.Left( space );
				value.Strip( ' ' );
			}
			if( key.Icmp( "order" ) == 0 )
			{
				page.order = atoi( value );
			}
			else if( key.Icmp( "alias" ) == 0 )
			{
				idStr word;
				for( int k = 0; k <= value.Length(); k++ )
				{
					if( k == value.Length() || value[k] == ' ' )
					{
						if( word.Length() > 0 )
						{
							page.aliases.Append( word );
						}
						word.Clear();
					}
					else
					{
						word += value[k];
					}
				}
			}
			else if( key.Icmp( "lesson" ) == 0 )
			{
				page.lesson = true;
			}
			else if( key.Icmp( "next" ) == 0 )
			{
				page.next = value;
			}
			continue;
		}
		if( l.Length() > 0 )
		{
			inSettings = false;
		}
		if( page.title.Length() == 0 && l.Cmpn( "# ", 2 ) == 0 )
		{
			page.title = l.Right( l.Length() - 2 );
			continue;
		}
		if( l.Cmpn( "- [ ]", 5 ) == 0 || l.Cmpn( "- [x]", 5 ) == 0 )
		{
			step_t& step = page.steps.Alloc();
			step.text = l.Right( l.Length() - 5 );
			step.check = TakeTag( step.text, "check" );
			step.show = TakeTag( step.text, "show" );
			step.text.StripLeading( ' ' );
			page.lines.Append( va( "@@step %d", page.steps.Num() - 1 ) );
			page.plain += step.text + " ";
			continue;
		}
		page.lines.Append( l );
		page.plain += l + " ";
	}
	if( page.title.Length() == 0 )
	{
		page.title = slash != NULL ? slash + 1 : id;
	}
	page.plain = page.title + " " + page.plain;
	page.plain.ToLower();
}

static void Load()
{
	if( loaded )
	{
		return;
	}
	loaded = true;
	pages.Clear();
	idFileList* files = fileSystem->ListFilesTree( "editor/help", ".md", true );
	const int skip = idStr::Length( "editor/help/" );
	for( int i = 0; files != NULL && i < files->GetNumFiles(); i++ )
	{
		idStr id = files->GetFile( i );
		if( id.Length() <= skip )
		{
			continue;
		}
		id = id.Right( id.Length() - skip );
		id.StripFileExtension();
		void* buffer = NULL;
		const int length = fileSystem->ReadFile( files->GetFile( i ), &buffer );
		if( length < 0 || buffer == NULL )
		{
			continue;
		}
		idStr text;
		text.Append( ( const char* )buffer, length );
		fileSystem->FreeFile( buffer );

		// a project's page replaces the engine's with the same id
		int existing = -1;
		for( int k = 0; k < pages.Num(); k++ )
		{
			if( pages[k].id.Icmp( id ) == 0 )
			{
				existing = k;
			}
		}
		page_t& page = existing >= 0 ? pages[existing] : pages.Alloc();
		page = page_t();
		ParsePage( id, text, page );
	}
	if( files != NULL )
	{
		fileSystem->FreeFileList( files );
	}
}

void Reload()
{
	loaded = false;
	Load();
}

static page_t* FindPage( const char* id )
{
	Load();
	if( id == NULL || id[0] == '\0' )
	{
		return NULL;
	}
	for( int i = 0; i < pages.Num(); i++ )
	{
		if( pages[i].id.Icmp( id ) == 0 )
		{
			return &pages[i];
		}
	}
	for( int i = 0; i < pages.Num(); i++ )
	{
		for( int k = 0; k < pages[i].aliases.Num(); k++ )
		{
			if( pages[i].aliases[k].Icmp( id ) == 0 )
			{
				return &pages[i];
			}
		}
	}
	return NULL;
}

bool PageExists( const char* page )
{
	return FindPage( page ) != NULL;
}

bool& Visible()
{
	return visible;
}

/*
===============================================================================

	Lessons

===============================================================================
*/

static void LoadProgress()
{
	if( progressLoaded )
	{
		return;
	}
	progressLoaded = true;
	void* buffer = NULL;
	const int length = fileSystem->ReadFile( PROGRESS_FILE, &buffer );
	if( length <= 0 || buffer == NULL )
	{
		return;
	}
	idStr text;
	text.Append( ( const char* )buffer, length );
	fileSystem->FreeFile( buffer );
	idStr line;
	for( int i = 0; i <= text.Length(); i++ )
	{
		if( i == text.Length() || text[i] == '\n' )
		{
			const int colon = line.Find( ':' );
			if( colon > 0 )
			{
				idStr value = line.Right( line.Length() - colon - 1 );
				value.Strip( ' ' );
				progress.Set( line.Left( colon ), value );
			}
			line.Clear();
			continue;
		}
		if( text[i] != '\r' )
		{
			line += text[i];
		}
	}
}

static void SaveProgress()
{
	idStr text;
	for( int i = 0; i < progress.GetNumKeyVals(); i++ )
	{
		text += va( "%s: %s\n", progress.GetKeyVal( i )->GetKey().c_str(), progress.GetKeyVal( i )->GetValue().c_str() );
	}
	fileSystem->WriteFile( PROGRESS_FILE, text.c_str(), text.Length(), "fs_savepath" );
}

static bool StepDone( const page_t& lesson, int step )
{
	LoadProgress();
	const char* done = progress.GetString( lesson.id );
	idStr word;
	for( const char* c = done; ; c++ )
	{
		if( *c == ' ' || *c == '\0' )
		{
			if( word.Length() > 0 && atoi( word ) == step )
			{
				return true;
			}
			word.Clear();
			if( *c == '\0' )
			{
				return false;
			}
			continue;
		}
		word += *c;
	}
}

static int StepsDone( const page_t& lesson )
{
	int n = 0;
	for( int i = 0; i < lesson.steps.Num(); i++ )
	{
		n += StepDone( lesson, i ) ? 1 : 0;
	}
	return n;
}

static void SetStepDone( const page_t& lesson, int step )
{
	if( StepDone( lesson, step ) )
	{
		return;
	}
	idStr done = progress.GetString( lesson.id );
	done += va( done.Length() > 0 ? " %d" : "%d", step );
	progress.Set( lesson.id, done );
	SaveProgress();
}

static void ResetLesson( const page_t& lesson )
{
	LoadProgress();
	progress.Delete( lesson.id );
	SaveProgress();
}

static page_t* ActiveLesson()
{
	page_t* page = FindPage( editor_lesson.GetString() );
	return ( page != NULL && page->lesson ) ? page : NULL;
}

static int NextStep( const page_t& lesson )
{
	for( int i = 0; i < lesson.steps.Num(); i++ )
	{
		if( !StepDone( lesson, i ) )
		{
			return i;
		}
	}
	return -1;
}

void Event( const char* name )
{
	page_t* lesson = ActiveLesson();
	if( lesson == NULL || name == NULL )
	{
		return;
	}
	const bool wasComplete = NextStep( *lesson ) < 0;
	for( int i = 0; i < lesson->steps.Num(); i++ )
	{
		const step_t& step = lesson->steps[i];
		if( step.check.Length() > 0 && idStr::Icmpn( name, step.check, step.check.Length() ) == 0 && !StepDone( *lesson, i ) )
		{
			SetStepDone( *lesson, i );
		}
	}
	if( !wasComplete && NextStep( *lesson ) < 0 && lesson->steps.Num() > 0 )
	{
		completedTime = Sys_Milliseconds();
	}
}

/*
===============================================================================

	Showing Markdown

===============================================================================
*/

enum
{
	SPAN_BOLD	= BIT( 0 ),
	SPAN_CODE	= BIT( 1 ),
	SPAN_LINK	= BIT( 2 )
};

struct word_t
{
	idStr		text;
	int			style;
	idStr		link;
	bool		spaceAfter;
};

// splits a line into words, each with its style and whether a space follows it
static void ParseInline( const char* text, idList<word_t>& words )
{
	words.Clear();
	int style = 0;
	idStr link;
	idStr wordText;
	auto flush = [&]( bool space )
	{
		if( wordText.Length() > 0 )
		{
			word_t& w = words.Alloc();
			w.text = wordText;
			w.style = style;
			w.link = link;
			w.spaceAfter = space;
			wordText.Clear();
		}
		else if( space && words.Num() > 0 )
		{
			words[words.Num() - 1].spaceAfter = true;
		}
	};

	for( const char* c = text; *c != '\0'; c++ )
	{
		if( c[0] == '*' && c[1] == '*' && !( style & SPAN_CODE ) )
		{
			flush( false );
			style ^= SPAN_BOLD;
			c++;
			continue;
		}
		if( c[0] == '`' )
		{
			if( style & SPAN_CODE )
			{
				// a code span stays one piece, spaces and all
				word_t& w = words.Alloc();
				w.text = wordText;
				w.style = style;
				w.link = link;
				w.spaceAfter = false;
				wordText.Clear();
				style &= ~SPAN_CODE;
			}
			else
			{
				flush( false );
				style |= SPAN_CODE;
			}
			continue;
		}
		if( style & SPAN_CODE )
		{
			wordText += *c;
			continue;
		}
		if( c[0] == '[' && !( style & SPAN_LINK ) )
		{
			// [text](target)
			const char* close = strchr( c, ']' );
			if( close != NULL && close[1] == '(' && strchr( close, ')' ) != NULL )
			{
				const char* end = strchr( close, ')' );
				flush( false );
				link = idStr( close + 2 ).Left( ( int )( end - close - 2 ) );
				style |= SPAN_LINK;
				idStr inner = idStr( c + 1 ).Left( ( int )( close - c - 1 ) );
				idList<word_t> innerWords;
				ParseInline( inner, innerWords );
				for( int i = 0; i < innerWords.Num(); i++ )
				{
					innerWords[i].style |= SPAN_LINK | ( style & SPAN_BOLD );
					innerWords[i].link = link;
					words.Append( innerWords[i] );
				}
				style &= ~SPAN_LINK;
				link.Clear();
				c = end;
				continue;
			}
		}
		if( *c == ' ' )
		{
			flush( true );
			continue;
		}
		wordText += *c;
	}
	flush( false );
}

static void FollowLink( const char* link )
{
	if( idStr::Icmpn( link, "http://", 7 ) == 0 || idStr::Icmpn( link, "https://", 8 ) == 0 )
	{
		sys->OpenURL( link, false );
		return;
	}
	Open( link );
}

// lays words out across the panel, wrapping; returns nothing, advances the cursor
static void DrawWords( const idList<word_t>& words, ImFont* font, ImU32 color, float indent )
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float left = origin.x + indent;
	const float right = origin.x + ImGui::GetContentRegionAvail().x;
	const float lineHeight = font->FontSize * 1.38f;
	ImFont* bold = UI::Font( UI::FONT_BOLD );
	const float space = font->CalcTextSizeA( font->FontSize, FLT_MAX, 0.0f, " " ).x;
	const float codePad = 4.0f * UI::Scale();
	const bool windowHovered = ImGui::IsWindowHovered();

	float x = left;
	float y = origin.y;
	for( int i = 0; i < words.Num(); i++ )
	{
		const word_t& w = words[i];
		ImFont* f = ( w.style & SPAN_BOLD ) ? bold : font;
		float width = f->CalcTextSizeA( font->FontSize, FLT_MAX, 0.0f, w.text.c_str() ).x;
		if( w.style & SPAN_CODE )
		{
			width += codePad * 2.0f;
		}
		// punctuation right after a word (no space between) stays with it
		const bool glued = i > 0 && !words[i - 1].spaceAfter;
		if( x + width > right && x > left && !glued )
		{
			x = left;
			y += lineHeight;
		}
		const ImVec2 min( x, y );
		const ImVec2 max( x + width, y + font->FontSize );
		ImU32 textColor = color;
		if( w.style & SPAN_CODE )
		{
			draw->AddRectFilled( ImVec2( min.x, min.y - 1.0f ), ImVec2( max.x, max.y + 2.0f ), UI::RaisedBg(), 4.0f * UI::Scale() );
			draw->AddText( f, font->FontSize, ImVec2( x + codePad, y ), color, w.text.c_str() );
		}
		else
		{
			if( w.style & SPAN_LINK )
			{
				const bool hovered = windowHovered && ImGui::IsMouseHoveringRect( min, ImVec2( max.x + ( w.spaceAfter ? space : 0.0f ), max.y + 2.0f ) );
				textColor = UI::AccentU32( hovered ? 1.0f : 0.85f );
				if( hovered )
				{
					draw->AddLine( ImVec2( min.x, max.y + 1.0f ), ImVec2( max.x, max.y + 1.0f ), textColor, 1.0f );
					ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
					if( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
					{
						FollowLink( w.link );
					}
				}
			}
			draw->AddText( f, font->FontSize, min, textColor, w.text.c_str() );
		}
		x += width + ( w.spaceAfter ? space : 0.0f );
	}
	ImGui::SetCursorScreenPos( origin );
	ImGui::Dummy( ImVec2( right - origin.x, y + lineHeight - origin.y ) );
}

static void DrawInline( const char* text, ImFont* font, ImU32 color, float indent = 0.0f )
{
	idList<word_t> words;
	ParseInline( text, words );
	DrawWords( words, font, color, indent );
}

// the words of a line without Markdown marks, for short summaries
static idStr PlainText( const char* text )
{
	idList<word_t> words;
	ParseInline( text, words );
	idStr out;
	for( int i = 0; i < words.Num(); i++ )
	{
		out += words[i].text;
		if( words[i].spaceAfter )
		{
			out += " ";
		}
	}
	return out;
}

static void DrawStep( const page_t& page, int index, bool isNext )
{
	const step_t& step = page.steps[index];
	const bool done = StepDone( page, index );
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 start = ImGui::GetCursorScreenPos();
	const float box = ImGui::GetFontSize();
	const float indent = box + 10.0f * UI::Scale();
	const ImVec2 center( start.x + box * 0.5f, start.y + box * 0.55f );
	if( done )
	{
		draw->AddCircleFilled( center, box * 0.5f, ImGui::ColorConvertFloat4ToU32( UI::Good() ) );
		ImFont* icons = UI::Font( UI::FONT_SMALL );
		const ImVec2 tick = icons->CalcTextSizeA( icons->FontSize, FLT_MAX, 0.0f, ICON_OK );
		draw->AddText( UI::Font( UI::FONT_BOLD ), UI::Font( UI::FONT_BOLD )->FontSize * 0.8f, ImVec2( center.x - tick.x * 0.5f, center.y - tick.y * 0.5f ), IM_COL32( 20, 20, 22, 255 ), ICON_OK );
	}
	else
	{
		draw->AddCircle( center, box * 0.5f - 1.0f, isNext ? UI::AccentU32() : ImGui::ColorConvertFloat4ToU32( UI::Muted() ), 0, 1.5f * UI::Scale() );
	}
	DrawInline( step.text, ImGui::GetFont(), ImGui::ColorConvertFloat4ToU32( done ? UI::Muted() : ImGui::GetStyleColorVec4( ImGuiCol_Text ) ), indent );
	if( !done && step.show.Length() > 0 && showMe != NULL )
	{
		ImGui::SetCursorPosX( ImGui::GetCursorPosX() + indent );
		ImGui::PushID( index );
		if( ImGui::SmallButton( ICON_EYE "  Show me" ) )
		{
			showMe( step.show );
		}
		ImGui::PopID();
	}
	ImGui::Dummy( ImVec2( 0.0f, 4.0f * UI::Scale() ) );
}

static void DrawBody( const page_t& page )
{
	const ImU32 textColor = ImGui::ColorConvertFloat4ToU32( ImGui::GetStyleColorVec4( ImGuiCol_Text ) );
	const int next = page.lesson ? NextStep( page ) : -1;
	idStr paragraph;
	auto flushParagraph = [&]()
	{
		if( paragraph.Length() > 0 )
		{
			DrawInline( paragraph, ImGui::GetFont(), textColor );
			ImGui::Dummy( ImVec2( 0.0f, 4.0f * UI::Scale() ) );
			paragraph.Clear();
		}
	};

	for( int i = 0; i < page.lines.Num(); i++ )
	{
		const idStr& line = page.lines[i];
		if( line.Length() == 0 )
		{
			flushParagraph();
			continue;
		}
		if( line.Cmpn( "@@step ", 7 ) == 0 )
		{
			flushParagraph();
			const int index = atoi( line.c_str() + 7 );
			DrawStep( page, index, index == next );
			continue;
		}
		if( line.Cmpn( "### ", 4 ) == 0 )
		{
			flushParagraph();
			ImGui::Dummy( ImVec2( 0.0f, 2.0f * UI::Scale() ) );
			UI::SectionLabel( line.c_str() + 4 );
			continue;
		}
		if( line.Cmpn( "## ", 3 ) == 0 || line.Cmpn( "# ", 2 ) == 0 )
		{
			flushParagraph();
			ImGui::Dummy( ImVec2( 0.0f, 6.0f * UI::Scale() ) );
			DrawInline( line.c_str() + ( line[1] == '#' ? 3 : 2 ), UI::Font( UI::FONT_BOLD ), UI::AccentU32() );
			continue;
		}
		if( line.Cmpn( "- ", 2 ) == 0 || line.Cmpn( "* ", 2 ) == 0 )
		{
			flushParagraph();
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddText( ImVec2( at.x + 4.0f * UI::Scale(), at.y ), UI::AccentU32(), "\xe2\x80\xa2" );
			DrawInline( line.c_str() + 2, ImGui::GetFont(), textColor, 16.0f * UI::Scale() );
			continue;
		}
		if( line[0] >= '0' && line[0] <= '9' && line.Find( ". " ) > 0 && line.Find( ". " ) < 4 )
		{
			flushParagraph();
			const int dot = line.Find( ". " );
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddText( UI::Font( UI::FONT_BOLD ), ImGui::GetFontSize(), at, UI::AccentU32(), line.Left( dot + 1 ).c_str() );
			DrawInline( line.c_str() + dot + 2, ImGui::GetFont(), textColor, 20.0f * UI::Scale() );
			continue;
		}
		if( line.Cmpn( "> ", 2 ) == 0 )
		{
			flushParagraph();
			// a callout: a tip set apart with an accent bar
			const ImVec2 top = ImGui::GetCursorScreenPos();
			DrawInline( line.c_str() + 2, ImGui::GetFont(), ImGui::ColorConvertFloat4ToU32( UI::Muted() ), 14.0f * UI::Scale() );
			const float bottom = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;
			ImGui::GetWindowDrawList()->AddRectFilled( ImVec2( top.x + 2.0f * UI::Scale(), top.y ), ImVec2( top.x + 5.0f * UI::Scale(), bottom ), UI::AccentU32( 0.8f ), 2.0f );
			continue;
		}
		if( paragraph.Length() > 0 )
		{
			paragraph += " ";
		}
		paragraph += line;
	}
	flushParagraph();
}

/*
===============================================================================

	The Help panel

===============================================================================
*/

void Open( const char* page )
{
	Load();
	visible = true;
	focusRequest = true;
	const idStr target = ( page != NULL ) ? page : "";
	page_t* found = FindPage( target );
	const idStr id = found != NULL ? found->id : idStr( "" );
	if( id != current )
	{
		history.Append( current );
		current = id;
	}
	if( found != NULL && found->lesson && editor_lesson.GetString() != found->id )
	{
		editor_lesson.SetString( found->id );	// opening a lesson starts it
		completedTime = 0;
	}
	Event( "help.open" );
}

struct folderName_t
{
	const char* folder;
	const char* title;
};
static const folderName_t folderNames[] =
{
	{ "lessons",	"Lessons" },
	{ "basics",		"Getting around" },
	{ "tools",		"Tools" },
	{ "things",		"Things to place" },
	{ "pieces",		"Pieces" },
	{ "play",		"Playing and building" },
	{ "panels",		"Panels" },
	{ "cards",		"Inspector cards" },
};

static int ComparePages( const int* a, const int* b )
{
	const page_t& pa = pages[*a];
	const page_t& pb = pages[*b];
	if( pa.order != pb.order )
	{
		return pa.order - pb.order;
	}
	return pa.title.Icmp( pb.title );
}

static void PageRow( const page_t& page )
{
	idStr label = page.title;
	if( page.lesson )
	{
		const int done = StepsDone( page );
		label = va( "%s  %s##%s", done == page.steps.Num() && done > 0 ? ICON_OK : ICON_LESSONS, page.title.c_str(), page.id.c_str() );
	}
	else
	{
		label = va( "%s##%s", page.title.c_str(), page.id.c_str() );
	}
	const float countWidth = page.lesson ? 44.0f * UI::Scale() : 0.0f;
	if( ImGui::Selectable( label, false, ImGuiSelectableFlags_None, ImVec2( ImGui::GetContentRegionAvail().x - countWidth, 0.0f ) ) )
	{
		Open( page.id );
	}
	if( page.lesson )
	{
		ImGui::SameLine( ImGui::GetContentRegionMax().x - countWidth + 6.0f * UI::Scale() );
		ImGui::TextDisabled( "%d/%d", StepsDone( page ), page.steps.Num() );
	}
}

static void DrawContents()
{
	ImGui::PushFont( UI::Font( UI::FONT_TITLE ) );
	ImGui::TextUnformatted( "Help" );
	ImGui::PopFont();
	UI::Hint( "Pages for every part of the editor, and lessons that tick themselves off as you go. The ? buttons around the editor open the page for what they sit on." );
	ImGui::Spacing();

	// the folders in their usual order, then any a project adds
	idStrList folders;
	for( int i = 0; i < ( int )( sizeof( folderNames ) / sizeof( folderNames[0] ) ); i++ )
	{
		folders.Append( folderNames[i].folder );
	}
	for( int i = 0; i < pages.Num(); i++ )
	{
		if( pages[i].folder.Length() > 0 )
		{
			folders.AddUnique( pages[i].folder );
		}
	}

	for( int f = 0; f < folders.Num(); f++ )
	{
		idList<int> inFolder;
		for( int i = 0; i < pages.Num(); i++ )
		{
			if( pages[i].folder.Icmp( folders[f] ) == 0 )
			{
				inFolder.Append( i );
			}
		}
		if( inFolder.Num() == 0 )
		{
			continue;
		}
		for( int a = 1; a < inFolder.Num(); a++ )
		{
			for( int b = a; b > 0 && ComparePages( &inFolder[b], &inFolder[b - 1] ) < 0; b-- )
			{
				const int t = inFolder[b];
				inFolder[b] = inFolder[b - 1];
				inFolder[b - 1] = t;
			}
		}
		idStr title = folders[f];
		for( int k = 0; k < ( int )( sizeof( folderNames ) / sizeof( folderNames[0] ) ); k++ )
		{
			if( title.Icmp( folderNames[k].folder ) == 0 )
			{
				title = folderNames[k].title;
			}
		}
		if( f >= ( int )( sizeof( folderNames ) / sizeof( folderNames[0] ) ) && title.Length() > 0 )
		{
			title[0] = idStr::ToUpper( title[0] );
		}
		ImGui::Spacing();
		UI::SectionLabel( title );
		for( int i = 0; i < inFolder.Num(); i++ )
		{
			PageRow( pages[inFolder[i]] );
		}
	}
}

static void DrawSearchResults()
{
	idStr needle = searchText;
	needle.ToLower();
	int found = 0;
	for( int i = 0; i < pages.Num(); i++ )
	{
		if( pages[i].plain.Find( needle ) >= 0 )
		{
			PageRow( pages[i] );
			found++;
		}
	}
	if( found == 0 )
	{
		UI::Hint( "Nothing found. Try another word." );
	}
}

static void DrawPage( page_t& page )
{
	ImGui::PushFont( UI::Font( UI::FONT_TITLE ) );
	ImGui::TextWrapped( "%s", page.title.c_str() );
	ImGui::PopFont();

	if( page.lesson )
	{
		const int done = StepsDone( page );
		const bool active = editor_lesson.GetString() == page.id;
		ImGui::PushStyleColor( ImGuiCol_PlotHistogram, UI::Accent() );
		ImGui::ProgressBar( page.steps.Num() > 0 ? done / ( float )page.steps.Num() : 0.0f, ImVec2( -FLT_MIN, 6.0f * UI::Scale() ), "" );
		ImGui::PopStyleColor();
		ImGui::TextDisabled( "%d of %d done%s", done, page.steps.Num(), active ? "" : "  (not under way)" );
		ImGui::SameLine();
		if( !active && ImGui::SmallButton( ICON_PLAY "  Carry on with this one" ) )
		{
			editor_lesson.SetString( page.id );
		}
		else if( active && done > 0 )
		{
			if( ImGui::SmallButton( ICON_UNDO "  Start over" ) )
			{
				ResetLesson( page );
			}
		}
		ImGui::Spacing();
	}
	else
	{
		ImGui::Spacing();
	}

	DrawBody( page );

	if( page.lesson && NextStep( page ) < 0 && page.steps.Num() > 0 )
	{
		ImGui::Spacing();
		ImGui::TextColored( UI::Good(), ICON_OK "  Lesson done. Well done!" );
		page_t* next = FindPage( page.next );
		if( next != NULL )
		{
			if( UI::ColorButton( va( "Next lesson: %s", next->title.c_str() ), UI::Accent() ) )
			{
				Open( next->id );
			}
		}
	}
}

void Draw()
{
	Load();
	if( focusRequest )
	{
		focusRequest = false;
		ImGui::SetNextWindowFocus();
	}
	ImGui::SetNextWindowSize( ImVec2( 380.0f * UI::Scale(), 520.0f * UI::Scale() ), ImGuiCond_FirstUseEver );
	panelShown = ImGui::Begin( WINDOW_HELP, &visible );
	if( !panelShown )
	{
		ImGui::End();
		return;
	}

	// back, contents, search
	ImGui::BeginDisabled( history.Num() == 0 );
	if( UI::IconButton( "back", ICON_TURN_LEFT, "Back" ) && history.Num() > 0 )
	{
		current = history[history.Num() - 1];
		history.RemoveIndex( history.Num() - 1 );
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if( UI::IconButton( "contents", ICON_BOOK, "Contents", current.Length() == 0 && searchText[0] == '\0' ) )
	{
		searchText[0] = '\0';
		if( current.Length() > 0 )
		{
			history.Append( current );
			current.Clear();
		}
	}
	ImGui::SameLine();
	UI::SearchField( "##helpsearch", searchText, sizeof( searchText ), "Search help" );
	ImGui::Separator();

	ImGui::BeginChild( "helppage", ImVec2( 0, 0 ), false );
	if( searchText[0] != '\0' )
	{
		DrawSearchResults();
	}
	else
	{
		page_t* page = FindPage( current );
		if( page != NULL )
		{
			DrawPage( *page );
		}
		else
		{
			DrawContents();
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

void DrawLessonStrip( const ImVec2& topRight )
{
	page_t* lesson = ActiveLesson();
	if( lesson == NULL )
	{
		return;
	}
	const int next = NextStep( *lesson );
	const bool justDone = next < 0 && completedTime > 0 && Sys_Milliseconds() - completedTime < 8000;
	if( ( next < 0 && !justDone ) || ( visible && panelShown ) )
	{
		return;
	}

	const float pad = 10.0f * UI::Scale();
	ImGui::SetNextWindowPos( ImVec2( topRight.x - pad, topRight.y + pad ), ImGuiCond_Always, ImVec2( 1.0f, 0.0f ) );
	ImGui::SetNextWindowSize( ImVec2( 300.0f * UI::Scale(), 0.0f ) );
	ImGui::SetNextWindowBgAlpha( 0.94f );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 10.0f * UI::Scale() );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 10.0f * UI::Scale(), 8.0f * UI::Scale() ) );
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
								   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
	if( ImGui::Begin( "##lessonstrip", NULL, flags ) )
	{
		ImGui::TextColored( UI::Accent(), ICON_LESSONS );
		ImGui::SameLine();
		ImGui::PushFont( UI::Font( UI::FONT_BOLD ) );
		ImGui::TextUnformatted( lesson->title );
		ImGui::PopFont();
		ImGui::SameLine();
		ImGui::TextDisabled( "%d/%d", StepsDone( *lesson ), lesson->steps.Num() );
		if( justDone )
		{
			ImGui::TextColored( UI::Good(), ICON_OK "  Lesson done!" );
		}
		else
		{
			ImGui::PushTextWrapPos( 0.0f );
			ImGui::TextUnformatted( PlainText( lesson->steps[next].text ) );
			ImGui::PopTextWrapPos();
			if( lesson->steps[next].show.Length() > 0 && showMe != NULL )
			{
				if( ImGui::SmallButton( ICON_EYE "  Show me" ) )
				{
					showMe( lesson->steps[next].show );
				}
				ImGui::SameLine();
			}
			if( ImGui::SmallButton( ICON_BOOK "  Open" ) )
			{
				Open( lesson->id );
			}
			ImGui::SameLine();
			if( ImGui::SmallButton( ICON_CLOSE "  Stop" ) )
			{
				editor_lesson.SetString( "" );
			}
			UI::Tip( "Stop this lesson (it remembers where you got to)" );
		}
	}
	ImGui::End();
	ImGui::PopStyleVar( 2 );
}

} // namespace Help
} // namespace DaedalusEditor

#endif // DAEDALUS_EDITOR
