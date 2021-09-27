// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "RHIDefinitions.h"
#if RHI_RAYTRACING

#include "PrimitiveSceneInfo.h"
#include "RHI.h"
#include "RHIUtilities.h"

class FScene;
class FViewInfo;
class FRayTracingPipelineState;
class FRHICommandList;
class FRHIRayTracingScene;
class FRHIShaderResourceView;
class FRHIUniformBuffer;
class FRHIUnorderedAccessView;
struct FRWBuffer;
struct FNiagaraDataInterfaceProxy;


//////////////////////////////////////////////////////////////////////////
//TODO: Move scratch pad buffers to core rendering code along side global dynamic read buffers etc.

/** Allocates space from a pool of fixed size static RWBuffers.
*	Allocations are valid between calls to Reset.
*	Useful for transient working data generated in one dispatch and consumed in others.
*	All buffers in the same scratch pad are transitioned as one.
*/
class NIAGARASHADER_API FGPUScratchPad
{
public:
	struct FAllocation
	{
		FRWBuffer* Buffer = nullptr;
		uint32 Offset = 0;

		FORCEINLINE bool IsValid()const { return Buffer != nullptr; }
	};

	struct FGPUScratchBuffer
	{
	public:
		struct FBuffer
		{
			FRWBuffer Buffer;
			uint32 Used = 0;
		};

		FGPUScratchBuffer(uint32 InBucketSize, EPixelFormat InPixelFormat, uint32 InBytesPerElement, EBufferUsageFlags InBufferUsageFlags)
			: MinBucketSize(InBucketSize), PixelFormat(InPixelFormat), BytesPerElement(InBytesPerElement), BufferUsageFlags(InBufferUsageFlags)
		{}

		/** Allocates NumElements from the buffers. Will try to find an existing buffer to fit the allocation but will create a new one if needed. */
		bool Allocate(uint32 NumElements, FString& BufferDebugName, FRHICommandList& RHICmdList, FGPUScratchPad::FAllocation& OutAllocation)
		{
			//Look for a buffer to fit this allocation.
			FBuffer* ToUse = nullptr;
			for (FBuffer& Buffer : Buffers)
			{
				uint32 NumBufferElements = Buffer.Buffer.NumBytes / BytesPerElement;
				if (NumElements + Buffer.Used <= NumBufferElements)
				{
					ToUse = &Buffer;
				}
			}

			bool bNew = false;
			if (ToUse == nullptr)
			{
				//Allocate a new buffer that will fit this allocation.
				FBuffer* NewBuffer = new FBuffer();
				Buffers.Add(NewBuffer);

				//Round up the size to pow 2 to ensure a slowly growing user doesn't just keep allocating new buckets. Probably a better guess size here than power of 2.
				uint32 NewBufferSize = FPlatformMath::RoundUpToPowerOfTwo(FMath::Max(MinBucketSize, NumElements));

				NewBuffer->Buffer.Initialize(*BufferDebugName, BytesPerElement, NewBufferSize, PixelFormat, CurrentAccess, BufferUsageFlags);
			
				ToUse = NewBuffer;

				NumAllocatedBytes += NewBuffer->Buffer.NumBytes;

				bNew = true;
			}

			OutAllocation.Buffer = &ToUse->Buffer;
			OutAllocation.Offset = ToUse->Used;
			ToUse->Used += NumElements;
			NumUsedBytes += NumElements * BytesPerElement;
			return bNew;
		}

		/** Resets the buffer and releases it's internal RWBuffers too. */
		void Release()
		{
			Reset();
			CurrentAccess = ERHIAccess::UAVCompute;
			for (FBuffer& Buffer : Buffers)
			{
				Buffer.Buffer.Release();
				Buffer.Used = 0;
			}
			Buffers.Reset();

			NumAllocatedBytes = 0;
		}

		/** Resets the usage for all buffers to zero. */
		void Reset()
		{
			for (FBuffer& Buffer : Buffers)
			{
				Buffer.Used = 0;
			}

			NumUsedBytes = 0;
		}

		template<typename TFunc>
		void ForEachBuffer(TFunc Func)
		{
			for (FBuffer& Buffer : Buffers)
			{
				Func(Buffer.Buffer);
			}
		}

