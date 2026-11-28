/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (c) 2014-2015 Omar Cornut and ImGui contributors
Copyright (C) 2015 Daniel Gibson
Copyright (C) 2016-2023 Robert Beckebans

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
#include "precompiled.h"
#pragma hdrstop

#include "BFGimgui.h"
#include "ImGuizmo.h"
#include "renderer/RenderCommon.h"
#include "renderer/RenderBackend.h"
#include "../tools/imgui/editor/EditorShell.h"
#if defined( DAEDALUS_EDITOR )
	#include "../tools/imgui/editor/EditorTheme.h"
#endif


idCVar imgui_showDemoWindow( "imgui_showDemoWindow", "0", CVAR_GUI | CVAR_BOOL, "show big ImGui demo window" );

// our custom ImGui functions from BFGimgui.h

// like DragFloat3(), but with "X: ", "Y: " or "Z: " prepended to each display_format, for vectors
// if !ignoreLabelWidth, it makes sure the label also fits into the current item width.
//    note that this screws up alignment with consecutive "value+label widgets" (like Drag* or ColorEdit*)
bool ImGui::DragVec3( const char* label, idVec3& v, float v_speed,
					  float v_min, float v_max, const char* display_format, float power, bool ignoreLabelWidth )
{
	bool value_changed = false;
	ImGui::BeginGroup();
	ImGui::PushID( label );

	ImGuiStyle& style = ImGui::GetStyle();
	float wholeWidth = ImGui::CalcItemWidth() - 2.0f * style.ItemSpacing.x;
	float spacing = style.ItemInnerSpacing.x;
	float labelWidth = ignoreLabelWidth ? 0.0f : ( ImGui::CalcTextSize( label, NULL, true ).x + spacing );
	float coordWidth = ( wholeWidth - labelWidth - 2.0f * spacing ) * ( 1.0f / 3.0f ); // width of one x/y/z dragfloat

	ImGui::PushItemWidth( coordWidth );
	for( int i = 0; i < 3; i++ )
	{
		ImGui::PushID( i );
		char format[64];
		idStr::snPrintf( format, sizeof( format ), "%c: %s", "XYZ"[i], display_format );
		value_changed |= ImGui::DragFloat( "##v", &v[i], v_speed, v_min, v_max, format, power );

		ImGui::PopID();
		ImGui::SameLine( 0.0f, spacing );
	}
	ImGui::PopItemWidth();
	ImGui::PopID();

	const char* labelEnd = strstr( label, "##" );
	ImGui::TextUnformatted( label, labelEnd );

	ImGui::EndGroup();

	return value_changed;
}

// shortcut for DragXYZ with ignorLabelWidth = false
// very similar, but adjusts width to width of label to make sure it's not cut off
// sometimes useful, but might not align with consecutive "value+label widgets" (like Drag* or ColorEdit*)
bool ImGui::DragVec3fitLabel( const char* label, idVec3& v, float v_speed,
							  float v_min, float v_max, const char* display_format, float power )
{
	return ImGui::DragVec3( label, v, v_speed, v_min, v_max, display_format, power, false );
}

// the ImGui hooks to integrate it into the engine



