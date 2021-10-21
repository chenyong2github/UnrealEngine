// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"
#include "CoreMinimal.h"

#if RHI_RAYTRACING

struct FRayTracingInstanceDescriptorInput
{
	//uint32 GPUSceneInstanceIndex;
	FVector4f LocalToWorld[3];
	uint32 AccelerationStructureIndex;
	uint32 InstanceId;
	uint32 InstanceMaskAndFlags;
	uint32 InstanceContributionToHitGroupIndex;
};

RENDERCORE_API void FillInstanceUploadBuffer(
	TConstArrayView<FRayTracingGeometryInstance> Instances,
	TConstArrayView<uint32> InstancesGeometryIndex,
	FRayTracingSceneRHIRef RayTracingSceneRHI,
	TArrayView<FRayTracingInstanceDescriptorInput> OutInstanceUploadData);

RENDERCORE_API void BuildRayTracingInstanceBuffer(
	FRHICommandList& RHICmdList, 
	uint32 NumInstances,
	FUnorderedAccessViewRHIRef InstancesUAV,
	FShaderResourceViewRHIRef InstanceUploadSRV,
	FShaderResourceViewRHIRef AccelerationStructureAddressesSRV);

#endif // RHI_RAYTRACING