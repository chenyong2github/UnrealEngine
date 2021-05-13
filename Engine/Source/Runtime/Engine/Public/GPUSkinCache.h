// Copyright Epic Games, Inc. All Rights Reserved.
// Copyright (C) Microsoft. All rights reserved.

/*=============================================================================
	GPUSkinCache.h: Performs skinning on a compute shader into a buffer to avoid vertex buffer skinning.
=============================================================================*/

// Requirements
// * Compute shader support (with Atomics)
// * Project settings needs to be enabled (r.SkinCache.CompileShaders)
// * feature need to be enabled (r.SkinCache.Mode)

// Features
// * Skeletal mesh, 4 / 8 weights per vertex, 16/32 index buffer
// * Supports Morph target animation (morph target blending is not done by this code)
// * Saves vertex shader computations when we render an object multiple times (EarlyZ, velocity, shadow, BasePass, CustomDepth, Shadow masking)
// * Fixes velocity rendering (needed for MotionBlur and TemporalAA) for WorldPosOffset animation and morph target animation
// * RecomputeTangents results in improved tangent space for WorldPosOffset animation and morph target animation
// * fixed amount of memory per Scene (r.SkinCache.SceneMemoryLimitInMB)
// * Velocity Rendering for MotionBlur and TemporalAA (test Velocity in BasePass)
// * r.SkinCache.Mode and r.SkinCache.RecomputeTangents can be toggled at runtime

// TODO:
// * Test: Tessellation
// * Quality/Optimization: increase TANGENT_RANGE for better quality or accumulate two components in one 32bit value
// * Bug: UpdateMorphVertexBuffer needs to handle SkinCacheObjects that have been rejected by the SkinCache (e.g. because it was running out of memory)
// * Refactor: Unify the 3 compute shaders to use the same C++ setup code for the variables
// * Optimization: Dispatch calls can be merged for better performance, stalls between Dispatch calls can be avoided (DX11 back door, DX12, console API)
// * Feature: Cloth is not supported yet (Morph targets is a similar code)
// * Feature: Support Static Meshes ?

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "RenderGraphDefinitions.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "UniformBuffer.h"
#include "GPUSkinPublicDefs.h"
#include "VertexFactory.h"

class FGPUSkinPassthroughVertexFactory;
class FGPUBaseSkinVertexFactory;
class FMorphVertexBuffer;
class FSkeletalMeshLODRenderData;
class FSkeletalMeshObjectGPUSkin;
class FSkeletalMeshVertexClothBuffer;
class FVertexOffsetBuffers;
struct FClothSimulData;
struct FSkelMeshRenderSection;
struct FVertexBufferAndSRV;
struct FRayTracingGeometrySegment;

// Can the skin cache be used (ie shaders added, etc)
extern ENGINE_API bool IsGPUSkinCacheAvailable(EShaderPlatform Platform);

extern ENGINE_API bool GPUSkinCacheNeedsDuplicatedVertices();

// Is it actually enabled?
extern ENGINE_API int32 GEnableGPUSkinCache;

class FGPUSkinCacheEntry;

struct FClothSimulEntry
{
	FVector Position;
	FVector Normal;

	/**
	 * Serializer
	 *
	 * @param Ar - archive to serialize with
	 * @param V - vertex to serialize
	 * @return archive that was used
	 */
	friend FArchive& operator<<(FArchive& Ar, FClothSimulEntry& V)
	{
		Ar << V.Position
		   << V.Normal;
		return Ar;
	}
};

struct FGPUSkinBatchElementUserData
{
	FGPUSkinCacheEntry* Entry;
	int32 Section;
};

class FRDGPooledBuffer;
struct FCachedGeometry
{
	struct Section
	{
		FRDGBufferSRVRef RDGPositionBuffer = nullptr;		// Valid when the input comes from a manual skin cache (i.e. skinned run into compute on demand)
		FRHIShaderResourceView* PositionBuffer = nullptr;	// Valid when the input comes from the skin cached (since it is not convert yet to RDG)
		FRHIShaderResourceView* UVsBuffer = nullptr;
		FRHIShaderResourceView* IndexBuffer = nullptr;
		uint32 UVsChannelOffset = 0;
		uint32 UVsChannelCount = 0;
		uint32 NumPrimitives = 0;
		uint32 NumVertices = 0;
		uint32 VertexBaseIndex = 0;
		uint32 IndexBaseIndex = 0;
		uint32 TotalVertexCount = 0;
		uint32 TotalIndexCount = 0;
		uint32 SectionIndex = 0;
		int32 LODIndex = 0;
	};