namespace ImGuiHook
{

// Daedalus: panels in windows of their own (BFGimguiViewports.cpp)
bool g_FrameRendered = false;				// an ImGui frame was rendered since the last RenderPlatformWindows
void InitViewports();
void ViewportsNewFrame( bool pollMouse, ImVec2& mousePos );


namespace
{

bool	g_IsInit = false;
double	g_Time = 0.0f;
bool	g_MousePressed[5] = { false, false, false, false, false };
float	g_MouseWheel = 0.0f;
ImVec2	g_MousePos = ImVec2( -1.0f, -1.0f ); //{-1.0f, -1.0f};
ImVec2	g_DisplaySize = ImVec2( 0.0f, 0.0f ); //{0.0f, 0.0f};
bool	g_haveNewFrame = false;
bool	g_MousePosForced = false;			// Daedalus: SetMousePos was called; use that rather than polling the mouse
bool	g_insideRender = false;

bool HandleKeyEvent( const sysEvent_t& keyEvent )
{
	assert( keyEvent.evType == SE_KEY );

	keyNum_t keyNum = static_cast<keyNum_t>( keyEvent.evValue );
	bool pressed = keyEvent.evValue2 > 0;

	ImGuiIO& io = ImGui::GetIO();

	if( keyNum == K_MOUSE2 )
	{
		// RB: allow navigation like in a level editor
		g_MousePressed[1] = pressed;

		if( ImGuiTools::AreEditorsActive() )
		{
			ImGuiTools::SetReleaseToolMouse( !pressed );
		}
		//common->Printf( "mouse2 pressed %d\n", int( pressed ) );

		return true;
	}

	if( g_MousePressed[1] && !DaedalusEditor::IsEditing() )
	{
		// RB: ignore everything as long right mouse button is pressed
		// Daedalus: except in the editor, which flies its camera with WASD while it's held
		return false;
	}

	if( keyNum < K_JOY1 )
	{
		// keyboard input as direct input scancodes
		io.KeysDown[keyNum] = pressed;

		// Daedalus: track the modifiers from these events. usercmdGen never sees them
		// once ImGui has taken the keyboard, so its key states stay "up".
		io.KeyAlt = io.KeysDown[K_LALT] || io.KeysDown[K_RALT];
		io.KeyCtrl = io.KeysDown[K_LCTRL] || io.KeysDown[K_RCTRL];
		io.KeyShift = io.KeysDown[K_LSHIFT] || io.KeysDown[K_RSHIFT];

		return true;
	}
	else if( keyNum >= K_MOUSE1 && keyNum <= K_MOUSE5 )
	{
		int buttonIdx = keyNum - K_MOUSE1;

		// K_MOUSE* are contiguous, so they can be used as indexes into imgui's
		// g_MousePressed[] - imgui even uses the same order (left, right, middle, X1, X2)
		g_MousePressed[buttonIdx] = pressed;

		return true; // let's pretend we also handle mouse up events
	}

	return false;
}

// Gross hack. I'm sorry.
// sets the kb-layout specific keys in the keymap
void FillCharKeys( int* keyMap )
{
	// Daedalus: map every letter, not just the clipboard and undo ones, so editor shortcuts
	// (Ctrl+S, Ctrl+D, F, W/E/R...) work. Scancodes first, as defaults for keys whose names
	// aren't found below.
	static const keyNum_t letters[26] =
	{
		K_A, K_B, K_C, K_D, K_E, K_F, K_G, K_H, K_I, K_J, K_K, K_L, K_M,
		K_N, K_O, K_P, K_Q, K_R, K_S, K_T, K_U, K_V, K_W, K_X, K_Y, K_Z
	};
	for( int i = 0; i < 26; i++ )
	{
		keyMap[ImGuiKey_A + i] = letters[i];
	}
	keyMap[ImGuiKey_Space] = K_SPACE;
	keyMap[ImGuiKey_LeftBracket] = K_LBRACKET;
	keyMap[ImGuiKey_RightBracket] = K_RBRACKET;
	keyMap[ImGuiKey_Comma] = K_COMMA;
	keyMap[ImGuiKey_Period] = K_PERIOD;
	keyMap[ImGuiKey_Slash] = K_SLASH;
	keyMap[ImGuiKey_Minus] = K_MINUS;
	keyMap[ImGuiKey_Equal] = K_EQUALS;
	keyMap[ImGuiKey_GraveAccent] = K_GRAVE;
	keyMap[ImGuiKey_0] = K_0;
	for( int i = 0; i < 9; i++ )
	{
		keyMap[ImGuiKey_1 + i] = K_1 + i;
	}
	for( int i = 0; i < 12; i++ )
	{
		keyMap[ImGuiKey_F1 + i] = ( i < 10 ) ? K_F1 + i : ( i == 10 ? K_F11 : K_F12 );
	}

	// then the keyboard layout: whichever key is labelled with a letter gets that letter
	for( int k = K_1; k < K_RSHIFT; ++k )
	{
		const char* kn = idKeyInput::LocalizedKeyName( ( keyNum_t )k );
		if( kn[0] == '\0' || kn[1] != '\0' || kn[0] == '?' )
		{
			// if the key wasn't found or the name has more than one char,
			// it's not what we're looking for.
			continue;
		}
		const char c = idStr::ToLower( kn[0] );
		if( c >= 'a' && c <= 'z' )
		{
			keyMap[ImGuiKey_A + ( c - 'a' )] = k;
		}
	}
}

// Sys_GetClipboardData() expects that you Mem_Free() its returned data
// ImGui can't do that, of course, so copy it into a static buffer here,
// Mem_Free() and return the copy
const char* GetClipboardText( void* )
{
	char* txt = Sys_GetClipboardData();
	if( txt == NULL )
	{
		return NULL;
	}

	static idStr clipboardBuf;
	clipboardBuf = txt;

	Mem_Free( txt );

	return clipboardBuf.c_str();
}

void SetClipboardText( void*, const char* text )
{
	Sys_SetClipboardData( text );
}


bool ShowWindows()
{
	// Daedalus: the editor shell is always drawn in editor mode (also while playing, for the Stop button)
	return ( DaedalusEditor::IsEditorMode() || ImGuiTools::AreEditorsActive() || imgui_showDemoWindow.GetBool() || com_showFPS.GetInteger() > 1 );
}

bool UseInput()
{
	return ( ImGuiTools::ReleaseMouseForTools() || imgui_showDemoWindow.GetBool() );
}

// Daedalus: while a level is played from the editor the mouse belongs to the game.
// ImGui still draws the menu bar then, so it must not see the game's clicks: the cursor
// is hidden but ImGui would keep its last position (usually right on the Play/Stop button).
static bool PlayingFromEditor()
{
	return DaedalusEditor::IsEditorMode() && !DaedalusEditor::IsEditing() && !ImGuiTools::AreEditorsActive() && !imgui_showDemoWindow.GetBool();
}

void StyleGruvboxDark()
{
	auto& style = ImGui::GetStyle();
#if 0
	style.ChildRounding = 0;
	style.GrabRounding = 0;
	style.FrameRounding = 0;
	style.PopupRounding = 0;
	style.ScrollbarRounding = 0;
	style.TabRounding = 0;
	style.WindowRounding = 0;
	style.FramePadding = {4, 4};
#endif
	style.WindowTitleAlign = {0.5, 0.5};

	ImVec4* colors = ImGui::GetStyle().Colors;
	// Updated to use IM_COL32 for more precise colors and to add table colors (1.80 feature)
	colors[ImGuiCol_Text] = ImColor{IM_COL32( 0xeb, 0xdb, 0xb2, 0xFF )};
	colors[ImGuiCol_TextDisabled] = ImColor{IM_COL32( 0x92, 0x83, 0x74, 0xFF )};
	colors[ImGuiCol_WindowBg] = ImColor{IM_COL32( 0x1d, 0x20, 0x21, 0xF0 )};
	colors[ImGuiCol_ChildBg] = ImColor{IM_COL32( 0x1d, 0x20, 0x21, 0xFF )};
	colors[ImGuiCol_PopupBg] = ImColor{IM_COL32( 0x1d, 0x20, 0x21, 0xF0 )};
	colors[ImGuiCol_Border] = ImColor{IM_COL32( 0x1d, 0x20, 0x21, 0xFF )};
	colors[ImGuiCol_BorderShadow] = ImColor{0};
	colors[ImGuiCol_FrameBg] = ImColor{IM_COL32( 0x3c, 0x38, 0x36, 0x90 )};
	colors[ImGuiCol_FrameBgHovered] = ImColor{IM_COL32( 0x50, 0x49, 0x45, 0xFF )};
	colors[ImGuiCol_FrameBgActive] = ImColor{IM_COL32( 0x66, 0x5c, 0x54, 0xA8 )};
	colors[ImGuiCol_TitleBg] = ImColor{IM_COL32( 0xd6, 0x5d, 0x0e, 0xFF )};
	colors[ImGuiCol_TitleBgActive] = ImColor{IM_COL32( 0xfe, 0x80, 0x19, 0xFF )};
	colors[ImGuiCol_TitleBgCollapsed] = ImColor{IM_COL32( 0xd6, 0x5d, 0x0e, 0x9C )};
	colors[ImGuiCol_MenuBarBg] = ImColor{IM_COL32( 0x28, 0x28, 0x28, 0xF0 )};
	colors[ImGuiCol_ScrollbarBg] = ImColor{IM_COL32( 0x00, 0x00, 0x00, 0x28 )};
	colors[ImGuiCol_ScrollbarGrab] = ImColor{IM_COL32( 0x3c, 0x38, 0x36, 0xFF )};
	colors[ImGuiCol_ScrollbarGrabHovered] = ImColor{IM_COL32( 0x50, 0x49, 0x45, 0xFF )};
	colors[ImGuiCol_ScrollbarGrabActive] = ImColor{IM_COL32( 0x66, 0x5c, 0x54, 0xFF )};
	colors[ImGuiCol_CheckMark] = ImColor{IM_COL32( 0xd6, 0x5d, 0x0e, 0x9E )};
	colors[ImGuiCol_SliderGrab] = ImColor{IM_COL32( 0xd6, 0x5d, 0x0e, 0x70 )};
	colors[ImGuiCol_SliderGrabActive] = ImColor{IM_COL32( 0xfe, 0x80, 0x19, 0xFF )};
	colors[ImGuiCol_Button] = ImColor{IM_COL32( 0xd6, 0x5d, 0x0e, 0x66 )};
	colors[ImGuiCol_ButtonHovered] = ImColor{IM_COL32( 0xfe, 0x80, 0x19, 0x9E )};
	colors[ImGuiCol_ButtonActive] = ImColor{IM_COL32( 0xfe, 0x80, 0x19, 0xFF )};
	colors[ImGuiCol_Header] = ImColor{IM_COL32( 0xd6, 0x5d, 0x0e, 0.4F )};
	colors[ImGuiCol_HeaderHovered] = ImColor{IM_COL32( 0xfe, 0x80, 0x19, 0xCC )};
	colors[ImGuiCol_HeaderActive] = ImColor{IM_COL32( 0xfe, 0x80, 0x19, 0xFF )};
	colors[ImGuiCol_Separator] = ImColor{IM_COL32( 0x66, 0x5c, 0x54, 0.50f )};
	colors[ImGuiCol_SeparatorHovered] = ImColor{IM_COL32( 0x50, 0x49, 0x45, 0.78f )};
	colors[ImGuiCol_SeparatorActive] = ImColor{IM_COL32( 0x66, 0x5c, 0x54, 0xFF )};
	colors[ImGuiCol_ResizeGrip] = ImColor{IM_COL32( 0xd6, 0x5d, 0x0e, 0x40 )};
	colors[ImGuiCol_ResizeGripHovered] = ImColor{IM_COL32( 0xfe, 0x80, 0x19, 0xAA )};
	colors[ImGuiCol_ResizeGripActive] = ImColor{IM_COL32( 0xfe, 0x80, 0x19, 0xF2 )};
	colors[ImGuiCol_Tab] = ImColor{IM_COL32( 0xd6, 0x5d, 0x0e, 0xD8 )};
	colors[ImGuiCol_TabHovered] = ImColor{IM_COL32( 0xfe, 0x80, 0x19, 0xCC )};
	colors[ImGuiCol_TabActive] = ImColor{IM_COL32( 0xfe, 0x80, 0x19, 0xFF )};
	colors[ImGuiCol_TabUnfocused] = ImColor{IM_COL32( 0x1d, 0x20, 0x21, 0.97f )};
	colors[ImGuiCol_TabUnfocusedActive] = ImColor{IM_COL32( 0x1d, 0x20, 0x21, 0xFF )};
	colors[ImGuiCol_PlotLines] = ImColor{IM_COL32( 0xd6, 0x5d, 0x0e, 0xFF )};
	colors[ImGuiCol_PlotLinesHovered] = ImColor{IM_COL32( 0xfe, 0x80, 0x19, 0xFF )};
	colors[ImGuiCol_PlotHistogram] = ImColor{IM_COL32( 0x98, 0x97, 0x1a, 0xFF )};
	colors[ImGuiCol_PlotHistogramHovered] = ImColor{IM_COL32( 0xb8, 0xbb, 0x26, 0xFF )};
	colors[ImGuiCol_TextSelectedBg] = ImColor{IM_COL32( 0x45, 0x85, 0x88, 0x59 )};
	colors[ImGuiCol_DragDropTarget] = ImColor{IM_COL32( 0x98, 0x97, 0x1a, 0.90f )};
	colors[ImGuiCol_TableHeaderBg] = ImColor{IM_COL32( 0x38, 0x3c, 0x36, 0xFF )};
	colors[ImGuiCol_TableBorderStrong] = ImColor{IM_COL32( 0x28, 0x28, 0x28, 0xFF )};
	colors[ImGuiCol_TableBorderLight] = ImColor{IM_COL32( 0x38, 0x3c, 0x36, 0xFF )};
	colors[ImGuiCol_TableRowBg] = ImColor {IM_COL32( 0x1d, 0x20, 0x21, 0xFF )};
	colors[ImGuiCol_TableRowBgAlt] = ImColor{IM_COL32( 0x28, 0x28, 0x28, 0xFF )};
	colors[ImGuiCol_TextSelectedBg] = ImColor { IM_COL32( 0x45, 0x85, 0x88, 0xF0 ) };
	colors[ImGuiCol_NavHighlight] = ImColor{IM_COL32( 0x83, 0xa5, 0x98, 0xFF )};
	colors[ImGuiCol_NavWindowingHighlight] = ImColor{IM_COL32( 0xfb, 0xf1, 0xc7, 0xB2 )};
	colors[ImGuiCol_NavWindowingDimBg] = ImColor{IM_COL32( 0x7c, 0x6f, 0x64, 0x33 )};
	colors[ImGuiCol_ModalWindowDimBg] = ImColor{IM_COL32( 0x1d, 0x20, 0x21, 0x59 )};
}

void StyleJungle()
{
	// based on BlackDevil style by Naeemullah1 from ImThemes

	ImVec4* colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Text]                   = ImVec4( 0.78f, 0.78f, 0.78f, 1.00f );
	colors[ImGuiCol_TextDisabled]           = ImVec4( 0.44f, 0.41f, 0.31f, 1.00f );
	colors[ImGuiCol_WindowBg]               = ImVec4( 0.12f, 0.14f, 0.16f, 0.87f );
	colors[ImGuiCol_ChildBg]                = ImVec4( 0.06f, 0.12f, 0.16f, 0.78f );
	colors[ImGuiCol_PopupBg]                = ImVec4( 0.08f, 0.08f, 0.08f, 0.78f );
	//colors[ImGuiCol_Border]                 = ImVec4(0.39f, 0.00f, 0.00f, 0.78f); the red is a bit too aggressive
	colors[ImGuiCol_Border]                 = ImVec4( 0.24f, 0.27f, 0.32f, 0.78f );
	colors[ImGuiCol_BorderShadow]           = ImVec4( 0.00f, 0.00f, 0.00f, 0.00f );
	colors[ImGuiCol_FrameBg]                = ImVec4( 0.06f, 0.12f, 0.16f, 0.78f );
	colors[ImGuiCol_FrameBgHovered]         = ImVec4( 0.12f, 0.24f, 0.35f, 0.78f );
	colors[ImGuiCol_FrameBgActive]          = ImVec4( 0.35f, 0.35f, 0.12f, 0.78f );
	colors[ImGuiCol_TitleBg]                = ImVec4( 0.06f, 0.12f, 0.16f, 0.78f );
	colors[ImGuiCol_TitleBgActive]          = ImVec4( 0.06f, 0.12f, 0.16f, 0.78f );
	colors[ImGuiCol_TitleBgCollapsed]       = ImVec4( 0.06f, 0.12f, 0.16f, 0.20f );
	colors[ImGuiCol_MenuBarBg]              = ImVec4( 0.08f, 0.08f, 0.08f, 0.78f );
	colors[ImGuiCol_ScrollbarBg]            = ImVec4( 0.06f, 0.12f, 0.16f, 0.78f );
	colors[ImGuiCol_ScrollbarGrab]          = ImVec4( 0.12f, 0.35f, 0.24f, 0.78f );
	colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4( 0.12f, 0.35f, 0.35f, 0.78f );
	colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4( 0.12f, 0.59f, 0.24f, 0.78f );
	colors[ImGuiCol_CheckMark]              = ImVec4( 0.12f, 0.59f, 0.24f, 0.78f );
	colors[ImGuiCol_SliderGrab]             = ImVec4( 0.12f, 0.35f, 0.24f, 0.78f );
	colors[ImGuiCol_SliderGrabActive]       = ImVec4( 0.12f, 0.59f, 0.24f, 0.78f );
	colors[ImGuiCol_Button]                 = ImVec4( 0.35f, 0.35f, 0.12f, 0.78f );
	colors[ImGuiCol_ButtonHovered]          = ImVec4( 0.35f, 0.47f, 0.24f, 0.78f );
	colors[ImGuiCol_ButtonActive]           = ImVec4( 0.59f, 0.35f, 0.24f, 0.78f );
	colors[ImGuiCol_Header]                 = ImVec4( 0.06f, 0.12f, 0.16f, 0.78f );
	colors[ImGuiCol_HeaderHovered]          = ImVec4( 0.12f, 0.35f, 0.35f, 0.78f );
	colors[ImGuiCol_HeaderActive]           = ImVec4( 0.12f, 0.59f, 0.24f, 0.78f );
	colors[ImGuiCol_Separator]              = ImVec4( 0.35f, 0.35f, 0.24f, 0.78f );
	colors[ImGuiCol_SeparatorHovered]       = ImVec4( 0.12f, 0.35f, 0.35f, 0.78f );
	colors[ImGuiCol_SeparatorActive]        = ImVec4( 0.59f, 0.35f, 0.24f, 0.78f );
	colors[ImGuiCol_ResizeGrip]             = ImVec4( 0.06f, 0.12f, 0.16f, 0.78f );
	colors[ImGuiCol_ResizeGripHovered]      = ImVec4( 0.59f, 0.35f, 0.35f, 0.78f );
	colors[ImGuiCol_ResizeGripActive]       = ImVec4( 0.59f, 0.24f, 0.24f, 0.78f );
	colors[ImGuiCol_Tab]                    = ImVec4( 0.35f, 0.35f, 0.12f, 0.78f );
	colors[ImGuiCol_TabHovered]             = ImVec4( 0.35f, 0.47f, 0.24f, 0.78f );
	colors[ImGuiCol_TabActive]              = ImVec4( 0.59f, 0.35f, 0.24f, 0.78f );
	colors[ImGuiCol_TabUnfocused]           = ImVec4( 0.06f, 0.12f, 0.16f, 0.78f );
	colors[ImGuiCol_TabUnfocusedActive]     = ImVec4( 0.59f, 0.35f, 0.35f, 0.78f );
	colors[ImGuiCol_DockingPreview]         = ImVec4( 0.26f, 0.59f, 0.98f, 0.70f );
	colors[ImGuiCol_DockingEmptyBg]         = ImVec4( 0.20f, 0.20f, 0.20f, 1.00f );
	colors[ImGuiCol_PlotLines]              = ImVec4( 0.39f, 0.78f, 0.39f, 0.78f );
	colors[ImGuiCol_PlotLinesHovered]       = ImVec4( 1.00f, 0.43f, 0.35f, 0.78f );
	colors[ImGuiCol_PlotHistogram]          = ImVec4( 0.00f, 0.35f, 0.39f, 0.78f );
	colors[ImGuiCol_PlotHistogramHovered]   = ImVec4( 0.20f, 0.59f, 0.59f, 0.78f );
	colors[ImGuiCol_TableHeaderBg]          = ImVec4( 0.19f, 0.19f, 0.20f, 0.78f );
	colors[ImGuiCol_TableBorderStrong]      = ImVec4( 0.31f, 0.31f, 0.35f, 0.78f );
	colors[ImGuiCol_TableBorderLight]       = ImVec4( 0.23f, 0.23f, 0.25f, 0.78f );
	colors[ImGuiCol_TableRowBg]             = ImVec4( 0.00f, 0.00f, 0.00f, 0.78f );
	colors[ImGuiCol_TableRowBgAlt]          = ImVec4( 1.00f, 1.00f, 1.00f, 0.06f );
	colors[ImGuiCol_TextSelectedBg]         = ImVec4( 0.39f, 0.35f, 0.39f, 0.39f );
	colors[ImGuiCol_DragDropTarget]         = ImVec4( 1.00f, 1.00f, 0.00f, 0.90f );
	colors[ImGuiCol_NavHighlight]           = ImVec4( 0.26f, 0.59f, 0.98f, 1.00f );
	colors[ImGuiCol_NavWindowingHighlight]  = ImVec4( 1.00f, 1.00f, 1.00f, 0.70f );
	colors[ImGuiCol_NavWindowingDimBg]      = ImVec4( 0.80f, 0.80f, 0.80f, 0.20f );
	colors[ImGuiCol_ModalWindowDimBg]       = ImVec4( 0.80f, 0.80f, 0.80f, 0.35f );
}

void StyleBioshock()
{
	// based on my UE5 Military theme
	auto& style = ImGui::GetStyle();
	style.ChildRounding = 4.0f;
	style.FrameRounding = 4.0f;
	style.ScrollbarRounding = 4.0f;
	style.GrabRounding = 4.0f;
	style.FrameBorderSize = 1.0f;

	ImVec4* colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Text]                   = ImVec4( 0.75f, 0.75f, 0.75f, 1.00f );
	colors[ImGuiCol_TextDisabled]           = ImVec4( 0.59f, 0.70f, 0.74f, 1.00f );
	colors[ImGuiCol_WindowBg]               = ImVec4( 0.14f, 0.16f, 0.18f, 0.94f );
	colors[ImGuiCol_ChildBg]                = ImVec4( 0.17f, 0.17f, 0.21f, 1.00f );
	colors[ImGuiCol_PopupBg]                = ImVec4( 0.52f, 0.52f, 0.42f, 1.00f );
	colors[ImGuiCol_Border]                 = ImVec4( 0.52f, 0.52f, 0.42f, 1.00f );
	colors[ImGuiCol_BorderShadow]           = ImVec4( 0.12f, 0.12f, 0.15f, 1.00f );
	colors[ImGuiCol_FrameBg]                = ImVec4( 0.12f, 0.13f, 0.15f, 1.00f );
	colors[ImGuiCol_FrameBgHovered]         = ImVec4( 0.57f, 0.52f, 0.45f, 1.00f );
	colors[ImGuiCol_FrameBgActive]          = ImVec4( 0.82f, 0.78f, 0.66f, 1.00f );
	colors[ImGuiCol_TitleBg]                = ImVec4( 0.14f, 0.16f, 0.18f, 1.00f );
	colors[ImGuiCol_TitleBgActive]          = ImVec4( 0.00f, 0.47f, 0.84f, 1.00f );
	colors[ImGuiCol_TitleBgCollapsed]       = ImVec4( 0.12f, 0.12f, 0.15f, 0.51f );
	colors[ImGuiCol_MenuBarBg]              = ImVec4( 0.14f, 0.16f, 0.18f, 1.00f );
	colors[ImGuiCol_ScrollbarBg]            = ImVec4( 0.17f, 0.17f, 0.21f, 1.00f );
	colors[ImGuiCol_ScrollbarGrab]          = ImVec4( 0.57f, 0.52f, 0.45f, 1.00f );
	colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4( 0.82f, 0.78f, 0.66f, 1.00f );
	colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4( 0.82f, 0.78f, 0.66f, 1.00f );
	colors[ImGuiCol_CheckMark]              = ImVec4( 0.17f, 0.73f, 1.00f, 1.00f );
	colors[ImGuiCol_SliderGrab]             = ImVec4( 0.38f, 0.35f, 0.30f, 1.00f );
	colors[ImGuiCol_SliderGrabActive]       = ImVec4( 0.17f, 0.73f, 1.00f, 1.00f );
	colors[ImGuiCol_Button]                 = ImVec4( 0.38f, 0.37f, 0.30f, 1.00f );
	colors[ImGuiCol_ButtonHovered]          = ImVec4( 0.57f, 0.52f, 0.45f, 1.00f );
	colors[ImGuiCol_ButtonActive]           = ImVec4( 0.17f, 0.73f, 1.00f, 1.00f );
	colors[ImGuiCol_Header]                 = ImVec4( 0.26f, 0.25f, 0.21f, 1.00f );
	colors[ImGuiCol_HeaderHovered]          = ImVec4( 0.33f, 0.32f, 0.26f, 1.00f );
	colors[ImGuiCol_HeaderActive]           = ImVec4( 0.33f, 0.32f, 0.26f, 1.00f );
	colors[ImGuiCol_Separator]              = ImVec4( 0.17f, 0.17f, 0.21f, 1.00f );
	colors[ImGuiCol_SeparatorHovered]       = ImVec4( 0.17f, 0.17f, 0.21f, 1.00f );
	colors[ImGuiCol_SeparatorActive]        = ImVec4( 0.17f, 0.17f, 0.21f, 1.00f );
	colors[ImGuiCol_ResizeGrip]             = ImVec4( 0.26f, 0.59f, 0.98f, 0.20f );
	colors[ImGuiCol_ResizeGripHovered]      = ImVec4( 0.26f, 0.59f, 0.98f, 0.67f );
	colors[ImGuiCol_ResizeGripActive]       = ImVec4( 0.26f, 0.59f, 0.98f, 0.95f );
	colors[ImGuiCol_Tab]                    = ImVec4( 0.05f, 0.43f, 0.71f, 1.00f );
	colors[ImGuiCol_TabHovered]             = ImVec4( 0.33f, 0.32f, 0.26f, 1.00f );
	colors[ImGuiCol_TabActive]              = ImVec4( 0.13f, 0.41f, 0.61f, 1.00f );
	colors[ImGuiCol_TabUnfocused]           = ImVec4( 0.12f, 0.21f, 0.29f, 1.00f );
	colors[ImGuiCol_TabUnfocusedActive]     = ImVec4( 0.14f, 0.31f, 0.44f, 1.00f );
	colors[ImGuiCol_DockingPreview]         = ImVec4( 0.33f, 0.32f, 0.26f, 0.70f );
	colors[ImGuiCol_DockingEmptyBg]         = ImVec4( 0.20f, 0.20f, 0.20f, 1.00f );
	colors[ImGuiCol_PlotLines]              = ImVec4( 0.61f, 0.61f, 0.61f, 1.00f );
	colors[ImGuiCol_PlotLinesHovered]       = ImVec4( 1.00f, 0.43f, 0.35f, 1.00f );
	colors[ImGuiCol_PlotHistogram]          = ImVec4( 0.90f, 0.70f, 0.00f, 1.00f );
	colors[ImGuiCol_PlotHistogramHovered]   = ImVec4( 1.00f, 0.60f, 0.00f, 1.00f );
	colors[ImGuiCol_TableHeaderBg]          = ImVec4( 0.19f, 0.19f, 0.20f, 1.00f );
	colors[ImGuiCol_TableBorderStrong]      = ImVec4( 0.31f, 0.31f, 0.35f, 1.00f );
	colors[ImGuiCol_TableBorderLight]       = ImVec4( 0.23f, 0.23f, 0.25f, 1.00f );
	colors[ImGuiCol_TableRowBg]             = ImVec4( 0.00f, 0.00f, 0.00f, 0.00f );
	colors[ImGuiCol_TableRowBgAlt]          = ImVec4( 1.00f, 1.00f, 1.00f, 0.06f );
	colors[ImGuiCol_TextSelectedBg]         = ImVec4( 0.26f, 0.59f, 0.98f, 0.35f );
	colors[ImGuiCol_DragDropTarget]         = ImVec4( 1.00f, 1.00f, 0.00f, 0.90f );
	colors[ImGuiCol_NavHighlight]           = ImVec4( 0.26f, 0.59f, 0.98f, 1.00f );
	colors[ImGuiCol_NavWindowingHighlight]  = ImVec4( 1.00f, 1.00f, 1.00f, 0.70f );
	colors[ImGuiCol_NavWindowingDimBg]      = ImVec4( 0.80f, 0.80f, 0.80f, 0.20f );
	colors[ImGuiCol_ModalWindowDimBg]       = ImVec4( 0.80f, 0.80f, 0.80f, 0.35f );
}

