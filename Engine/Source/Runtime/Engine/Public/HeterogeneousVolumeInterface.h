// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IHeterogeneousVolumeInterface
{
public:
	ENGINE_API virtual ~IHeterogeneousVolumeInterface();

	// Volume
	ENGINE_API virtual FIntVector GetVoxelResolution() const = 0;
	ENGINE_API virtual float GetMinimumVoxelSize() const = 0;

	// Lighting
	ENGINE_API virtual float GetLightingDownsampleFactor() const = 0;
};

class FHeterogeneousVolumeData : public IHeterogeneousVolumeInterface
{
public:
	ENGINE_API FHeterogeneousVolumeData();
	ENGINE_API virtual ~FHeterogeneousVolumeData();

	// Volume
	ENGINE_API virtual FIntVector GetVoxelResolution() const { return VoxelResolution; }
	ENGINE_API virtual float GetMinimumVoxelSize() const { return MinimumVoxelSize; }

	// Lighting
	ENGINE_API virtual float GetLightingDownsampleFactor() const { return LightingDownsampleFactor; }

	FIntVector VoxelResolution;
	float MinimumVoxelSize;
	float LightingDownsampleFactor;
};
