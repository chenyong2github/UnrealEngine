// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderResource.h"

#if RHI_RAYTRACING

enum ERaytracingInstanceMask
{
	RaytracingInstanceMask_Opaque			= 0x01,
	RaytracingInstanceMask_Translucent		= 0x02,
	RaytracingInstanceMask_ThinShadow		= 0x04,
	RaytracingInstanceMask_Shadow			= 0x08,
	RaytracingInstanceMask_All				= 0xFF
};

ENGINE_API void AddOpaqueRaytracingInstance(const FMatrix& InstanceTransform, const FRayTracingGeometry* RayTracingGeometry, const uint32 Mask, TArray<struct FRayTracingInstance>& OutRayTracingInstances);
#endif
