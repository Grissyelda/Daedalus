/*
===========================================================================

GrizzTech4 GPL Source Code
Copyright (C) 2026 GrizzTech4 contributors

This file is part of GrizzTech4. GrizzTech4 is free software: you can
redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of
the License, or (at your option) any later version.

===========================================================================
*/

// ImGui in the editor's extra panel windows (panels dragged onto other monitors)

#include <imgui_viewport.cb.h>

#ifdef SPIRV

[[vk::push_constant]] ConstantBuffer<ImGuiViewportConstants> g_Const;

#else

cbuffer g_Const :
register( b0 )
{
	ImGuiViewportConstants g_Const;
}

#endif

// *INDENT-OFF*
struct VS_IN
{
	float2 pos		: POSITION;
	float2 uv		: TEXCOORD0;
	float4 color	: COLOR0;
};

struct VS_OUT
{
	float4 posClip	: SV_Position;
	float4 color	: COLOR0;
	float2 uv		: TEXCOORD0;
};
// *INDENT-ON*

void main( VS_IN vertex, out VS_OUT result )
{
	result.posClip = float4( vertex.pos * g_Const.scale + g_Const.translate, 0, 1 );
	result.color = vertex.color;
	result.uv = vertex.uv;
}
