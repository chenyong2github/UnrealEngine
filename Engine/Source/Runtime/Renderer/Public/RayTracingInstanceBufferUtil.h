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

// Helper function to create FRayTracingSceneRHI using array of high level instances
// Also outputs data required to build the instance buffer
RENDERER_API FRayTracingSceneRHIRef CreateRayTracingSceneWithGeometryInstances(
	TArrayView<FRayTracingGeometryInstance> Instances,
	uint32 NumShaderSlotsPerGeometrySegment,
	uint32 NumMissShaderSlots,
	TArray<uint32>& OutGeometryIndices);

RENDERER_API void FillRayTracingInstanceUploadBuffer(
	TConstArrayView<FRayTracingGeometryInstance> Instances,
	TConstArrayView<uint32> InstancesGeometryIndex,
	FRayTracingSceneRHIRef RayTracingSceneRHI,
	TArrayView<FRayTracingInstanceDescriptorInput> OutInstanceUploadData);

RENDERER_API void BuildRayTracingInstanceBuffer(
	FRHICommandList& RHICmdList, 
	uint32 NumInstances,
	FUnorderedAccessViewRHIRef InstancesUAV,
	FShaderResourceViewRHIRef InstanceUploadSRV,
	FShaderResourceViewRHIRef AccelerationStructureAddressesSRV);

#endif // RHI_RAYTRACING