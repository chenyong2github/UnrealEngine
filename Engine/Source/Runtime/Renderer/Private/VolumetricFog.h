// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VolumetricFog.h
=============================================================================*/

#pragma once

#include "RHIDefinitions.h"
#include "SceneView.h"
#include "SceneRendering.h"

inline bool DoesPlatformSupportVolumetricFog(EShaderPlatform Platform)
{
	return Platform == SP_PCD3D_SM5 || Platform == SP_PS4 || Platform == SP_XBOXONE_D3D12
		|| Platform == SP_METAL_SM5 || Platform == SP_METAL_SM5_NOTESS
		|| IsVulkanSM5Platform(Platform)
		|| FDataDrivenShaderPlatformInfo::GetInfo(Platform).bSupportsVolumetricFog;
}

inline bool DoesPlatformSupportVolumetricFogVoxelization(EShaderPlatform Platform)
{
	return Platform == SP_PCD3D_SM5 || Platform == SP_PS4 || Platform == SP_XBOXONE_D3D12
		|| Platform == SP_METAL_SM5 || Platform == SP_METAL_SM5_NOTESS
		|| IsVulkanSM5Platform(Platform)
		|| FDataDrivenShaderPlatformInfo::GetInfo(Platform).bSupportsVolumetricFog;
}

extern bool ShouldRenderVolumetricFog(const FScene* Scene, const FSceneViewFamily& ViewFamily);

extern bool LightNeedsSeparateInjectionIntoVolumetricFog(const FLightSceneInfo* LightSceneInfo, FVisibleLightInfo& VisibleLightInfo);
