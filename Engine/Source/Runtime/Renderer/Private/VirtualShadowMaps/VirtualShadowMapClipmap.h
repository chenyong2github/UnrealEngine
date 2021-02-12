// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
VirtualShadowMapClipmap.h
=============================================================================*/

#pragma once

#include "VirtualShadowMapConfig.h"
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
		const FMatrix& WorldToLightRotationMatrix,
		const FViewMatrices& CameraViewMatrices,
		FIntPoint CameraViewRectSize,
		const FViewInfo* InDependentView
	);

	FViewMatrices GetViewMatrices(int32 ClipmapIndex) const;

	FVirtualShadowMap* GetVirtualShadowMap(int32 ClipmapIndex = 0)
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

	FVirtualShadowMapProjectionShaderData GetProjectionShaderData(int32 ClipmapIndex) const;

	FVector GetWorldOrigin() const
	{
		return WorldOrigin;
	}

	float GetMaxRadius() const { return MaxRadius; }

	const FViewInfo* GetDependentView() const { return DependentView; }

private:
	const FLightSceneInfo& LightSceneInfo;

	/**
	 * DependentView is the 'main' or visible geometry view that this view-dependent clipmap was created for. Should only be used to 
	 * identify the view during shadow projection (note: this should be refactored to be more explicit instead).
	 */
	const FViewInfo* DependentView;

	/** Origin of the clipmap in world space
	* Usually aligns with the camera position from which it was created.
	* Note that the centers of each of the levels can be different as they are snapped to page alignment at their respective scales
	* */
	FVector WorldOrigin;

	/** Directional light rotation matrix (no translation) */
	FMatrix WorldToViewRotationMatrix;

	int32 FirstLevel;
	float ResolutionLodBias;
	float MaxRadius;

	struct FLevelData
	{
		FVirtualShadowMap* VirtualShadowMap;
		FMatrix ViewToClip;
		FVector WorldCenter;
		FIntPoint CenterSnapUnits;	// Center of clipmap in world units / level radius
	};

	TArray< FLevelData, TInlineAllocator<16> > LevelData;
};