	int32 LODIndex = 0;
	TArray<Section> Sections;
	FRDGBufferRef DeformedPositionBuffer = nullptr;
};

class FGPUSkinCache
{
public:
	struct FRWBufferTracker;

	enum ESkinCacheInitSettings
	{
		// max 256 bones as we use a byte to index
		MaxUniformBufferBones = 256,
		// Controls the output format on GpuSkinCacheComputeShader.usf
		RWTangentXOffsetInFloats = 0,	// Packed U8x4N
		RWTangentZOffsetInFloats = 1,	// Packed U8x4N

		// 3 ints for normal, 3 ints for tangent, 1 for orientation = 7, rounded up to 8 as it should result in faster math and caching
		IntermediateAccumBufferNumInts = 8,
	};

	struct FDispatchEntry
	{
		FGPUSkinCacheEntry* SkinCacheEntry = nullptr;
		FSkeletalMeshLODRenderData* LODModel = nullptr;

		uint32 RevisionNumber = 0;

		// Section is a uint32, but steal 2 bits since its impossible to have over 1 billion sections
		uint32 Section : 30;	
		uint32 bRequireRecreatingRayTracingGeometry : 1;
		uint32 bAnySegmentUsesWorldPositionOffset : 1;
	};

	FGPUSkinCache() = delete;
	ENGINE_API FGPUSkinCache(ERHIFeatureLevel::Type InFeatureLevel, bool bInRequiresMemoryLimit);
	ENGINE_API ~FGPUSkinCache();

	ENGINE_API FCachedGeometry GetCachedGeometry(uint32 ComponentId) const;
	FCachedGeometry::Section GetCachedGeometry(FGPUSkinCacheEntry* InOutEntry, uint32 SectionId);
	void UpdateSkinWeightBuffer(FGPUSkinCacheEntry* Entry);

	void ProcessEntry(
		FRHICommandListImmediate& RHICmdList, 
		FGPUBaseSkinVertexFactory* VertexFactory,
		FGPUSkinPassthroughVertexFactory* TargetVertexFactory, 
		const FSkelMeshRenderSection& BatchElement, 
		FSkeletalMeshObjectGPUSkin* Skin,
		FVertexOffsetBuffers* VertexOffsetBuffers,
		const FMorphVertexBuffer* MorphVertexBuffer, 
		const FSkeletalMeshVertexClothBuffer* ClothVertexBuffer, 
		const FClothSimulData* SimData,
		const FMatrix& ClothLocalToWorld,
		float ClothBlendWeight, 
		uint32 RevisionNumber, 
		int32 Section, 
		FGPUSkinCacheEntry*& InOutEntry
		);

	static void SetVertexStreams(FGPUSkinCacheEntry* Entry, int32 Section, FRHICommandList& RHICmdList,
		class FRHIVertexShader* ShaderRHI, const FGPUSkinPassthroughVertexFactory* VertexFactory,
		uint32 BaseVertexIndex, FShaderResourceParameter PreviousStreamBuffer);

	static void GetShaderBindings(
		FGPUSkinCacheEntry* Entry,
		int32 Section,
		const FShader* Shader,
		const FGPUSkinPassthroughVertexFactory* VertexFactory,
		uint32 BaseVertexIndex,
		FShaderResourceParameter GPUSkinCachePositionBuffer,
		FShaderResourceParameter GPUSkinCachePreviousPositionBuffer,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams);

	static void Release(FGPUSkinCacheEntry*& SkinCacheEntry);

	static inline FGPUSkinBatchElementUserData* GetFactoryUserData(FGPUSkinCacheEntry* Entry, int32 Section)
	{
		if (Entry)
		{
			return InternalGetFactoryUserData(Entry, Section);
		}
		return nullptr;
	}

#if RHI_RAYTRACING
	static void GetRayTracingSegmentVertexBuffers(const FGPUSkinCacheEntry& SkinCacheEntry, TArrayView<FRayTracingGeometrySegment> OutSegments);
#endif // RHI_RAYTRACING

	static bool IsEntryValid(FGPUSkinCacheEntry* SkinCacheEntry, int32 Section);

	ENGINE_API uint64 GetExtraRequiredMemoryAndReset();

	enum
	{
		NUM_BUFFERS = 2,
	};

	struct FSkinCacheRWBuffer
	{
		FRWBuffer	Buffer;
		ERHIAccess	AccessState = ERHIAccess::Unknown;	// Keep track of current access state

		void Release()
		{
			Buffer.Release();
			AccessState = ERHIAccess::Unknown;
		}