		/** Transitions all buffers in the scratch pad. */
		void Transition(FRHICommandList& RHICmdList, ERHIAccess InPreviousState, ERHIAccess InNewState, EResourceTransitionFlags InFlags = EResourceTransitionFlags::None)
		{
			//Store off current access state so we can place any new buffers in the same state.
			CurrentAccess = InNewState;

 			TArray<FRHITransitionInfo, TMemStackAllocator<>> Transitions;
 			Transitions.Reserve(Buffers.Num());
 			for (FBuffer& Buffer : Buffers)
 			{
 				Transitions.Emplace(Buffer.Buffer.UAV, InPreviousState, InNewState, InFlags);
 			}
 
 			RHICmdList.Transition(Transitions);
		}

		/** Returns the total bytes allocated for all buffers in the scratch pad. */
		FORCEINLINE uint32 AllocatedBytes()const
		{
			return NumAllocatedBytes;
		}

		/** Returns the number of used bytes allocated for all buffers in the scratch pad. */
		FORCEINLINE uint32 UsedBytes()const
		{
			return NumUsedBytes;
		}

		FORCEINLINE ERHIAccess GetExpectedCurrentAccess()const{return CurrentAccess;}
	private:

		uint32 MinBucketSize = 4096;
		TIndirectArray<FBuffer> Buffers;

		EPixelFormat PixelFormat;
		uint32 BytesPerElement = 0;
		EBufferUsageFlags BufferUsageFlags = EBufferUsageFlags::None;
		ERHIAccess CurrentAccess = ERHIAccess::UAVCompute;

		uint32 NumAllocatedBytes = 0;
		uint32 NumUsedBytes = 0;
	};

	FGPUScratchPad(uint32 InBucketSize, const FString& InDebugName, EBufferUsageFlags BufferUsageFlags = BUF_Static)
		: BucketSize(InBucketSize)
		, DebugName(InDebugName)
		, ScratchBufferFloat(BucketSize, PF_R32_FLOAT, sizeof(float), BufferUsageFlags)
		, ScratchBufferUInt(BucketSize, PF_R32_UINT, sizeof(uint32), BufferUsageFlags)
	{
	}

	/** Allocate a number of elements from the scratch pad. This will attempt to allocate from an existing buffer but will create a new one if it can't. */
	template<typename T>
	FORCEINLINE FAllocation Alloc(uint32 Size, FRHICommandList& RHICmdList, bool bClearNew);

	/** Resets the scratch pad and also releases all buffers it holds. */
	void Release()
	{
		ScratchBufferFloat.Release();
		ScratchBufferUInt.Release();
	}

	/** Resets the usage counters for buffers in this scratch pad, invalidating all previous allocations. Does not affect actual allocations. Optionally clears all buffers to zero. */
	void Reset(FRHICommandList& RHICmdList, bool bClear)
	{
		ScratchBufferFloat.Reset();
		ScratchBufferUInt.Reset();

		if (bClear)
		{
			Transition(RHICmdList, ERHIAccess::Unknown, ERHIAccess::UAVCompute);

			auto ClearFloatBuffer = [&](FRWBuffer& Buffer)
			{
				RHICmdList.ClearUAVFloat(Buffer.UAV, FVector4f(0.0f));
			};
			ScratchBufferFloat.ForEachBuffer(ClearFloatBuffer);

			auto ClearUIntBuffer = [&](FRWBuffer& Buffer)
			{
				RHICmdList.ClearUAVUint(Buffer.UAV, FUintVector4(0, 0, 0, 0));
			};
			ScratchBufferUInt.ForEachBuffer(ClearUIntBuffer);

			Transition(RHICmdList, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute);
		}
	}

	/** Transitions all buffers in the scratch pad. */
	void Transition(FRHICommandList& RHICmdList, ERHIAccess InPreviousState, ERHIAccess InNewState, EResourceTransitionFlags InFlags = EResourceTransitionFlags::None)
	{
		ScratchBufferFloat.Transition(RHICmdList, InPreviousState, InNewState, InFlags);
		ScratchBufferUInt.Transition(RHICmdList, InPreviousState, InNewState, InFlags);
	}

	/** Returns the total bytes allocated for all buffers in the scratch pad. */
	FORCEINLINE uint32 AllocatedBytes()const
	{
		return ScratchBufferFloat.AllocatedBytes() + ScratchBufferUInt.AllocatedBytes();
	}

