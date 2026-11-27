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

#include "renderer/RenderCommon.h"
#include "renderer/ImGuiViewportRenderer.h"
#include "sys/DeviceManager.h"
#include "../../libs/imgui/imgui.h"

#include <unordered_map>

extern DeviceManager* deviceManager;

namespace ImGuiViewportRenderer
{

struct target_t
{
	void* 					swapChain;
	nvrhi::BufferHandle		vertexBuffer;
	nvrhi::BufferHandle		indexBuffer;
	size_t					vertexBufferSize;
	size_t					indexBufferSize;
};

struct constants_t
{
	float	scale[2];
	float	translate[2];
};

static bool								initialized = false;
static bool								failed = false;
static nvrhi::ShaderHandle				vertexShader;
static nvrhi::ShaderHandle				pixelShader;
static nvrhi::InputLayoutHandle			inputLayout;
static nvrhi::BindingLayoutHandle		bindingLayout;
static nvrhi::SamplerHandle				sampler;
static nvrhi::CommandListHandle			commandList;
static std::unordered_map<int, nvrhi::GraphicsPipelineHandle>	pipelines;		// by framebuffer format
static std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> bindingSets;

bool Supported()
{
	if( deviceManager == NULL )
	{
		return false;
	}
	const nvrhi::GraphicsAPI api = deviceManager->GetGraphicsAPI();
	return api == nvrhi::GraphicsAPI::D3D12 || api == nvrhi::GraphicsAPI::VULKAN;
}

static bool Init()
{
	if( initialized )
	{
		return true;
	}
	if( failed )
	{
		return false;
	}

	nvrhi::IDevice* device = deviceManager->GetDevice();
	const int vsIndex = renderProgManager.FindShader( "builtin/imgui_viewport", SHADER_STAGE_VERTEX, "", idList<shaderMacro_t>(), true, LAYOUT_DRAW_VERT );
	const int psIndex = renderProgManager.FindShader( "builtin/imgui_viewport", SHADER_STAGE_FRAGMENT, "", idList<shaderMacro_t>(), true, LAYOUT_DRAW_VERT );
	vertexShader = renderProgManager.GetShader( vsIndex );
	pixelShader = renderProgManager.GetShader( psIndex );
	if( !vertexShader || !pixelShader )
	{
		common->Warning( "Panel windows: the builtin/imgui_viewport shaders are missing" );
		failed = true;
		return false;
	}

	nvrhi::VertexAttributeDesc attributes[] =
	{
		nvrhi::VertexAttributeDesc().setName( "POSITION" ).setFormat( nvrhi::Format::RG32_FLOAT ).setOffset( offsetof( ImDrawVert, pos ) ).setElementStride( sizeof( ImDrawVert ) ),
		nvrhi::VertexAttributeDesc().setName( "TEXCOORD" ).setFormat( nvrhi::Format::RG32_FLOAT ).setOffset( offsetof( ImDrawVert, uv ) ).setElementStride( sizeof( ImDrawVert ) ),
		nvrhi::VertexAttributeDesc().setName( "COLOR" ).setFormat( nvrhi::Format::RGBA8_UNORM ).setOffset( offsetof( ImDrawVert, col ) ).setElementStride( sizeof( ImDrawVert ) ),
	};
	inputLayout = device->createInputLayout( attributes, uint32_t( std::size( attributes ) ), vertexShader );

	nvrhi::BindingLayoutDesc layoutDesc;
	layoutDesc.visibility = nvrhi::ShaderType::All;
	layoutDesc.bindings =
	{
		nvrhi::BindingLayoutItem::PushConstants( 0, sizeof( constants_t ) ),
		nvrhi::BindingLayoutItem::Texture_SRV( 0 ),
		nvrhi::BindingLayoutItem::Sampler( 0 )
	};
	bindingLayout = device->createBindingLayout( layoutDesc );

	sampler = device->createSampler( nvrhi::SamplerDesc().setAllFilters( true ).setAllAddressModes( nvrhi::SamplerAddressMode::Wrap ) );
	commandList = device->createCommandList();

	initialized = inputLayout && bindingLayout && sampler && commandList;
	failed = !initialized;
	return initialized;
}

void Shutdown()
{
	if( deviceManager != NULL && initialized )
	{
		deviceManager->GetDevice()->waitForIdle();
	}
	bindingSets.clear();
	pipelines.clear();
	commandList = nullptr;
	sampler = nullptr;
	bindingLayout = nullptr;
	inputLayout = nullptr;
	vertexShader = nullptr;
	pixelShader = nullptr;
	initialized = false;
	failed = false;
}

void* CreateWindowTarget( void* windowHandle, int width, int height )
{
	if( !Supported() || !Init() )
	{
		return NULL;
	}
	void* swapChain = deviceManager->CreateExtraSwapChain( windowHandle, width, height );
	if( swapChain == NULL )
	{
		return NULL;
	}
	target_t* target = new target_t();
	target->swapChain = swapChain;
	target->vertexBufferSize = 0;
	target->indexBufferSize = 0;
	return target;
}

void ResizeWindowTarget( void* handle, int width, int height )
{
	target_t* target = ( target_t* )handle;
	if( target != NULL )
	{
		deviceManager->ResizeExtraSwapChain( target->swapChain, width, height );
	}
}

void DestroyWindowTarget( void* handle )
{
	target_t* target = ( target_t* )handle;
	if( target == NULL )
	{
		return;
	}
	deviceManager->DestroyExtraSwapChain( target->swapChain );
	target->vertexBuffer = nullptr;
	target->indexBuffer = nullptr;
	delete target;
	// textures may come and go; binding sets are cheap to make again
	bindingSets.clear();
}

// ImGui's texture IDs are materials (the font atlas is "_imguiFont")
static nvrhi::ITexture* TextureFor( ImTextureID id )
{
	idImage* fontImage = globalImages->imguiFontImage;
	const idMaterial* material = ( const idMaterial* )id;
	idImage* image = NULL;
	if( material != NULL && material != tr.imgGuiMaterial )
	{
		image = material->GetEditorImage();
	}
	if( image == NULL || !image->IsLoaded() || !image->GetTextureHandle() )
	{
		image = fontImage;
	}
	return image != NULL ? image->GetTextureHandle().Get() : NULL;
}

static nvrhi::IBindingSet* BindingSetFor( nvrhi::ITexture* texture )
{
	auto it = bindingSets.find( texture );
	if( it != bindingSets.end() )
	{
		return it->second;
	}
	nvrhi::BindingSetDesc desc;
	desc.bindings =
	{
		nvrhi::BindingSetItem::PushConstants( 0, sizeof( constants_t ) ),
		nvrhi::BindingSetItem::Texture_SRV( 0, texture ),
		nvrhi::BindingSetItem::Sampler( 0, sampler )
	};
	nvrhi::BindingSetHandle set = deviceManager->GetDevice()->createBindingSet( desc, bindingLayout );
	bindingSets[texture] = set;
	return set;
}

static nvrhi::IGraphicsPipeline* PipelineFor( nvrhi::IFramebuffer* framebuffer )
{
	const int format = int( framebuffer->getDesc().colorAttachments[0].texture->getDesc().format );
	auto it = pipelines.find( format );
	if( it != pipelines.end() )
	{
		return it->second;
	}

	nvrhi::GraphicsPipelineDesc desc;
	desc.primType = nvrhi::PrimitiveType::TriangleList;
	desc.inputLayout = inputLayout;
	desc.VS = vertexShader;
	desc.PS = pixelShader;
	desc.bindingLayouts = { bindingLayout };
	desc.renderState.rasterState.setCullNone().setScissorEnable( true );
	desc.renderState.depthStencilState.depthTestEnable = false;
	desc.renderState.depthStencilState.depthWriteEnable = false;
	desc.renderState.depthStencilState.stencilEnable = false;
	desc.renderState.blendState.targets[0]
	.setBlendEnable( true )
	.setSrcBlend( nvrhi::BlendFactor::SrcAlpha )
	.setDestBlend( nvrhi::BlendFactor::InvSrcAlpha )
	.setSrcBlendAlpha( nvrhi::BlendFactor::One )
	.setDestBlendAlpha( nvrhi::BlendFactor::InvSrcAlpha );

	nvrhi::GraphicsPipelineHandle pipeline = deviceManager->GetDevice()->createGraphicsPipeline( desc, framebuffer );
	pipelines[format] = pipeline;
	return pipeline;
}

static void EnsureBuffer( nvrhi::BufferHandle& buffer, size_t& size, size_t needed, bool index )
{
	if( buffer && size >= needed )
	{
		return;
	}
	size = Max( needed + needed / 2, ( size_t )( index ? 16384 : 32768 ) );
	nvrhi::BufferDesc desc;
	desc.byteSize = size;
	desc.isVertexBuffer = !index;
	desc.isIndexBuffer = index;
	desc.debugName = index ? "Panel window indices" : "Panel window vertices";
	desc.initialState = nvrhi::ResourceStates::CopyDest;
	desc.keepInitialState = true;
	buffer = deviceManager->GetDevice()->createBuffer( desc );
}

void RenderWindow( void* handle, ImDrawData* drawData )
{
	target_t* target = ( target_t* )handle;
	if( target == NULL || drawData == NULL || !initialized )
	{
		return;
	}
	nvrhi::IFramebuffer* framebuffer = deviceManager->BeginExtraFrame( target->swapChain );
	if( framebuffer == NULL )
	{
		return;
	}
	nvrhi::IDevice* device = deviceManager->GetDevice();
	const nvrhi::FramebufferInfoEx& fbInfo = framebuffer->getFramebufferInfo();

	// everything in one vertex and one index buffer
	const size_t vertexBytes = Max( 1, drawData->TotalVtxCount ) * sizeof( ImDrawVert );
	const size_t indexBytes = Max( 1, drawData->TotalIdxCount ) * sizeof( ImDrawIdx );
	EnsureBuffer( target->vertexBuffer, target->vertexBufferSize, vertexBytes, false );
	EnsureBuffer( target->indexBuffer, target->indexBufferSize, indexBytes, true );

	commandList->open();
	commandList->clearTextureFloat( framebuffer->getDesc().colorAttachments[0].texture, nvrhi::AllSubresources, nvrhi::Color( 0.118f, 0.122f, 0.130f, 1.0f ) );

	if( drawData->TotalVtxCount > 0 )
	{
		idList<ImDrawVert> vertices;
		idList<ImDrawIdx> indices;
		vertices.SetNum( drawData->TotalVtxCount );
		indices.SetNum( drawData->TotalIdxCount );
		int vtx = 0, idx = 0;
		for( int n = 0; n < drawData->CmdListsCount; n++ )
		{
			const ImDrawList* list = drawData->CmdLists[n];
			memcpy( &vertices[vtx], list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof( ImDrawVert ) );
			memcpy( &indices[idx], list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof( ImDrawIdx ) );
			vtx += list->VtxBuffer.Size;
			idx += list->IdxBuffer.Size;
		}
		commandList->writeBuffer( target->vertexBuffer, vertices.Ptr(), vertices.Num() * sizeof( ImDrawVert ) );
		commandList->writeBuffer( target->indexBuffer, indices.Ptr(), indices.Num() * sizeof( ImDrawIdx ) );

		// ImGui pixels (relative to the window's DisplayPos) to clip space
		constants_t constants;
		constants.scale[0] = 2.0f / drawData->DisplaySize.x;
		constants.scale[1] = -2.0f / drawData->DisplaySize.y;
		constants.translate[0] = -1.0f - drawData->DisplayPos.x * constants.scale[0];
		constants.translate[1] = 1.0f - drawData->DisplayPos.y * constants.scale[1];

		nvrhi::GraphicsState state;
		state.pipeline = PipelineFor( framebuffer );
		state.framebuffer = framebuffer;
		state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer( target->vertexBuffer ).setSlot( 0 ).setOffset( 0 ) };
		state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer( target->indexBuffer ).setFormat( sizeof( ImDrawIdx ) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT ).setOffset( 0 );
		state.viewport.addViewport( nvrhi::Viewport( float( fbInfo.width ), float( fbInfo.height ) ) );

		const ImVec2 clipOffset = drawData->DisplayPos;
		const ImVec2 clipScale = drawData->FramebufferScale;
		int vertexOffset = 0, indexOffset = 0;
		for( int n = 0; n < drawData->CmdListsCount; n++ )
		{
			const ImDrawList* list = drawData->CmdLists[n];
			for( int c = 0; c < list->CmdBuffer.Size; c++ )
			{
				const ImDrawCmd* cmd = &list->CmdBuffer[c];
				if( cmd->UserCallback != NULL )
				{
					if( cmd->UserCallback != ImDrawCallback_ResetRenderState )
					{
						cmd->UserCallback( list, cmd );
					}
					continue;
				}

				const int x0 = Max( 0, int( ( cmd->ClipRect.x - clipOffset.x ) * clipScale.x ) );
				const int y0 = Max( 0, int( ( cmd->ClipRect.y - clipOffset.y ) * clipScale.y ) );
				const int x1 = Min( int( fbInfo.width ), int( ( cmd->ClipRect.z - clipOffset.x ) * clipScale.x ) );
				const int y1 = Min( int( fbInfo.height ), int( ( cmd->ClipRect.w - clipOffset.y ) * clipScale.y ) );
				if( x1 <= x0 || y1 <= y0 || cmd->ElemCount == 0 )
				{
					continue;
				}

				nvrhi::ITexture* texture = TextureFor( cmd->GetTexID() );
				if( texture == NULL )
				{
					continue;
				}
				state.bindings = { BindingSetFor( texture ) };
				state.viewport.scissorRects.resize( 0 );
				state.viewport.addScissorRect( nvrhi::Rect( x0, x1, y0, y1 ) );
				commandList->setGraphicsState( state );
				commandList->setPushConstants( &constants, sizeof( constants ) );

				nvrhi::DrawArguments args;
				args.vertexCount = cmd->ElemCount;
				args.startIndexLocation = indexOffset + cmd->IdxOffset;
				args.startVertexLocation = vertexOffset + cmd->VtxOffset;
				commandList->drawIndexed( args );
			}
			vertexOffset += list->VtxBuffer.Size;
			indexOffset += list->IdxBuffer.Size;
		}
	}

	commandList->close();
	device->executeCommandList( commandList );
	deviceManager->PresentExtra( target->swapChain );
}

}