		// Update the access state and return transition info
		FRHITransitionInfo UpdateAccessState(ERHIAccess NewState)
		{
			ERHIAccess OldState = AccessState;
			AccessState = NewState;
			return FRHITransitionInfo(Buffer.UAV.GetReference(), OldState, AccessState);
		}
	};

	struct FRWBuffersAllocation
	{
		friend struct FRWBufferTracker;

		FRWBuffersAllocation(uint32 InNumVertices, bool InWithTangents, bool InUseIntermediateTangents, uint32 InNumTriangles, FRHICommandListImmediate& RHICmdList)
			: NumVertices(InNumVertices), WithTangents(InWithTangents), UseIntermediateTangents(InUseIntermediateTangents), NumTriangles(InNumTriangles)
		{
			for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
			{
				PositionBuffers[Index].Buffer.Initialize(TEXT("SkinCachePositions"), PosBufferBytesPerElement, NumVertices * 3, PF_R32_FLOAT, BUF_Static);
				PositionBuffers[Index].AccessState = ERHIAccess::Unknown;
			}
			if (WithTangents)
			{
				Tangents.Buffer.Initialize(TEXT("SkinCacheTangents"), TangentBufferBytesPerElement, NumVertices * 2, PF_R16G16B16A16_SNORM, BUF_Static);
				Tangents.AccessState = ERHIAccess::Unknown;
				if (UseIntermediateTangents)
				{
					IntermediateTangents.Buffer.Initialize(TEXT("SkinCacheIntermediateTangents"), TangentBufferBytesPerElement, NumVertices * 2, PF_R16G16B16A16_SNORM, BUF_Static);
					IntermediateTangents.AccessState = ERHIAccess::Unknown;
				}
			}
			if (NumTriangles > 0)
			{
				IntermediateAccumulatedTangents.Buffer.Initialize(TEXT("SkinCacheIntermediateAccumulatedTangents"), sizeof(int32), NumTriangles * 3 * FGPUSkinCache::IntermediateAccumBufferNumInts, PF_R32_SINT, BUF_UnorderedAccess);
				IntermediateAccumulatedTangents.AccessState = ERHIAccess::Unknown;
				// The UAV must be zero-filled. We leave it zeroed after each round (see RecomputeTangentsPerVertexPass.usf), so this is only needed on when the buffer is first created.
				RHICmdList.ClearUAVUint(IntermediateAccumulatedTangents.Buffer.UAV, FUintVector4(0, 0, 0, 0));
			}
		}

		~FRWBuffersAllocation()
		{
			for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
			{
				PositionBuffers[Index].Release();
			}
			if (WithTangents)
			{
				Tangents.Release();
				IntermediateTangents.Release();
			}
			if (NumTriangles > 0)
			{
				IntermediateAccumulatedTangents.Release();
			}
		}

		static uint64 CalculateRequiredMemory(uint32 InNumVertices, bool InWithTangents, bool InUseIntermediateTangents, uint32 InNumTriangles)
		{
			uint64 PositionBufferSize = PosBufferBytesPerElement * InNumVertices * 3 * NUM_BUFFERS;
			uint64 TangentBufferSize = InWithTangents ? TangentBufferBytesPerElement * InNumVertices * 2 : 0;
			uint64 IntermediateTangentBufferSize = 0;
			if (InUseIntermediateTangents)
			{
				IntermediateTangentBufferSize = InWithTangents ? TangentBufferBytesPerElement * InNumVertices * 2 : 0;
			}
			uint64 AccumulatedTangentBufferSize = InNumTriangles * 3 * FGPUSkinCache::IntermediateAccumBufferNumInts * sizeof(int32);
			return TangentBufferSize + IntermediateTangentBufferSize + PositionBufferSize + AccumulatedTangentBufferSize;
		}

		uint64 GetNumBytes() const
		{
			return CalculateRequiredMemory(NumVertices, WithTangents, UseIntermediateTangents, NumTriangles);
		}

		FSkinCacheRWBuffer* GetTangentBuffer()
		{
			return WithTangents ? &Tangents : nullptr;
		}

		FSkinCacheRWBuffer* GetIntermediateTangentBuffer()
		{
			return (WithTangents && UseIntermediateTangents) ? &IntermediateTangents : nullptr;
		}

		FSkinCacheRWBuffer* GetIntermediateAccumulatedTangentBuffer()
		{
			return NumTriangles > 0 ? &IntermediateAccumulatedTangents : nullptr;
		}

		void RemoveAllFromTransitionArray(TSet<FSkinCacheRWBuffer*>& BuffersToTransition);

	private:
		// Output of the GPU skinning (ie Pos, Normals)
		FSkinCacheRWBuffer PositionBuffers[NUM_BUFFERS];

