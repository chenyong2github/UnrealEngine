// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace VirtualTextureScalability
{
	/** Get max upload rate to virtual textures. */
	ENGINE_API int32 GetMaxUploadsPerFrame();
	/** Get scale factor for virtual texture physical pool sizes. */
	ENGINE_API float GetPoolSizeScale();
	/** Get resolution bias for runtime virtual textures. */
	ENGINE_API int32 GetRuntimeVirtualTextureSizeBias();
	/**
	 * Get maximum anisotropy when virtual texture sampling. 
	 * This is also clamped per virtual texture according to the tile border size.
	 */
	ENGINE_API int32 GetMaxAnisotropy();
}
