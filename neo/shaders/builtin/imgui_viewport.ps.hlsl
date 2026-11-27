/*
===========================================================================

Daedalus GPL Source Code
Copyright (C) 2026 Daedalus contributors

This file is part of Daedalus. Daedalus is free software: you can
redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of
the License, or (at your option) any later version.

===========================================================================
*/

// ImGui in the editor's extra panel windows (panels dragged onto other monitors)

// *INDENT-OFF*
Texture2D tex : register( t0 );
SamplerState samp : register( s0 );

struct PS_IN
{
	float4 posClip	: SV_Position;
	float4 color	: COLOR0;
	float2 uv		: TEXCOORD0;
};
// *INDENT-ON*

void main( PS_IN fragment, out float4 o_rgba : SV_Target )
{
	o_rgba = fragment.color * tex.Sample( samp, fragment.uv );
}
