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
}
