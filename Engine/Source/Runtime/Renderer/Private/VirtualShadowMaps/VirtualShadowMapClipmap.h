// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
VirtualShadowMapClipmap.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "VirtualShadowMapProjection.h"

struct FViewMatrices;
class FVirtualShadowMapArray;
class FVirtualShadowMapArrayCacheManager;

class FVirtualShadowMapClipmap : FRefCountedObject
{
public:	
	FVirtualShadowMapClipmap(
		FVirtualShadowMapArray& VirtualShadowMapArray,
		FVirtualShadowMapArrayCacheManager* VirtualShadowMapArrayCacheManager,
		const FLightSceneInfo& InLightSceneInfo,
		const FMatrix& WorldToLight,
		const FViewMatrices& CameraViewMatrices,
		float MaxRadius		// Maximum radius the clipmap must cover from the center point; used to compute level count
	);

	FViewMatrices GetViewMatrices(int32 ClipmapIndex) const;

	FVirtualShadowMap* GetVirtualShadowMap(int32 ClipmapIndex)
	{
		return LevelData[ClipmapIndex].VirtualShadowMap;
	}

	int32 GetLevelCount() const
	{
		return LevelData.Num();
	}

	// Get absolute clipmap level from index (0..GetLevelCount())
	int32 GetClipmapLevel(int32 ClipmapIndex) const
	{
		return FirstLevel + ClipmapIndex;
	}

	const FLightSceneInfo& GetLightSceneInfo() const
	{
		return LightSceneInfo;
	}

	FVirtualShadowMapProjectionShaderData GetProjectionShaderData(
		int32 ClipmapIndex) const;

private:
	const FLightSceneInfo& LightSceneInfo;

	// A translation that is applied to world-space before transforming by one of the shadow matrices.
	FMatrix TranslatedWorldToView;

	int32 FirstLevel;
	float ResolutionLodBias;

	struct FLevelData
	{
		FVirtualShadowMap* VirtualShadowMap;
		FMatrix ViewToClip;
		FVector WorldCenter;
	};

	TArray< FLevelData, TInlineAllocator<16> > LevelData;
};
