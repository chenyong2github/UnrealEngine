// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VirtualShadowMapConfig.h
=============================================================================*/
#pragma once

#include "RenderUtils.h"

extern int32 GEnableNonNaniteVSM;

/**
 * Returns true if non-nanite virtual shadow maps are enbled by CVar r.Shadow.Virtual.NonNaniteVSM 
 * and UseVirtualShadowMaps is true for the given platform and feature level.
 */
inline bool UseNonNaniteVirtualShadowMaps(EShaderPlatform ShaderPlatform, const FStaticFeatureLevel FeatureLevel)
{
#if GPUCULL_TODO
	return GEnableNonNaniteVSM != 0 && UseVirtualShadowMaps(ShaderPlatform, FeatureLevel);
#else // !GPUCULL_TODO
	return false;
#endif // GPUCULL_TODO
}