	/** Returns the number of used bytes allocated for all buffers in the scratch pad. */
	FORCEINLINE uint32 UsedBytes()const
	{
		return ScratchBufferFloat.UsedBytes() + ScratchBufferUInt.UsedBytes();
	}

private:

	/** Size of each FRWBuffer in the pool. */
	uint32 BucketSize = 4096;
	FString DebugName;

	FGPUScratchBuffer ScratchBufferFloat;
	FGPUScratchBuffer ScratchBufferUInt;
};

template<>
FORCEINLINE FGPUScratchPad::FAllocation FGPUScratchPad::Alloc<float>(uint32 Size, FRHICommandList& RHICmdList, bool bClearNew)
{
	FGPUScratchPad::FAllocation Allocation;
	bool bNewBuffer = ScratchBufferFloat.Allocate(Size, DebugName, RHICmdList, Allocation);
	if (bNewBuffer && bClearNew)
	{
		RHICmdList.Transition(FRHITransitionInfo(Allocation.Buffer->UAV, ScratchBufferFloat.GetExpectedCurrentAccess(), ERHIAccess::UAVCompute));
		RHICmdList.ClearUAVFloat(Allocation.Buffer->UAV, FVector4f(0.0f));
		RHICmdList.Transition(FRHITransitionInfo(Allocation.Buffer->UAV, ERHIAccess::UAVCompute, ScratchBufferFloat.GetExpectedCurrentAccess()));
	}
	return Allocation;
}

template<>
FORCEINLINE FGPUScratchPad::FAllocation FGPUScratchPad::Alloc<uint32>(uint32 Size, FRHICommandList& RHICmdList, bool bClearNew)
{
	FGPUScratchPad::FAllocation Allocation;
	bool bNewBuffer = ScratchBufferUInt.Allocate(Size, DebugName, RHICmdList, Allocation);
	if (bNewBuffer && bClearNew)
	{
		RHICmdList.Transition(FRHITransitionInfo(Allocation.Buffer->UAV, ScratchBufferUInt.GetExpectedCurrentAccess(), ERHIAccess::UAVCompute));
		RHICmdList.ClearUAVUint(Allocation.Buffer->UAV, FUintVector4(0, 0, 0, 0));
		RHICmdList.Transition(FRHITransitionInfo(Allocation.Buffer->UAV, ERHIAccess::UAVCompute, ScratchBufferUInt.GetExpectedCurrentAccess()));
	}
	return Allocation;
}

//////////////////////////////////////////////////////////////////////////

/** Behaves similar to FGPUScratchPad but for use with RWStructuredBuffers
*/
template<typename T>
class NIAGARASHADER_API FGPUScratchPadStructured
{
public:
	struct NIAGARASHADER_API FAllocation
	{
		FRWBufferStructured* Buffer = nullptr;
		uint32 Offset = 0;

		FORCEINLINE bool IsValid()const { return Buffer != nullptr; }
	};

	struct NIAGARASHADER_API FBuffer
	{
		FRWBufferStructured Buffer;
		uint32 Used = 0;
	};

	FGPUScratchPadStructured(uint32 InBucketSize, const FString& InDebugName, EBufferUsageFlags InBufferUsageFlags = EBufferUsageFlags::Static | EBufferUsageFlags::UnorderedAccess)
		: MinBucketSize(InBucketSize)
		, DebugName(InDebugName)
		, BufferUsageFlags(InBufferUsageFlags)
	{
	}

	/** Allocates NumElements from the scratch pad. Will try to allocate from an existing buffer but will create a new one if needed. */
	FAllocation Alloc(uint32 NumElements)
	{
		//Look for a buffer to fit this allocation.
		FBuffer* ToUse = nullptr;
		for (FBuffer& Buffer : Buffers)
		{
			uint32 NumBufferElements = Buffer.Buffer.NumBytes / sizeof(T);
			if (NumElements + Buffer.Used < NumBufferElements)
			{
				ToUse = &Buffer;
			}
		}

		if (ToUse == nullptr)
		{
			//Allocate a new buffer that will fit this allocation.

			FBuffer* NewBuffer = new FBuffer();
			Buffers.Add(NewBuffer);

			//Round up the size to pow 2 to ensure a slowly growing user doesn't just keep allocating new buckets. Probably a better guess size here than power of 2.
			uint32 NewBufferSize = FPlatformMath::RoundUpToPowerOfTwo(FMath::Max(MinBucketSize, NumElements));

			NewBuffer->Buffer.Initialize(
				*DebugName,
				sizeof(T),
				NewBufferSize,
				BufferUsageFlags,
				false /*bUseUavCounter*/,
				false /*bAppendBuffer*/,
				CurrentAccess);

			ToUse = NewBuffer;
			NumAllocatedBytes += NewBuffer->Buffer.NumBytes;
		}

		FAllocation Allocation;
		Allocation.Buffer = &ToUse->Buffer;
		Allocation.Offset = ToUse->Used;
		ToUse->Used += NumElements;
		NumUsedBytes += NumElements * sizeof(T);
		return Allocation;
	}

