// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IHeterogeneousVolumeInterface
{
public:
	ENGINE_API virtual ~IHeterogeneousVolumeInterface();

	// Volume
	virtual FIntVector GetVoxelResolution() const = 0;
	virtual float GetMinimumVoxelSize() const = 0;

	// Lighting
	virtual float GetLightingDownsampleFactor() const = 0;
};

class FHeterogeneousVolumeData : public IHeterogeneousVolumeInterface
{
public:
	ENGINE_API FHeterogeneousVolumeData();
	ENGINE_API virtual ~FHeterogeneousVolumeData();

	// Volume
	virtual FIntVector GetVoxelResolution() const { return VoxelResolution; }
	virtual float GetMinimumVoxelSize() const { return MinimumVoxelSize; }

	// Lighting
	virtual float GetLightingDownsampleFactor() const { return LightingDownsampleFactor; }

	FIntVector VoxelResolution;
	float MinimumVoxelSize;
	float LightingDownsampleFactor;
};