using namespace rapidjson;

ImColor ParseColor( const Value& dColors, const char* colorName )
{
	idLexer src( LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );

	idStr str = dColors[colorName].GetString();
	src.LoadMemory( str, str.Length(), "UE5 Theme Color" );

	ImColor color = ImColor{IM_COL32( 0xFF, 0x0, 0x0, 0xFF )};

	if( !src.ExpectTokenString( "(" ) )
	{
		return color;
	}

	if( !src.ExpectTokenString( "R" ) )
	{
		return color;
	}

	if( !src.ExpectTokenString( "=" ) )
	{
		return color;
	}

	color.Value.x = src.ParseFloat();

	if( !src.ExpectTokenString( "," ) )
	{
		return color;
	}

	if( !src.ExpectTokenString( "G" ) )
	{
		return color;
	}

	if( !src.ExpectTokenString( "=" ) )
	{
		return color;
	}

	color.Value.y = src.ParseFloat();

	if( !src.ExpectTokenString( "," ) )
	{
		return color;
	}

	if( !src.ExpectTokenString( "B" ) )
	{
		return color;
	}

	if( !src.ExpectTokenString( "=" ) )
	{
		return color;
	}

	color.Value.z = src.ParseFloat();

	if( !src.ExpectTokenString( "," ) )
	{
		return color;
	}

	if( !src.ExpectTokenString( "A" ) )
	{
		return color;
	}

	if( !src.ExpectTokenString( "=" ) )
	{
		return color;
	}

	color.Value.w = src.ParseFloat();

	if( !src.ExpectTokenString( ")" ) )
	{
		return color;
	}

	// convert from linear RGB to sRGB
	float gamma = 1.0f / 2.2f;
	color.Value.x = idMath::Pow( color.Value.x, gamma );
	color.Value.y = idMath::Pow( color.Value.y, gamma );
	color.Value.z = idMath::Pow( color.Value.z, gamma );

	return color;
}

