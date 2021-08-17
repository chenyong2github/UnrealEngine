// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "RHIDefinitions.h"

#if RHI_RAYTRACING

class FViewInfo;
class FRayTracingPipelineState;
class FRHICommandList;
class FRHIRayTracingScene;
class FRHIShaderResourceView;
class FRHIUniformBuffer;
class FRHIUnorderedAccessView;
struct FRWBuffer;

//c++ mirror of the ray struct defined in NiagaraRayTracingCommon.ush
struct FNiagaraRayData : public FBasicRayData
{
	int32 CollisionGroup;
};

// c++ mirror of the struct defined in NiagaraRayTracingCommon.ush
struct FNiagaraRayTracingPayload
{
	float HitT;
	uint32 PrimitiveIndex;
	uint32 InstanceIndex;
	float Barycentrics[2];
	float WorldPosition[3];
	float WorldNormal[3];
};

struct FNiagaraRayTracingResult
{
	float HitT;

	float WorldPosition[3];
	float WorldNormal[3];
};

class NIAGARASHADER_API FNiagaraRayTracingHelper
{
	public:
		FNiagaraRayTracingHelper(EShaderPlatform InShaderPlatform)
		: ShaderPlatform(InShaderPlatform)
		{}

		FNiagaraRayTracingHelper() = delete;

		void Reset();
		void BuildRayTracingSceneInfo(FRHICommandList& RHICmdList, TConstArrayView<FViewInfo> Views);
		void IssueRayTraces(FRHICommandList& RHICmdList, FScene* Scene, const FIntPoint& RayTraceCounts, uint32 MaxRetraces, FRHIShaderResourceView* RayTraceRequests, FRWBuffer* IndirectArgsBuffer, uint32 IndirectArgsOffset, FRHIUnorderedAccessView* RayTraceResults) const;
		bool IsValid() const;

		/** Adds a primitive to a collision group. This data is sent to the GPU on the start of the next frame. */
		void SetPrimitiveCollisionGroup(FPrimitiveSceneInfo& Primitive, uint32 CollisionGroup);

		/** Pushes cpu side copy of the collision group map to the GPU. */
		void UpdateCollisionGroupMap(FRHICommandList& RHICmdList, FScene* Scene, ERHIFeatureLevel::Type FeatureLevel);

		void RefreshPrimitiveInstanceData();
	private:
		const EShaderPlatform ShaderPlatform;

		mutable FRayTracingPipelineState* RayTracingPipelineState = nullptr;
		mutable FRHIRayTracingScene* RayTracingScene = nullptr;
		mutable FRHIShaderResourceView* RayTracingSceneView = nullptr;
		mutable FRHIUniformBuffer* ViewUniformBuffer = nullptr;

		/**
		CPU side copy of the PrimID to Collision Group Map.
		This is uploaded to the GPU and used in Niagara RG shader to filter self collisions between objects of the same group.
		*/
		TMap<FPrimitiveComponentId, uint32> CollisionGroupMap;
		
		/** Hash table. 
		 PrimIdHashTable is the main hash table that maps GPUSceneInstanceIndex to and Index we can use to store Collision Groups inside HashToCollisionGroups.
*/
		FRWBuffer PrimIdHashTable;
		uint32 HashTableSize = 0;
		FRWBuffer HashToCollisionGroups;

		bool bCollisionGroupMapDirty = true;
};

#endif
