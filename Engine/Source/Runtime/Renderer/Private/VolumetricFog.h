// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VolumetricFog.h
=============================================================================*/

#pragma once

#include "RHIDefinitions.h"
#include "SceneView.h"
#include "SceneRendering.h"

inline bool DoesPlatformSupportVolumetricFog(const FStaticShaderPlatform Platform)
{
	return Platform == SP_PCD3D_SM5
		|| Platform == SP_METAL_SM5 || Platform == SP_METAL_SM5_NOTESS
		|| IsVulkanSM5Platform(Platform)
		|| FDataDrivenShaderPlatformInfo::GetSupportsVolumetricFog(Platform);
}

inline bool DoesPlatformSupportVolumetricFogVoxelization(const FStaticShaderPlatform Platform)
{
	return Platform == SP_PCD3D_SM5
		|| Platform == SP_METAL_SM5 || Platform == SP_METAL_SM5_NOTESS
		|| IsVulkanSM5Platform(Platform)
		|| FDataDrivenShaderPlatformInfo::GetSupportsVolumetricFog(Platform);
}

extern bool ShouldRenderVolumetricFog(const FScene* Scene, const FSceneViewFamily& ViewFamily);

extern bool LightNeedsSeparateInjectionIntoVolumetricFog(const FLightSceneInfo* LightSceneInfo, FVisibleLightInfo& VisibleLightInfo);