static inline ImVec4 ImLerp( const ImVec4& a, const ImVec4& b, float t )
{
	return ImVec4( a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, a.z + ( b.z - a.z ) * t, a.w + ( b.w - a.w ) * t );
}

static inline ImVec4 operator*( const ImVec4& lhs, const ImVec4& rhs )
{
	return ImVec4( lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z, lhs.w * rhs.w );
}

static inline ImVec4  operator+( const ImVec4& lhs, const ImVec4& rhs )
{
	return ImVec4( lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w );
}

void StyleUE5EditorTheme( const char* themeName )
{
	idFile* f = fileSystem->OpenFileReadMemory( themeName );
	if( f == NULL || f->Length() <= 0 )
	{
		return;
	}

	int fileLength = f->Length();
	const char* fileData = ( const char* )Mem_Alloc( fileLength, TAG_SWF );
	size_t fileSize = f->Read( ( byte* ) fileData, fileLength );
	delete f;

	rapidjson::Document d;
	d.Parse( fileData );

	auto& style = ImGui::GetStyle();

	//style.WindowRounding = 9.0f;
	style.ChildRounding = 4.0f;
	style.FrameRounding = 4.0f;
	style.ScrollbarRounding = 4.0f;
	style.GrabRounding = 4.0f;
	style.FrameBorderSize = 1.0f;

	//rapidjson::Document d;
	//d.Parse( MilitaryUE5Theme );

	assert( d.IsObject() );

	if( d.HasMember( "Version" ) )
	{
		Value& s = d["Version"];
		int version = s.GetInt();

		idLib::Printf( "version = %i", version );
	}

	Value& dColors = d["Colors"];

	ImVec4* colors = ImGui::GetStyle().Colors;

	colors[ImGuiCol_Text]					= ParseColor( dColors, "EStyleColor::Foreground" );
	colors[ImGuiCol_TextDisabled]			= ParseColor( dColors, "EStyleColor::SelectInactive" );
	colors[ImGuiCol_WindowBg]				= ParseColor( dColors, "EStyleColor::Background" ) * ImVec4( 1.0f, 1.0f, 1.0f, 0.94f );
	colors[ImGuiCol_ChildBg]				= ParseColor( dColors, "EStyleColor::Recessed" );
	colors[ImGuiCol_PopupBg]				= ParseColor( dColors, "EStyleColor::Notifications" );
	colors[ImGuiCol_Border]					= ParseColor( dColors, "EStyleColor::Notifications" );
	colors[ImGuiCol_BorderShadow]			= ParseColor( dColors, "EStyleColor::WindowBorder" );

	colors[ImGuiCol_FrameBg]				= ParseColor( dColors, "EStyleColor::Input" );
	colors[ImGuiCol_FrameBgHovered]			= ParseColor( dColors, "EStyleColor::Hover" );
	colors[ImGuiCol_FrameBgActive]			= ParseColor( dColors, "EStyleColor::Hover2" );

	colors[ImGuiCol_TitleBg]				= ParseColor( dColors, "EStyleColor::Title" );
	colors[ImGuiCol_TitleBgActive]			= ParseColor( dColors, "EStyleColor::Highlight" );
	colors[ImGuiCol_TitleBgCollapsed]		= ParseColor( dColors, "EStyleColor::Foldout" ) * ImVec4( 1.0f, 1.0f, 1.0f, 0.51f );

	colors[ImGuiCol_MenuBarBg]				= ParseColor( dColors, "EStyleColor::Title" );

	colors[ImGuiCol_ScrollbarBg]			= ParseColor( dColors, "EStyleColor::Recessed" );
	colors[ImGuiCol_ScrollbarGrab]			= ParseColor( dColors, "EStyleColor::Hover" );
	colors[ImGuiCol_ScrollbarGrabHovered]	= ParseColor( dColors, "EStyleColor::Hover2" );

	colors[ImGuiCol_ScrollbarGrabActive]	= colors[ImGuiCol_ScrollbarGrabHovered];
	colors[ImGuiCol_CheckMark]				= ParseColor( dColors, "EStyleColor::AccentBlue" );
	colors[ImGuiCol_SliderGrab]				= ParseColor( dColors, "EStyleColor::Dropdown" );
	colors[ImGuiCol_SliderGrabActive]		= ParseColor( dColors, "EStyleColor::Primary" );

	colors[ImGuiCol_Button]					= ParseColor( dColors, "EStyleColor::Secondary" );
	colors[ImGuiCol_ButtonHovered]			= ParseColor( dColors, "EStyleColor::Hover" );
	colors[ImGuiCol_ButtonActive]			= ParseColor( dColors, "EStyleColor::Primary" );

	colors[ImGuiCol_Header]					= ParseColor( dColors, "EStyleColor::Panel" );
	colors[ImGuiCol_HeaderHovered]			= ParseColor( dColors, "EStyleColor::Header" );
	colors[ImGuiCol_HeaderActive]			= ParseColor( dColors, "EStyleColor::Header" );

	colors[ImGuiCol_Separator]				= ParseColor( dColors, "EStyleColor::Recessed" );
	colors[ImGuiCol_SeparatorHovered]		= ParseColor( dColors, "EStyleColor::Recessed" );
	colors[ImGuiCol_SeparatorActive]		= ParseColor( dColors, "EStyleColor::Recessed" );

	colors[ImGuiCol_ResizeGrip]             = ImVec4( 0.26f, 0.59f, 0.98f, 0.20f );
	colors[ImGuiCol_ResizeGripHovered]      = ImVec4( 0.26f, 0.59f, 0.98f, 0.67f );
	colors[ImGuiCol_ResizeGripActive]       = ImVec4( 0.26f, 0.59f, 0.98f, 0.95f );
	colors[ImGuiCol_Tab]                    = ImLerp( colors[ImGuiCol_Header],       colors[ImGuiCol_TitleBgActive], 0.80f );
	colors[ImGuiCol_TabHovered]             = colors[ImGuiCol_HeaderHovered];
	colors[ImGuiCol_TabActive]              = ImLerp( colors[ImGuiCol_HeaderActive], colors[ImGuiCol_TitleBgActive], 0.60f );
	colors[ImGuiCol_TabUnfocused]           = ImLerp( colors[ImGuiCol_Tab],          colors[ImGuiCol_TitleBg], 0.80f );
	colors[ImGuiCol_TabUnfocusedActive]     = ImLerp( colors[ImGuiCol_TabActive],    colors[ImGuiCol_TitleBg], 0.40f );
	colors[ImGuiCol_DockingPreview]         = colors[ImGuiCol_HeaderActive] * ImVec4( 1.0f, 1.0f, 1.0f, 0.7f );
	colors[ImGuiCol_DockingEmptyBg]         = ImVec4( 0.20f, 0.20f, 0.20f, 1.00f );
	colors[ImGuiCol_PlotLines]              = ImVec4( 0.61f, 0.61f, 0.61f, 1.00f );
	colors[ImGuiCol_PlotLinesHovered]       = ImVec4( 1.00f, 0.43f, 0.35f, 1.00f );
	colors[ImGuiCol_PlotHistogram]          = ImVec4( 0.90f, 0.70f, 0.00f, 1.00f );
	colors[ImGuiCol_PlotHistogramHovered]   = ImVec4( 1.00f, 0.60f, 0.00f, 1.00f );
	colors[ImGuiCol_TableHeaderBg]          = ImVec4( 0.19f, 0.19f, 0.20f, 1.00f );
	colors[ImGuiCol_TableBorderStrong]      = ImVec4( 0.31f, 0.31f, 0.35f, 1.00f ); // Prefer using Alpha=1.0 here
	colors[ImGuiCol_TableBorderLight]       = ImVec4( 0.23f, 0.23f, 0.25f, 1.00f ); // Prefer using Alpha=1.0 here
	colors[ImGuiCol_TableRowBg]             = ImVec4( 0.00f, 0.00f, 0.00f, 0.00f );
	colors[ImGuiCol_TableRowBgAlt]          = ImVec4( 1.00f, 1.00f, 1.00f, 0.06f );
	colors[ImGuiCol_TextSelectedBg]         = ImVec4( 0.26f, 0.59f, 0.98f, 0.35f );
	colors[ImGuiCol_DragDropTarget]         = ImVec4( 1.00f, 1.00f, 0.00f, 0.90f );
	colors[ImGuiCol_NavHighlight]           = ImVec4( 0.26f, 0.59f, 0.98f, 1.00f );
	colors[ImGuiCol_NavWindowingHighlight]  = ImVec4( 1.00f, 1.00f, 1.00f, 0.70f );
	colors[ImGuiCol_NavWindowingDimBg]      = ImVec4( 0.80f, 0.80f, 0.80f, 0.20f );
	colors[ImGuiCol_ModalWindowDimBg]       = ImVec4( 0.80f, 0.80f, 0.80f, 0.35f );
}

} //anon namespace

