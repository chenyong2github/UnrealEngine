// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VT/RuntimeVirtualTextureEnum.h"

class FRHICommandListImmediate;
class FRHITexture2D;
class FScene;
class URuntimeVirtualTextureComponent;

namespace RuntimeVirtualTexture
{
#if WITH_EDITOR
	/**
	 * Get the scene index of the FRuntimeVirtualTextureSceneProxy associated with a URuntimeVirtualTextureComponent.
	 * This is needed when rendering runtime virtual texture pages in alternative contexts such as when building previews etc.
	 * This function is slow because it needs to flush render commands.
	 */
	RENDERER_API uint32 GetRuntimeVirtualTextureSceneIndex_GameThread(URuntimeVirtualTextureComponent* InComponent);
#endif

	/** Enum for our maximum RenderPages() batch size. */
	enum { EMaxRenderPageBatch = 8 };

	/** A single page description. Multiple of these can be placed in a single FRenderPageBatchDesc batch description. */
	struct FRenderPageDesc
	{
		uint8 vLevel;
		FBox2D UVRange;
		FBox2D DestBox[RuntimeVirtualTexture::MaxTextureLayers];
	};

	/** A description of a batch of pages to be rendered with a single call to RenderPages(). */
	struct FRenderPageBatchDesc
	{
		FScene* Scene;
		uint32 RuntimeVirtualTextureMask;
		FTransform UVToWorld;
		ERuntimeVirtualTextureMaterialType MaterialType;
		uint8 MaxLevel;
		bool bClearTextures;
		ERuntimeVirtualTextureDebugType DebugType;

		int32 NumPageDescs;
		FRHITexture2D* Textures[RuntimeVirtualTexture::MaxTextureLayers];
		FRenderPageDesc PageDescs[EMaxRenderPageBatch];
	};

	/** Returns true if the FScene is initialized for rendering to runtime virtual textures. */
	RENDERER_API bool IsSceneReadyToRender(FScene* Scene);

	/** Render a batch of pages for a runtime virtual texture. */
	RENDERER_API void RenderPages(FRHICommandListImmediate& RHICmdList, FRenderPageBatchDesc const& InDesc);
}