	/** Resets the scratch pad and releases all buffers it holds. */
	void Release()
	{
		Reset();
		CurrentAccess = ERHIAccess::UAVCompute;
		for (FBuffer& Buffer : Buffers)
		{
			Buffer.Buffer.Release();
			Buffer.Used = 0;
		}
		Buffers.Reset();

		NumAllocatedBytes = 0;
	}

	/** Resets the scratch pad, invalidating all previous allocations. Does not modify actual RWBuffer allocations. */
	void Reset()
	{
		for (FBuffer& Buffer : Buffers)
		{
			Buffer.Used = 0;
		}

		NumUsedBytes = 0;
	}

	/** Transitions all buffers in the scratch pad. */
	void Transition(FRHICommandList& RHICmdList, ERHIAccess InPreviousState, ERHIAccess InNewState, EResourceTransitionFlags InFlags = EResourceTransitionFlags::None)
	{
		//Store off current access state so we can place any new buffers in the same state.
		CurrentAccess = InNewState;

 		TArray<FRHITransitionInfo, TMemStackAllocator<>> Transitions;
 		Transitions.Reserve(Buffers.Num());
 		for (FBuffer& Buffer : Buffers)
 		{
 			Transitions.Emplace(Buffer.Buffer.UAV, InPreviousState, InNewState, InFlags);
 		}

 		RHICmdList.Transition(Transitions);
	}

	/** Returns the total bytes allocated for all buffers in the scratch pad. */
	FORCEINLINE uint32 AllocatedBytes()const
	{
		return NumAllocatedBytes;
	}

	/** Returns the number of used bytes allocated for all buffers in the scratch pad. */
	FORCEINLINE uint32 UsedBytes()const
	{
		return NumUsedBytes;
	}

private:

	/** Size of each FRWBuffer in the pool. */
	uint32 MinBucketSize = 4096;
	TIndirectArray<FBuffer> Buffers;
	FString DebugName;
	EBufferUsageFlags BufferUsageFlags = EBufferUsageFlags::None;
	ERHIAccess CurrentAccess = ERHIAccess::UAVCompute;

	uint32 NumAllocatedBytes = 0;
	uint32 NumUsedBytes = 0;
};

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

/** Holds all information on a single RayTracing dispatch. */
struct FNiagaraRayTraceDispatchInfo
{
	/** Buffer allocation for ray requests to trace. */
	FGPUScratchPadStructured<FNiagaraRayData>::FAllocation RayRequests;
	/** Buffer allocation for writing trace results into. */
	FGPUScratchPadStructured<FNiagaraRayTracingResult>::FAllocation RayTraceIntersections;
	/** Buffer allocation for last frames trace results to read from in this frame's simulation. */
	FGPUScratchPadStructured<FNiagaraRayTracingResult>::FAllocation LastFrameRayTraceIntersections;
	/** Buffer allocation for ray trace counts. Accumulated in simulations shaders and used as dispatch args for ray tracing shaders. */
	FGPUScratchPad::FAllocation RayCounts;
	/** Total possible rays to trace. This may be significantly higher than the actual rays request we accumulate into RayCounts. */
	uint32 MaxRays = 0;
	/** Max number or times rays can be re-traced in a shader if they hit something that is invalid/filtered. */
	uint32 MaxRetraces = 0;