bool Init( int windowWidth, int windowHeight )
{
	if( IsInitialized() )
	{
		Destroy();
	}

	IMGUI_CHECKVERSION();

	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();

	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	// Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array.
	io.KeyMap[ImGuiKey_Tab] = K_TAB;
	io.KeyMap[ImGuiKey_LeftArrow] = K_LEFTARROW;
	io.KeyMap[ImGuiKey_RightArrow] = K_RIGHTARROW;
	io.KeyMap[ImGuiKey_UpArrow] = K_UPARROW;
	io.KeyMap[ImGuiKey_DownArrow] = K_DOWNARROW;
	io.KeyMap[ImGuiKey_PageUp] = K_PGUP;
	io.KeyMap[ImGuiKey_PageDown] = K_PGDN;
	io.KeyMap[ImGuiKey_Home] = K_HOME;
	io.KeyMap[ImGuiKey_End] = K_END;
	io.KeyMap[ImGuiKey_Delete] = K_DEL;
	io.KeyMap[ImGuiKey_Backspace] = K_BACKSPACE;
	io.KeyMap[ImGuiKey_Enter] = K_ENTER;
	io.KeyMap[ImGuiKey_Escape] = K_ESCAPE;

	FillCharKeys( io.KeyMap );

	g_DisplaySize.x = windowWidth;
	g_DisplaySize.y = windowHeight;
	io.DisplaySize = g_DisplaySize;

	// RB: FIXME double check
	io.SetClipboardTextFn = SetClipboardText;
	io.GetClipboardTextFn = GetClipboardText;
	io.ClipboardUserData = NULL;

	// SRS - store imgui.ini file in fs_savepath (not in cwd please!)
	static idStr BFG_IniFilename = fileSystem->BuildOSPath( cvarSystem->GetCVarString( "fs_savepath" ), io.IniFilename );
	io.IniFilename = BFG_IniFilename;

	// make it a bit prettier with rounded edges
	ImGuiStyle& style = ImGui::GetStyle();

#if 1
	style.ChildRounding = 4.0f;
	style.FrameRounding = 4.0f;
	style.ScrollbarRounding = 4.0f;
	style.GrabRounding = 4.0f;
	style.FrameBorderSize = 1.0f;
#endif

	// Setup style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();
	//ImGui::StyleColorsLight();
	//StyleGruvboxDark();
	StyleJungle();
	//StyleBioshock();
	//StyleUE5EditorTheme( "themes/military1.json" );

#if defined( DAEDALUS_EDITOR )
	// Daedalus: the editor's fonts, icons and look
	DaedalusEditor::UI::Init();
#endif

	// Daedalus: panels can be dragged out into windows of their own
	InitViewports();

	g_IsInit = true;

	return true;
}