		FSkinCacheRWBuffer Tangents;
		FSkinCacheRWBuffer IntermediateTangents;
		FSkinCacheRWBuffer IntermediateAccumulatedTangents;	// Intermediate buffer used to accumulate results of triangle pass to be passed onto vertex pass

		const uint32 NumVertices;
		const bool WithTangents;
		const bool UseIntermediateTangents;
		const uint32 NumTriangles;

		static const uint32 PosBufferBytesPerElement = 4;
		static const uint32 TangentBufferBytesPerElement = 8;
	};

	struct FRWBufferTracker
	{
		FRWBuffersAllocation* Allocation;

		FRWBufferTracker()
			: Allocation(nullptr)
		{
			Reset();
		}

		void Reset()
		{
			for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
			{
				Revisions[Index] = 0;
				BoneBuffers[Index] = nullptr;
			}
		}

		inline uint32 GetNumBytes() const
		{
			return Allocation->GetNumBytes();
		}

		FSkinCacheRWBuffer* Find(const FVertexBufferAndSRV& BoneBuffer, uint32 Revision)
		{
			for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
			{
				if (Revisions[Index] == Revision && BoneBuffers[Index] == &BoneBuffer)
				{
					return &Allocation->PositionBuffers[Index];
				}
			}

			return nullptr;
		}

		FSkinCacheRWBuffer* GetTangentBuffer()
		{
			return Allocation ? Allocation->GetTangentBuffer() : nullptr;
		}

		FSkinCacheRWBuffer* GetIntermediateTangentBuffer()
		{
			return Allocation ? Allocation->GetIntermediateTangentBuffer() : nullptr;
		}

		FSkinCacheRWBuffer* GetIntermediateAccumulatedTangentBuffer()
		{
			return Allocation ? Allocation->GetIntermediateAccumulatedTangentBuffer() : nullptr;
		}

		void Advance(const FVertexBufferAndSRV& BoneBuffer1, uint32 Revision1, const FVertexBufferAndSRV& BoneBuffer2, uint32 Revision2)
		{
			const FVertexBufferAndSRV* InBoneBuffers[2] = { &BoneBuffer1 , &BoneBuffer2 };
			uint32 InRevisions[2] = { Revision1 , Revision2 };

			for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
			{
				bool Needed = false;
				for (int32 i = 0; i < 2; ++i)
				{
					if (Revisions[Index] == InRevisions[i] && BoneBuffers[Index] == InBoneBuffers[i])
					{
						Needed = true;
					}
				}

				if (!Needed)
				{
					Revisions[Index] = Revision1;
					BoneBuffers[Index] = &BoneBuffer1;
					break;
				}
			}
		}

	private:
		uint32 Revisions[NUM_BUFFERS];
		const FVertexBufferAndSRV* BoneBuffers[NUM_BUFFERS];
	};

	ENGINE_API void TransitionAllToReadable(FRHICommandList& RHICmdList);

	ENGINE_API FRWBuffer* GetPositionBuffer(uint32 ComponentId, uint32 SectionIndex) const;
	ENGINE_API FRWBuffer* GetTangentBuffer(uint32 ComponentId, uint32 SectionIndex) const;
	ENGINE_API FRHIShaderResourceView* GetBoneBuffer(uint32 ComponentId, uint32 SectionIndex) const;

#if RHI_RAYTRACING
	void AddRayTracingGeometryToUpdate(FRayTracingGeometry* InRayTracingGeometry, EAccelerationStructureBuildMode InBuildMode);
	ENGINE_API void CommitRayTracingGeometryUpdates(FRHICommandListImmediate& RHICmdList);
	void RemoveRayTracingGeometryUpdate(FRayTracingGeometry* RayTracingGeometry)
	{
		if (RayTracingGeometriesToUpdate.Find(RayTracingGeometry) != nullptr)
		{
			RayTracingGeometriesToUpdate.Remove(RayTracingGeometry);
		}
	}

	void ProcessRayTracingGeometryToUpdate(
		FRHICommandListImmediate& RHICmdList,
		FGPUSkinCacheEntry* SkinCacheEntry,
		FSkeletalMeshLODRenderData& LODModel,
		bool bRequireRecreatingRayTracingGeometry,
		bool bAnySegmentUsesWorldPositionOffset
		);
#endif // RHI_RAYTRACING

	void BeginBatchDispatch(FRHICommandListImmediate& RHICmdList);
	void EndBatchDispatch(FRHICommandListImmediate& RHICmdList);
	bool IsBatchingDispatch() const { return bShouldBatchDispatches; }

