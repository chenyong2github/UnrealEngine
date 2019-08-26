// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class ERuntimeVirtualTextureDebugType;
enum class ERuntimeVirtualTextureMaterialType : uint8;
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

	/**
	 * Render a single page of a virtual texture with a given material.
	 * todo[vt]: Likely to be more optimal to batch several pages at a time and share setup/visibility/render targets.
	 */
	RENDERER_API void RenderPage(
		FRHICommandListImmediate& RHICmdList,
		FScene* Scene,
		uint32 RuntimeVirtualTextureMask,
		ERuntimeVirtualTextureMaterialType MaterialType,
		FRHITexture2D* Texture0, FBox2D const& DestBox0,
		FRHITexture2D* Texture1, FBox2D const& DestBox1,
		FTransform const& UVToWorld,
		FBox2D const& UVRange,
		uint8 vLevel,
		uint8 MaxLevel,
		ERuntimeVirtualTextureDebugType DebugType);
}
