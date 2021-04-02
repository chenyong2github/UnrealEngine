// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VolumetricFog.h
=============================================================================*/

#pragma once

#include "RHIDefinitions.h"
#include "SceneView.h"
#include "SceneRendering.h"


class FTransientLightFunctionTextureAtlas
{
public:

	FTransientLightFunctionTextureAtlas(FRDGBuilder& GraphBuilder);
	~FTransientLightFunctionTextureAtlas();

	// FTransientLightFunctionTextureAtlasTile will never be null, but it can be a default white light function
	FTransientLightFunctionTextureAtlasTile AllocateAtlasTile();

	FRDGTextureRef GetTransientLightFunctionAtlasTexture()
	{
		return TransientLightFunctionAtlasTexture;
	}
	FRDGTextureRef GetDefaultLightFunctionTexture()
	{
		return DefaultLightFunctionAtlasItemTexture;
	}

	uint32 GetAtlasTextureWidth()
	{
		return AtlasTextureWidth;
	}

private:
	FTransientLightFunctionTextureAtlas() {}

	uint32 AtlasItemWidth;
	uint32 AtlasTextureWidth;
	uint32 AllocatedAtlasTiles;
	float HalfTexelSize;

	FRDGTextureRef TransientLightFunctionAtlasTexture;
	FRDGTextureRef DefaultLightFunctionAtlasItemTexture;
};

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

extern bool LightNeedsSeparateInjectionIntoVolumetricFogForOpaqueShadow(const FLightSceneInfo* LightSceneInfo, FVisibleLightInfo& VisibleLightInfo);
extern bool LightNeedsSeparateInjectionIntoVolumetricFogForLightFunction(const FLightSceneInfo* LightSceneInfo);