void SetMousePos( float x, float y )
{
	g_MousePos = ImVec2( x, y );
	g_MousePosForced = true;
}

void NotifyDisplaySizeChanged( int width, int height )
{
	// Daedalus: just take the new size (NewFrame passes it on). Recreating the context here
	// threw away the editor's layout state, fonts and any panels in their own windows.
	g_DisplaySize = ImVec2( ( float )width, ( float )height );
}

// inject a sys event
bool InjectSysEvent( const sysEvent_t* event )
{
	if( IsInitialized() && ( UseInput() || RightMouseActive() ) && !PlayingFromEditor() )
	{
		if( event == NULL )
		{
			assert( 0 ); // I think this shouldn't happen
			return false;
		}

		const sysEvent_t& ev = *event;

		switch( ev.evType )
		{
			case SE_KEY:
				return HandleKeyEvent( ev );

			case SE_MOUSE_ABSOLUTE:
				// RB: still allow mouse movement if right mouse button is pressed
				//if( !g_MousePressed[1] )
			{
				// Daedalus: with panel windows the mouse is polled in screen coordinates instead
				if( !ViewportsActive() )
				{
					g_MousePos.x = ev.evValue;
					g_MousePos.y = ev.evValue2;
				}
				return true;
			}

			case SE_CHAR:
				if( ev.evValue < 0x10000 )
				{
					ImGui::GetIO().AddInputCharacter( ev.evValue );
					return true;
				}
				break;

			case SE_MOUSE_LEAVE:
				if( !ViewportsActive() )
				{
					g_MousePos = ImVec2( -1.0f, -1.0f );
				}
				return true;

			default:
				break;
		}
	}
	return false;
}

