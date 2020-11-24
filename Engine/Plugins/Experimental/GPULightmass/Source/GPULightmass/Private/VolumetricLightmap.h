// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "PrimitiveSceneInfo.h"
#include "PrecomputedVolumetricLightmap.h"
#include "Scene/Scene.h"

namespace GPULightmass
{

class FVolumetricLightmapRenderer
{
public:
	FVolumetricLightmapRenderer(FSceneRenderState* Scene);

	void VoxelizeScene();
	void BackgroundTick();

	FPrecomputedVolumetricLightmap* GetPrecomputedVolumetricLightmapForPreview();

	FBox CombinedImportanceVolume;
	TArray<FBox> ImportanceVolumes;
	float TargetDetailCellSize = 50.0f;
	int32 NumTotalBricks;

	int32 FrameNumber = 0;
	uint64 SamplesTaken = 0;

	int32 GetGISamplesMultiplier();

private:
	FSceneRenderState* Scene;

	FPrecomputedVolumetricLightmap VolumetricLightmap;
	FPrecomputedVolumetricLightmapData VolumetricLightmapData;
	FVolumetricLightmapBrickData AccumulationBrickData;
	TRefCountPtr<IPooledRenderTarget> IndirectionTexture;

	FVector VolumeMin;
	FVector VolumeSize;
	FIntVector IndirectionTextureDimensions;

	TArray<TRefCountPtr<IPooledRenderTarget>> VoxelizationVolumeMips;

	FRWBuffer BrickAllocatorParameters;
	FRWBuffer BrickRequests;
};

}
