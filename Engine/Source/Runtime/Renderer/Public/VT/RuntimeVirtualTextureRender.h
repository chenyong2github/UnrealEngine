// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class ERuntimeVirtualTextureDebugType;
enum class ERuntimeVirtualTextureMaterialType : uint8;
class FRHICommandListImmediate;
class FRHITexture2D;
class FScene;

namespace RuntimeVirtualTexture
{
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
