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

	ENGINE_API FGPUSkinCache(bool bInRequiresMemoryLimit);
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

	static bool UseIntermediateTangents();

	inline uint64 GetExtraRequiredMemoryAndReset()
	{
		uint64 OriginalValue = ExtraRequiredMemory;
		ExtraRequiredMemory = 0;
		return OriginalValue;
	}

	enum
	{
		NUM_BUFFERS = 2,
	};

	struct FRWBuffersAllocation
	{
		friend struct FRWBufferTracker;

		FRWBuffersAllocation(uint32 InNumVertices, bool InWithTangents)
			: NumVertices(InNumVertices), WithTangents(InWithTangents)
		{
			for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
			{
				RWBuffers[Index].Initialize(4, NumVertices * 3, PF_R32_FLOAT, BUF_Static, TEXT("SkinCacheVertices"));
			}
			if (WithTangents)
			{
				Tangents.Initialize(8, NumVertices * 2, PF_R16G16B16A16_SNORM, BUF_Static, TEXT("SkinCacheTangents"));
				if (FGPUSkinCache::UseIntermediateTangents())
				{
					IntermediateTangents.Initialize(8, NumVertices * 2, PF_R16G16B16A16_SNORM, BUF_Static, TEXT("SkinCacheIntermediateTangents"));
				}
			}
		}

		~FRWBuffersAllocation()
		{
			for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
			{
				RWBuffers[Index].Release();
			}
			if (WithTangents)
			{
				Tangents.Release();
				IntermediateTangents.Release();
			}
		}

		static uint64 CalculateRequiredMemory(uint32 NumVertices, bool WithTangents)
		{
			uint64 PositionBufferSize = 4 * 3 * NumVertices * NUM_BUFFERS;
			uint64 TangentBufferSize = WithTangents ? 2 * 4 * NumVertices : 0;
			uint64 IntermediateTangentBufferSize = 0;
			if (FGPUSkinCache::UseIntermediateTangents())
			{
				IntermediateTangentBufferSize = WithTangents ? 2 * 4 * NumVertices : 0;
			}
			return TangentBufferSize + IntermediateTangentBufferSize + PositionBufferSize;
		}

		uint64 GetNumBytes() const
		{
			return CalculateRequiredMemory(NumVertices, WithTangents);
		}

		FRWBuffer* GetTangentBuffer()
		{
			return WithTangents ? &Tangents : nullptr;
		}

		FRWBuffer* GetIntermediateTangentBuffer()
		{
			return WithTangents ? &IntermediateTangents : nullptr;
		}

		void RemoveAllFromTransitionArray(TSet<FRHIUnorderedAccessView*>& BuffersToTransition);

	private:
		// Output of the GPU skinning (ie Pos, Normals)
		FRWBuffer RWBuffers[NUM_BUFFERS];

		FRWBuffer Tangents;
		FRWBuffer IntermediateTangents;
		const uint32 NumVertices;
		const bool WithTangents;
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

		FRWBuffer* Find(const FVertexBufferAndSRV& BoneBuffer, uint32 Revision)
		{
			for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
			{
				if (Revisions[Index] == Revision && BoneBuffers[Index] == &BoneBuffer)
				{
					return &Allocation->RWBuffers[Index];
				}
			}

			return nullptr;
		}

		FRWBuffer* GetTangentBuffer()
		{
			return Allocation->GetTangentBuffer();
		}

		FRWBuffer* GetIntermediateTangentBuffer()
		{
			return Allocation->GetIntermediateTangentBuffer();
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

#if RHI_RAYTRACING
	void AddRayTracingGeometryToUpdate(FRayTracingGeometry* RayTracingGeometry)
	{
		RayTracingGeometriesToUpdate.Add(RayTracingGeometry);
	}

	void CommitRayTracingGeometryUpdates(FRHICommandList& RHICmdList);

	void RemoveRayTracingGeometryUpdate(FRayTracingGeometry* RayTracingGeometry)
	{
		if (RayTracingGeometriesToUpdate.Find(RayTracingGeometry) != nullptr)
			RayTracingGeometriesToUpdate.Remove(RayTracingGeometry);
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

protected:

	void AddBufferToTransition(FRHIUnorderedAccessView* InUAV);

	TSet<FRHIUnorderedAccessView*> BuffersToTransition;
#if RHI_RAYTRACING
	TSet<FRayTracingGeometry*> RayTracingGeometriesToUpdate;
	uint64 RayTracingGeometryMemoryPendingRelease = 0;
#endif // RHI_RAYTRACING

	TArray<FRWBuffersAllocation*> Allocations;
	TArray<FGPUSkinCacheEntry*> Entries;
	TArray<FDispatchEntry> BatchDispatches;

	FRWBuffersAllocation* TryAllocBuffer(uint32 NumVertices, bool WithTangnents);
	void DoDispatch(FRHICommandListImmediate& RHICmdList);
	void DoDispatch(FRHICommandListImmediate& RHICmdList, FGPUSkinCacheEntry* SkinCacheEntry, int32 Section, int32 RevisionNumber);
	void DispatchUpdateSkinTangents(FRHICommandListImmediate& RHICmdList, FGPUSkinCacheEntry* Entry, int32 SectionIndex);

	void PrepareUpdateSkinning(
		FGPUSkinCacheEntry* Entry, 
		int32 Section, 
		uint32 RevisionNumber, 
		TArray<FRHIUnorderedAccessView*>* OverlappedUAVs
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
	TArray<FRWBuffer> StagingBuffers;
	int32 CurrentStagingBufferIndex;

	static void CVarSinkFunction();
	static FAutoConsoleVariableSink CVarSink;
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
