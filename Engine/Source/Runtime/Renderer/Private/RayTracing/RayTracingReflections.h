// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHIDefinitions.h"

class FViewInfo;
class FScene;

struct FRayTracingReflectionOptions
{
	enum EAlgorithm
	{
		BruteForce,
		Sorted,
		SortedDeferred
	};

	EAlgorithm Algorithm = EAlgorithm::Sorted;
	int SamplesPerPixel = 1;
	float ResolutionFraction = 1.0;
	bool bReflectOnlyWater = false;
};

#if RHI_RAYTRACING

int32 GetRayTracingReflectionsSamplesPerPixel(const FViewInfo& View);
float GetRayTracingReflectionsMaxRoughness(const FViewInfo& View);

bool ShouldRenderRayTracingReflections(const FViewInfo& View);
bool ShouldRayTracedReflectionsUseHybridReflections();
bool ShouldRayTracedReflectionsSortMaterials(const FViewInfo& View);
bool ShouldRayTracedReflectionsUseSortedDeferredAlgorithm(const FViewInfo& View);
bool ShouldRayTracedReflectionsRayTraceSkyLightContribution(const FScene& Scene);

#else

FORCEINLINE int32 GetRayTracingReflectionsSamplesPerPixel(const FViewInfo& View)
{
	return 0;
}

FORCEINLINE bool ShouldRayTracedReflectionsSortMaterials(const FViewInfo& View)
{
	return false;
}

FORCEINLINE bool ShouldRayTracedReflectionsUseSortedDeferredAlgorithm(const FViewInfo& View)
{
	return false;
}

#endif // RHI_RAYTRACING
