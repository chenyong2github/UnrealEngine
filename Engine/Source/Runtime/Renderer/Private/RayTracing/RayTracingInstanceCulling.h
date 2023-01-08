// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "RHIDefinitions.h"

#if RHI_RAYTRACING

#include "Containers/ArrayView.h"
#include "Math/MathFwd.h"

class FPrimitiveSceneInfo;
class FScene;

struct FRayTracingCullingParameters;

int32 GetRayTracingCulling();
float GetRayTracingCullingRadius();
int32 GetRayTracingCullingPerInstance();

namespace RayTracing
{

// Tests if a primitive with the given inputs should be considered for culling.  Does NOT test configuration values.  The assumption is that the config values have already been tested.
bool ShouldConsiderCulling(const FRayTracingCullingParameters& CullingParameters, const FBoxSphereBounds& ObjectBounds, float MinDrawDistance);

// Returns true if the primitive should be culled out due to it's ray tracing flags
bool CullPrimitiveByFlags(const FRayTracingCullingParameters& CullingParameters, const FScene* RESTRICT Scene, int32 PrimitiveIndex);

// Completely test if the bounds should be culled for ray tracing. This includes all configuration values.
bool ShouldCullBounds(const FRayTracingCullingParameters& CullingParameters, const FBoxSphereBounds& ObjectBounds, float MinDrawDistance, bool bIsFarFieldPrimitive);

bool ShouldSkipPerInstanceCullingForPrimitive(const FRayTracingCullingParameters& CullingParameters, FBoxSphereBounds ObjectBounds, FBoxSphereBounds SmallestInstanceBounds, bool bIsFarFieldPrimitive);
}

struct FRayTracingCullPrimitiveInstancesClosure
{
	const FScene* Scene;
	int32 PrimitiveIndex;
	const FPrimitiveSceneInfo* SceneInfo;
	bool bIsFarFieldPrimitive;
	TArrayView<uint32> OutInstanceActivationMask;

	const FRayTracingCullingParameters* CullingParameters;

	void operator()() const;
};

#endif
