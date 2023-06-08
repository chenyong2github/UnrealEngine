// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

class UTexture;

namespace RectLightAtlas
{

// Scope for invalidating a particular texture 
// This ensures the atlas contains the latest version of the texture and filter it
struct RENDERER_API FAtlasTextureInvalidationScope
{
	FAtlasTextureInvalidationScope(const UTexture* In);
	~FAtlasTextureInvalidationScope();
	FAtlasTextureInvalidationScope(const FAtlasTextureInvalidationScope&) = delete;
	FAtlasTextureInvalidationScope& operator=(const FAtlasTextureInvalidationScope&) = delete;
	bool bLocked = false;
};

} 