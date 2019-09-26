// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RayTracingInstance.cpp: Helper functions for creating a ray tracing instance.
=============================================================================*/

#include "RayTracingInstanceUtils.h"
#include "RayTracingInstance.h"

#if RHI_RAYTRACING


void AddOpaqueRaytracingInstance(const FMatrix& InstanceTransform, const FRayTracingGeometry* RayTracingGeometry, const uint32 Mask, TArray<FRayTracingInstance>& OutRayTracingInstances)
{
	FRayTracingInstance Instance;
	Instance.Geometry = RayTracingGeometry;
	Instance.InstanceTransforms.Add(InstanceTransform);
	Instance.bForceOpaque = true;
	Instance.Mask = 0;
	if (Mask & RaytracingInstanceMask_Opaque)		Instance.Mask |= RAY_TRACING_MASK_OPAQUE;
	if (Mask & RaytracingInstanceMask_Translucent)	Instance.Mask |= RAY_TRACING_MASK_TRANSLUCENT;
	if (Mask & RaytracingInstanceMask_Shadow)		Instance.Mask |= RAY_TRACING_MASK_SHADOW;
	if (Mask & RaytracingInstanceMask_ThinShadow)	Instance.Mask |= RAY_TRACING_MASK_THIN_SHADOW;
	OutRayTracingInstances.Add(Instance);


}

#endif