bool RightMouseActive()
{
	return g_MousePressed[1];
}

bool InjectMouseWheel( int delta )
{
	if( IsInitialized() && UseInput() && !PlayingFromEditor() && delta != 0 )
	{
		g_MouseWheel += ( float )delta;
		return true;
	}
	return false;
}

bool InjectMouseWheelSmooth( float notches )
{
	if( IsInitialized() && UseInput() && !PlayingFromEditor() && notches != 0.0f )
	{
		g_MouseWheel += notches;
		return true;
	}
	return false;
}

void NewFrame()
{
	if( !g_haveNewFrame && IsInitialized() && ShowWindows() )
	{
		ImGuiIO& io = ImGui::GetIO();

		// Setup display size (every frame to accommodate for window resizing)
		io.DisplaySize = g_DisplaySize;

		// Setup time step
		int	time = Sys_Milliseconds();
		double current_time = time * 0.001;
		io.DeltaTime = g_Time > 0.0 ? ( float )( current_time - g_Time ) : ( float )( 1.0f / 60.0f );

		if( io.DeltaTime <= 0.0F )
		{
			io.DeltaTime = ( 1.0f / 60.0f );
		}

		g_Time = current_time;

		// Daedalus: panel windows follow the main window, and the mouse is found in screen coordinates
		const bool playing = PlayingFromEditor();
		ViewportsNewFrame( !playing && !g_MousePosForced, g_MousePos );
		g_MousePosForced = false;

		// Setup inputs
		if( playing )
		{
			// the game has the mouse: no position, no buttons, and nothing left held for later
			io.MousePos = ImVec2( -FLT_MAX, -FLT_MAX );
			g_MousePos = ImVec2( -1.0f, -1.0f );
			for( int i = 0; i < 5; ++i )
			{
				g_MousePressed[i] = false;
				io.MouseDown[i] = false;
			}
		}
		else
		{
			io.MousePos = g_MousePos;

			// If a mouse press event came, always pass it as "mouse held this frame",
			// so we don't miss click-release events that are shorter than 1 frame.
			for( int i = 0; i < 5; ++i )
			{
				io.MouseDown[i] = g_MousePressed[i] || usercmdGen->KeyState( K_MOUSE1 + i ) == 1;
				//g_MousePressed[i] = false;
			}
		}

		io.MouseWheel = g_MouseWheel;
		g_MouseWheel = 0.0f;

		// Hide OS mouse cursor if ImGui is drawing it TODO: hide mousecursor?
		// ShowCursor(io.MouseDrawCursor ? 0 : 1);

		ImGui::GetIO().MouseDrawCursor = UseInput();

		// Start the frame
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();

		g_haveNewFrame = true;
	}
}

