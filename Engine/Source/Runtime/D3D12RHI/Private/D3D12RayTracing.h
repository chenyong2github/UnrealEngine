// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "D3D12RHIPrivate.h"

#if D3D12_RHI_RAYTRACING

class FD3D12RayTracingPipelineState;
class FD3D12RayTracingShaderTable;

typedef FD3D12VertexBuffer FD3D12MemBuffer; // Generic GPU memory buffer

class FD3D12RayTracingGeometry : public FRHIRayTracingGeometry
{
public:

	FD3D12RayTracingGeometry();
	~FD3D12RayTracingGeometry();

	void TransitionBuffers(FD3D12CommandContext& CommandContext);
	void BuildAccelerationStructure(FD3D12CommandContext& CommandContext, EAccelerationStructureBuildMode BuildMode);

	bool bIsAccelerationStructureDirty[MAX_NUM_GPUS] = {};
	void SetDirty(FRHIGPUMask GPUMask, bool bState)
	{
		for (uint32 GPUIndex : GPUMask)
		{
			bIsAccelerationStructureDirty[GPUIndex] = bState;
		}
	}
	bool IsDirty(uint32 GPUIndex) const
	{
		return bIsAccelerationStructureDirty[GPUIndex];
	}

	uint32 IndexStride = 0; // 0 for non-indexed / implicit triangle list, 2 for uint16, 4 for uint32
	uint32 IndexOffsetInBytes = 0;
	uint32 TotalPrimitiveCount = 0; // Combined number of primitives in all mesh segments

	D3D12_RAYTRACING_GEOMETRY_TYPE GeometryType = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;

	TArray<FRayTracingGeometrySegment> Segments; // Defines addressable parts of the mesh that can be used for material assignment (one segment = one SBT record)
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS BuildFlags;

	FIndexBufferRHIRef  RHIIndexBuffer;

	TRefCountPtr<FD3D12MemBuffer> AccelerationStructureBuffers[MAX_NUM_GPUS];
	TRefCountPtr<FD3D12MemBuffer> ScratchBuffers[MAX_NUM_GPUS];

};

class FD3D12RayTracingScene : public FRHIRayTracingScene
{
public:

	FD3D12RayTracingScene(FD3D12Adapter& Adapter);
	~FD3D12RayTracingScene();

	void BuildAccelerationStructure(FD3D12CommandContext& CommandContext, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS BuildFlags);

	TRefCountPtr<FD3D12MemBuffer> AccelerationStructureBuffers[MAX_NUM_GPUS];
	bool bAccelerationStructureViewInitialized[MAX_NUM_GPUS] = {};

	TArray<FRayTracingGeometryInstance> Instances;

	// Scene keeps track of child acceleration structures to manage their residency
	TArray<TRefCountPtr<FD3D12MemBuffer>> BottomLevelAccelerationStructureBuffers[MAX_NUM_GPUS];
	void UpdateResidency(FD3D12CommandContext& CommandContext);

	uint32 ShaderSlotsPerGeometrySegment = 1;

	// Exclusive prefix sum of instance geometry segments is used to calculate SBT record address from instance and segment indices.
	TArray<uint32> SegmentPrefixSum;
	uint32 NumTotalSegments = 0;
	uint32 GetHitRecordBaseIndex(uint32 InstanceIndex, uint32 SegmentIndex) const { return (SegmentPrefixSum[InstanceIndex] + SegmentIndex) * ShaderSlotsPerGeometrySegment; }

	uint32 TotalPrimitiveCount = 0; // Combined number of primitives in all geometry instances

	uint32 NumCallableShaderSlots = 0;

	// #dxr_todo UE-68230: shader tables should be explicitly registered and unregistered with the scene
	FD3D12RayTracingShaderTable* FindOrCreateShaderTable(const FD3D12RayTracingPipelineState* Pipeline, FD3D12Device* Device);
	FD3D12RayTracingShaderTable* FindExistingShaderTable(const FD3D12RayTracingPipelineState* Pipeline, FD3D12Device* Device) const;

	TMap<const FD3D12RayTracingPipelineState*, FD3D12RayTracingShaderTable*> ShaderTables[MAX_NUM_GPUS];

	ERayTracingSceneLifetime Lifetime = RTSL_SingleFrame;
	uint64 CreatedFrameFenceValue = 0;
};

#endif // D3D12_RHI_RAYTRACING