	FORCEINLINE bool IsValid() const { return MaxRays > 0; }
	FORCEINLINE void Reset()
	{
		RayRequests = FGPUScratchPadStructured<FNiagaraRayData>::FAllocation();
		RayTraceIntersections = FGPUScratchPadStructured<FNiagaraRayTracingResult>::FAllocation();
		RayCounts = FGPUScratchPad::FAllocation();
		MaxRays = 0;
		MaxRetraces = 0;
	}
};

class NIAGARASHADER_API FNiagaraRayTracingHelper
{
public:
		FNiagaraRayTracingHelper(EShaderPlatform InShaderPlatform);
		FNiagaraRayTracingHelper() = delete;
		~FNiagaraRayTracingHelper();

		void Reset();
		void BuildRayTracingSceneInfo(FRHICommandList& RHICmdList, TConstArrayView<FViewInfo> Views);

		void IssueRayTraces(FRHICommandList& RHICmdList, FScene* Scene);
		void IssueRayTraces(FRHICommandList& RHICmdList, FScene* Scene, const FIntPoint& RayTraceCounts, uint32 MaxRetraces, FRHIShaderResourceView* RayTraceRequests, FRWBuffer* IndirectArgsBuffer, uint32 IndirectArgsOffset, FRHIUnorderedAccessView* RayTraceResults);
		bool IsValid() const;
		
		void BeginFrame(FRHICommandList& RHICmdList, bool HasRayTracingScene);
		void EndFrame(FRHICommandList& RHICmdList, bool HasRayTracingScene, FScene* Scene);

		/** Accumulates ray requests from user DIs into single dispatches per DI. */
		void AddToDispatch(FNiagaraDataInterfaceProxy* DispatchKey, uint32 MaxRays, int32 MaxRetraces);

		/** Ensures that the final buffers for the provided proxy have been allocated. */
		void BuildDispatch(FRHICommandList& RHICmdList, FNiagaraDataInterfaceProxy* DispatchKey);

		/** Ensures that buffers for a dummy buffer have been allocated. */
		void BuildDummyDispatch(FRHICommandList& RHICmdList);

		/** Returns the final buffers for each DI for use in simulations and RT dispatches. */
		const FNiagaraRayTraceDispatchInfo& GetDispatch(FNiagaraDataInterfaceProxy* DispatchKey) const;

		/** Returns a dummy dispatch to allow shaders to still bind to valid buffers. */
		const FNiagaraRayTraceDispatchInfo& GetDummyDispatch() const;

		/** Adds a primitive to a collision group. This data is sent to the GPU on the start of the next frame. */
		void SetPrimitiveCollisionGroup(FPrimitiveSceneInfo& Primitive, uint32 CollisionGroup);

		/** Pushes cpu side copy of the collision group map to the GPU. */
		void UpdateCollisionGroupMap(FRHICommandList& RHICmdList, FScene* Scene, ERHIFeatureLevel::Type FeatureLevel);

		void RefreshPrimitiveInstanceData();

		FGPUScratchPadStructured<FNiagaraRayData>& GetRayRequests(){ return RayRequests; }
		FGPUScratchPadStructured<FNiagaraRayTracingResult> GetRayTraceIntersections() { return RayTraceIntersections; }
		FGPUScratchPad GetRayTraceCounts() { return RayTraceCounts; }

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

		/** Scratch buffer holding all ray trace requests for all simulations in the scene. */
		FGPUScratchPadStructured<FNiagaraRayData> RayRequests;
		/** Scratch buffer holding all ray trace results for all simulations in the scene. */
		FGPUScratchPadStructured<FNiagaraRayTracingResult> RayTraceIntersections;
		/** Scratch buffer holding all accumulated ray request counts for all simulations in the scene. */
		FGPUScratchPad RayTraceCounts;

		/** Map of DI Proxy to Dispatch Info. Each DI will have a single dispatch for all it's instances. */
		TMap<FNiagaraDataInterfaceProxy*, FNiagaraRayTraceDispatchInfo> Dispatches;

		/** Last frame's DI proxy map. Allows use to retrieve the buffer allocations to read last frames trace results. TODO: Improve. Ideally don't need a map copy here. */
		TMap<FNiagaraDataInterfaceProxy*, FNiagaraRayTraceDispatchInfo> PreviousFrameDispatches;

		/** In places where ray tracing is disabled etc we use a dummy dispatch allocation set so the simulations can bind to valid buffers. */
		FNiagaraRayTraceDispatchInfo DummyDispatch;
};

#endif