bool IsReadyToRender()
{
	if( IsInitialized() && ShowWindows() )
	{
		if( !g_haveNewFrame )
		{
			// for screenshots etc, where we didn't go through idCommonLocal::Frame()
			// before idRenderSystemLocal::SwapCommandBuffers_FinishRendering()
			NewFrame();
		}

		return true;
	}

	return false;
}

void Render()
{
	if( !g_insideRender && IsInitialized() && ShowWindows() )
	{
		g_insideRender = true;

		if( !g_haveNewFrame )
		{
			// for screenshots etc, where we didn't go through idCommonLocal::Frame()
			// before idRenderSystemLocal::SwapCommandBuffers_FinishRendering()
			NewFrame();
		}

		// Daedalus: the editor's menu bar goes first so the dockspace fits below it
		DaedalusEditor::DrawMainMenuBar();

		// make dockspace transparent
		static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;
		ImGuiID mainDockSpaceId = ImGui::DockSpaceOverViewport( NULL, dockspaceFlags, NULL );

		DaedalusEditor::Draw( mainDockSpaceId );

		ImGuiTools::DrawToolWindows();

		if( imgui_showDemoWindow.GetBool() )
		{
			ImGui::ShowDemoWindow();
		}

		ImGui::Render();
		idRenderBackend::ImGui_RenderDrawLists( ImGui::GetDrawData() );
		g_FrameRendered = true;
		g_haveNewFrame = false;
		g_insideRender = false;
	}
}

void Destroy()
{
	if( IsInitialized() )
	{
		ShutdownViewports();
		ImGui::DestroyContext();
		g_IsInit = false;
		g_haveNewFrame = false;
	}
}

bool IsInitialized()
{
	// checks if imgui is up and running
	return g_IsInit;
}

} //namespace ImGuiHook