	inline ERHIFeatureLevel::Type GetFeatureLevel() const { return FeatureLevel; }

protected:
	void MakeBufferTransitions(FRHICommandListImmediate& RHICmdList, TArray<FSkinCacheRWBuffer*>& Buffers, ERHIAccess ToState);
	void GetBufferUAVs(const TArray<FSkinCacheRWBuffer*>& InBuffers, TArray<FRHIUnorderedAccessView*>& OutUAVs);

	TSet<FSkinCacheRWBuffer*> BuffersToTransitionToRead;
#if RHI_RAYTRACING
	TMap<FRayTracingGeometry*, EAccelerationStructureBuildMode> RayTracingGeometriesToUpdate;
	uint64 RayTracingGeometryMemoryPendingRelease = 0;
#endif // RHI_RAYTRACING

	TArray<FRWBuffersAllocation*> Allocations;
	TArray<FGPUSkinCacheEntry*> Entries;
	TArray<FDispatchEntry> BatchDispatches;

	FRWBuffersAllocation* TryAllocBuffer(uint32 NumVertices, bool WithTangnents, bool UseIntermediateTangents, uint32 NumTriangles, FRHICommandListImmediate& RHICmdList);
	void DoDispatch(FRHICommandListImmediate& RHICmdList);
	void DoDispatch(FRHICommandListImmediate& RHICmdList, FGPUSkinCacheEntry* SkinCacheEntry, int32 Section, int32 RevisionNumber);
	void DispatchUpdateSkinTangents(FRHICommandListImmediate& RHICmdList, FGPUSkinCacheEntry* Entry, int32 SectionIndex, FSkinCacheRWBuffer*& StagingBuffer, bool bTrianglePass);

	void PrepareUpdateSkinning(
		FGPUSkinCacheEntry* Entry, 
		int32 Section, 
		uint32 RevisionNumber, 
		TArray<FSkinCacheRWBuffer*>* OverlappedUAVs
		);

	void DispatchUpdateSkinning(
		FRHICommandListImmediate& RHICmdList, 
		FGPUSkinCacheEntry* Entry, 
		int32 Section, 
		uint32 RevisionNumber
		);

	void Cleanup();
	static void ReleaseSkinCacheEntry(FGPUSkinCacheEntry* SkinCacheEntry);
	static FGPUSkinBatchElementUserData* InternalGetFactoryUserData(FGPUSkinCacheEntry* Entry, int32 Section);
	void InvalidateAllEntries();
	uint64 UsedMemoryInBytes;
	uint64 ExtraRequiredMemory;
	int32 FlushCounter;
	bool bRequiresMemoryLimit;
	bool bShouldBatchDispatches = false;

	// For recompute tangents, holds the data required between compute shaders
	TArray<FSkinCacheRWBuffer> StagingBuffers;
	int32 CurrentStagingBufferIndex;

	ERHIFeatureLevel::Type FeatureLevel;

	static void CVarSinkFunction();
	static FAutoConsoleVariableSink CVarSink;

	void IncrementDispatchCounter(FRHICommandListImmediate& RHICmdList);
	int32 DispatchCounter = 0;

	void PrintMemorySummary() const;
	FString GetSkeletalMeshObjectName(const FSkeletalMeshObjectGPUSkin* GPUSkin) const;
};

DECLARE_STATS_GROUP(TEXT("GPU Skin Cache"), STATGROUP_GPUSkinCache, STATCAT_Advanced);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Num Sections Skinned"), STAT_GPUSkinCache_TotalNumChunks, STATGROUP_GPUSkinCache,);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Num Vertices Skinned"), STAT_GPUSkinCache_TotalNumVertices, STATGROUP_GPUSkinCache,);
DECLARE_MEMORY_STAT_EXTERN(TEXT("Total Memory Bytes Used"), STAT_GPUSkinCache_TotalMemUsed, STATGROUP_GPUSkinCache, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Intermediate buffer for Recompute Tangents"), STAT_GPUSkinCache_TangentsIntermediateMemUsed, STATGROUP_GPUSkinCache, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Num Triangles for Recompute Tangents"), STAT_GPUSkinCache_NumTrianglesForRecomputeTangents, STATGROUP_GPUSkinCache, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Num Sections Processed"), STAT_GPUSkinCache_NumSectionsProcessed, STATGROUP_GPUSkinCache, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Num SetVertexStreams"), STAT_GPUSkinCache_NumSetVertexStreams, STATGROUP_GPUSkinCache, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Num PreGDME"), STAT_GPUSkinCache_NumPreGDME, STATGROUP_GPUSkinCache, );
