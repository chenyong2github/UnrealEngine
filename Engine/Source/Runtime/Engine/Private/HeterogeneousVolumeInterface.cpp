// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HeterogeneousVolumeInterface.cpp: Primitive scene proxy implementation.
=============================================================================*/

#include "HeterogeneousVolumeInterface.h"

IHeterogeneousVolumeInterface::~IHeterogeneousVolumeInterface()
{
}

FHeterogeneousVolumeData::FHeterogeneousVolumeData()
	: VoxelResolution(FIntVector::ZeroValue)
	, MinimumVoxelSize(0.1)
	, LightingDownsampleFactor(1.0)
{
}

FHeterogeneousVolumeData::~FHeterogeneousVolumeData()
{
}
