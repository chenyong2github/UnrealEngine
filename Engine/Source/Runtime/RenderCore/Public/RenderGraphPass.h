// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphResources.h"
#include "RenderGraphEvent.h"
#include "Containers/SortedMap.h"

class FRDGTransitionQueue
{
public:
	FRDGTransitionQueue() = default;
	FRDGTransitionQueue(uint32 ReservedCount);

	void Insert(const FRHITransition* Transition, ERHITransitionCreateFlags TransitionFlags);
	void Begin(FRHIComputeCommandList& RHICmdList);
	void End(FRHIComputeCommandList& RHICmdList);

private:
	TArray<const FRHITransition*, TInlineAllocator<1, FRDGArrayAllocator>> Queue;
};

struct FRDGBarrierBatchBeginId
{
	FRDGBarrierBatchBeginId() = default;

	bool operator==(FRDGBarrierBatchBeginId Other) const
	{
		return Passes == Other.Passes && PipelinesAfter == Other.PipelinesAfter;
	}

	bool operator!=(FRDGBarrierBatchBeginId Other) const
	{
		return !(*this == Other);
	}

	FRDGPassHandlesByPipeline Passes;
	ERHIPipeline PipelinesAfter = ERHIPipeline::None;
};

RENDERCORE_API uint32 GetTypeHash(FRDGBarrierBatchBeginId Id);

class RENDERCORE_API FRDGBarrierBatchBegin
{
public:
	FRDGBarrierBatchBegin(ERHIPipeline PipelinesToBegin, ERHIPipeline PipelinesToEnd, const TCHAR* DebugName, const FRDGPass* DebugPass);
	FRDGBarrierBatchBegin(ERHIPipeline PipelinesToBegin, ERHIPipeline PipelinesToEnd, const TCHAR* DebugName, FRDGPassHandlesByPipeline DebugPasses);

	void AddTransition(FRDGParentResourceRef Resource, const FRHITransitionInfo& Info);

	void AddAlias(FRDGParentResourceRef Resource, const FRHITransientAliasingInfo& Info);

	void SetUseCrossPipelineFence()
	{
		TransitionFlags = ERHITransitionCreateFlags::None;
		bTransitionNeeded = true;
	}

	void Submit(FRHIComputeCommandList& RHICmdList, ERHIPipeline Pipeline);
	void Submit(FRHIComputeCommandList& RHICmdList, ERHIPipeline Pipeline, FRDGTransitionQueue& TransitionsToBegin);

private:
	const FRHITransition* Transition = nullptr;
	TArray<FRHITransitionInfo, TInlineAllocator<1, FRDGArrayAllocator>> Transitions;
	TArray<FRHITransientAliasingInfo, FRDGArrayAllocator> Aliases;
	ERHITransitionCreateFlags TransitionFlags = ERHITransitionCreateFlags::NoFence;
	bool bTransitionNeeded = false;

	/** These pipelines masks are set at creation time and reset with each submission. */
	ERHIPipeline PipelinesToBegin;
	ERHIPipeline PipelinesToEnd;

#if RDG_ENABLE_DEBUG
	FRDGPassHandlesByPipeline DebugPasses;
	TArray<FRDGParentResource*, FRDGArrayAllocator> DebugTransitionResources;
	TArray<FRDGParentResource*, FRDGArrayAllocator> DebugAliasingResources;
	const TCHAR* DebugName;
	ERHIPipeline DebugPipelinesToBegin;
	ERHIPipeline DebugPipelinesToEnd;
#endif

	friend class FRDGBarrierBatchBegin;
	friend class FRDGBarrierBatchEnd;
	friend class FRDGBarrierValidation;
};

class RENDERCORE_API FRDGBarrierBatchEnd
{
public:
	FRDGBarrierBatchEnd(FRDGPassHandle InPassHandle)
#if RDG_ENABLE_DEBUG
		: PassHandle(InPassHandle)
#endif
	{}

	/** Inserts a dependency on a begin batch. A begin batch can be inserted into more than one end batch. */
	void AddDependency(FRDGBarrierBatchBegin* BeginBatch);

	void Submit(FRHIComputeCommandList& RHICmdList, ERHIPipeline Pipeline);

private:
	TArray<FRDGBarrierBatchBegin*, TInlineAllocator<1, FRDGArrayAllocator>> Dependencies;

#if RDG_ENABLE_DEBUG
	FRDGPassHandle PassHandle;
#endif

	friend class FRDGBarrierValidation;
};

/** Base class of a render graph pass. */
class RENDERCORE_API FRDGPass
{
public:
	FRDGPass(FRDGEventName&& InName, FRDGParameterStruct InParameterStruct, ERDGPassFlags InFlags);
	FRDGPass(const FRDGPass&) = delete;
	virtual ~FRDGPass() = default;

#if RDG_ENABLE_DEBUG
	const TCHAR* GetName() const;
#else
	FORCEINLINE const TCHAR* GetName() const
	{
		return Name.GetTCHAR();
	}
#endif

	FORCEINLINE const FRDGEventName& GetEventName() const
	{
		return Name;
	}

	FORCEINLINE ERDGPassFlags GetFlags() const
	{
		return Flags;
	}

	FORCEINLINE ERHIPipeline GetPipeline() const
	{
		return Pipeline;
	}

	FORCEINLINE FRDGParameterStruct GetParameters() const
	{
		return ParameterStruct;
	}

	FORCEINLINE FRDGPassHandle GetHandle() const
	{
		return Handle;
	}

	bool IsMergedRenderPassBegin() const
	{
		return !bSkipRenderPassBegin && bSkipRenderPassEnd;
	}

	bool IsMergedRenderPassEnd() const
	{
		return bSkipRenderPassBegin && !bSkipRenderPassEnd;
	}

	bool SkipRenderPassBegin() const
	{
		return bSkipRenderPassBegin;
	}

	bool SkipRenderPassEnd() const
	{
		return bSkipRenderPassEnd;
	}

	bool IsAsyncCompute() const
	{
		return Pipeline == ERHIPipeline::AsyncCompute;
	}

	bool IsAsyncComputeBegin() const
	{
		return bAsyncComputeBegin;
	}

	bool IsAsyncComputeEnd() const
	{
		return bAsyncComputeEnd;
	}

	bool IsGraphicsFork() const
	{
		return bGraphicsFork;
	}

	bool IsGraphicsJoin() const
	{
		return bGraphicsJoin;
	}

	const FRDGPassHandleArray& GetProducers() const
	{
		return Producers;
	}

	/** Returns the producer pass on the other pipeline, if it exists. */
	FRDGPassHandle GetCrossPipelineProducer() const
	{
		return CrossPipelineProducer;
	}

	/** Returns the consumer pass on the other pipeline, if it exists. */
	FRDGPassHandle GetCrossPipelineConsumer() const
	{
		return CrossPipelineConsumer;
	}

	/** Returns the graphics pass responsible for forking the async interval this pass is in. */
	FRDGPassHandle GetGraphicsForkPass() const
	{
		return GraphicsForkPass;
	}

	/** Returns the graphics pass responsible for joining the async interval this pass is in. */
	FRDGPassHandle GetGraphicsJoinPass() const
	{
		return GraphicsJoinPass;
	}

#if RDG_CPU_SCOPES
	FRDGCPUScopes GetCPUScopes() const
	{
		return CPUScopes;
	}
#endif

#if RDG_GPU_SCOPES
	FRDGGPUScopes GetGPUScopes() const
	{
		return GPUScopes;
	}
#endif

protected:
	FRDGBarrierBatchBegin& GetPrologueBarriersToBegin(FRDGAllocator& Allocator);
	FRDGBarrierBatchBegin& GetEpilogueBarriersToBeginForGraphics(FRDGAllocator& Allocator);
	FRDGBarrierBatchBegin& GetEpilogueBarriersToBeginForAsyncCompute(FRDGAllocator& Allocator);
	FRDGBarrierBatchBegin& GetEpilogueBarriersToBeginForAll(FRDGAllocator& Allocator);

	FRDGBarrierBatchBegin& GetEpilogueBarriersToBeginFor(FRDGAllocator& Allocator, ERHIPipeline PipelineForEnd)
	{
		switch (PipelineForEnd)
		{
		default: 
			checkNoEntry();
			// fall through

		case ERHIPipeline::Graphics:
			return GetEpilogueBarriersToBeginForGraphics(Allocator);

		case ERHIPipeline::AsyncCompute:
			return GetEpilogueBarriersToBeginForAsyncCompute(Allocator);

		case ERHIPipeline::All:
			return GetEpilogueBarriersToBeginForAll(Allocator);
		}
	}

	FRDGBarrierBatchEnd& GetPrologueBarriersToEnd(FRDGAllocator& Allocator);
	FRDGBarrierBatchEnd& GetEpilogueBarriersToEnd(FRDGAllocator& Allocator);

	//////////////////////////////////////////////////////////////////////////
	//! User Methods to Override

	virtual void ExecuteImpl(FRHIComputeCommandList& RHICmdList) = 0;

	//////////////////////////////////////////////////////////////////////////

	void Execute(FRHIComputeCommandList& RHICmdList);

	// When r.RDG.Debug is enabled, this will include a full namespace path with event scopes included.
	IF_RDG_ENABLE_DEBUG(FString FullPathIfDebug);

	const FRDGEventName Name;
	const FRDGParameterStruct ParameterStruct;
	const ERDGPassFlags Flags;
	const ERHIPipeline Pipeline;
	FRDGPassHandle Handle;

	union
	{
		struct
		{
			/** Whether the render pass begin / end should be skipped. */
			uint32 bSkipRenderPassBegin : 1;
			uint32 bSkipRenderPassEnd : 1;

			/** (AsyncCompute only) Whether this is the first / last async compute pass in an async interval. */
			uint32 bAsyncComputeBegin : 1;
			uint32 bAsyncComputeEnd : 1;

			/** (AsyncCompute only) Whether this is the last async compute pass in the graph. */
			uint32 bAsyncComputeEndExecute : 1;

			/** (Graphics only) Whether this is a graphics fork / join pass. */
			uint32 bGraphicsFork : 1;
			uint32 bGraphicsJoin : 1;

			/** Whether the pass only writes to resources in its render pass. */
			uint32 bRenderPassOnlyWrites : 1;

			/** Whether the pass uses the immediate command list. */
			uint32 bImmediateCommandList : 1;

			/** Whether this pass allocated a texture through the pool. */
			IF_RDG_ENABLE_DEBUG(uint32 bFirstTextureAllocated : 1);
		};
		uint32 PackedBits = 0;
	};

	/** Handle of the latest cross-pipeline producer and earliest cross-pipeline consumer. */
	FRDGPassHandle CrossPipelineProducer;
	FRDGPassHandle CrossPipelineConsumer;

	/** (AsyncCompute only) Graphics passes which are the fork / join for async compute interval this pass is in. */
	FRDGPassHandle GraphicsForkPass;
	FRDGPassHandle GraphicsJoinPass;

	/** The passes which are handling the epilogue / prologue barriers meant for this pass. */
	FRDGPassHandle PrologueBarrierPass;
	FRDGPassHandle EpilogueBarrierPass;

	/** Lists of producer passes. */
	FRDGPassHandleArray Producers;

	struct FTextureState
	{
		FTextureState()
		{
			InitAsWholeResource(State);
		}

		FRDGTextureTransientSubresourceState State;
		FRDGTextureTransientSubresourceStateIndirect MergeState;
		uint16 ReferenceCount = 0;
	};

	struct FBufferState
	{
		FRDGSubresourceState State;
		FRDGSubresourceState* MergeState = nullptr;
		uint16 ReferenceCount = 0;
	};

	/** Maps textures / buffers to information on how they are used in the pass. */
	TSortedMap<FRDGTexture*, FTextureState, FRDGArrayAllocator> TextureStates;
	TSortedMap<FRDGBuffer*, FBufferState, FRDGArrayAllocator> BufferStates;

	/** Lists of pass parameters scheduled for begin during execution of this pass. */
	TArray<FRDGPass*, TInlineAllocator<1, FRDGArrayAllocator>> ResourcesToBegin;
	TArray<FRDGPass*, TInlineAllocator<1, FRDGArrayAllocator>> ResourcesToEnd;

	/** Split-barrier batches at various points of execution of the pass. */
	FRDGBarrierBatchBegin* PrologueBarriersToBegin = nullptr;
	FRDGBarrierBatchEnd* PrologueBarriersToEnd = nullptr;
	FRDGBarrierBatchBegin* EpilogueBarriersToBeginForGraphics = nullptr;
	FRDGBarrierBatchBegin* EpilogueBarriersToBeginForAsyncCompute = nullptr;
	FRDGBarrierBatchBegin* EpilogueBarriersToBeginForAll = nullptr;
	TArray<FRDGBarrierBatchBegin*> SharedEpilogueBarriersToBegin;
	FRDGBarrierBatchEnd* EpilogueBarriersToEnd = nullptr;

	EAsyncComputeBudget AsyncComputeBudget = EAsyncComputeBudget::EAll_4;

#if WITH_MGPU
	FRHIGPUMask GPUMask;
#endif

	IF_RDG_CMDLIST_STATS(TStatId CommandListStat);

	IF_RDG_CPU_SCOPES(FRDGCPUScopes CPUScopes);
	IF_RDG_GPU_SCOPES(FRDGGPUScopes GPUScopes);

#if RDG_GPU_SCOPES && RDG_ENABLE_TRACE
	const FRDGEventScope* TraceEventScope = nullptr;
#endif

#if RDG_ENABLE_TRACE
	TArray<FRDGTextureHandle, FRDGArrayAllocator> TraceTextures;
	TArray<FRDGBufferHandle, FRDGArrayAllocator> TraceBuffers;
#endif

	friend FRDGBuilder;
	friend FRDGPassRegistry;
	friend FRDGTrace;
};

/** Render graph pass with lambda execute function. */
template <typename ParameterStructType, typename ExecuteLambdaType>
class TRDGLambdaPass
	: public FRDGPass
{
	// Verify that the amount of stuff captured by the pass lambda is reasonable.
	static constexpr int32 kMaximumLambdaCaptureSize = 1024;
	static_assert(sizeof(ExecuteLambdaType) <= kMaximumLambdaCaptureSize, "The amount of data of captured for the pass looks abnormally high.");

	template <typename T>
	struct TLambdaTraits
		: TLambdaTraits<decltype(&T::operator())>
	{};
	template <typename ReturnType, typename ClassType, typename ArgType>
	struct TLambdaTraits<ReturnType(ClassType::*)(ArgType&) const>
	{
		using TRHICommandList = ArgType;
	};
	template <typename ReturnType, typename ClassType, typename ArgType>
	struct TLambdaTraits<ReturnType(ClassType::*)(ArgType&)>
	{
		using TRHICommandList = ArgType;
	};
	using TRHICommandList = typename TLambdaTraits<ExecuteLambdaType>::TRHICommandList;

public:
	static const bool kSupportsAsyncCompute = TIsSame<TRHICommandList, FRHIComputeCommandList>::Value;
	static const bool kSupportsRaster = TIsDerivedFrom<TRHICommandList, FRHICommandList>::IsDerived;

	TRDGLambdaPass(
		FRDGEventName&& InName,
		const FShaderParametersMetadata* InParameterMetadata,
		const ParameterStructType* InParameterStruct,
		ERDGPassFlags InPassFlags,
		ExecuteLambdaType&& InExecuteLambda)
		: FRDGPass(MoveTemp(InName), FRDGParameterStruct(InParameterStruct, &InParameterMetadata->GetLayout()), InPassFlags)
		, ExecuteLambda(MoveTemp(InExecuteLambda))
#if RDG_ENABLE_DEBUG
		, DebugParameterStruct(InParameterStruct)
#endif
	{
		checkf(kSupportsAsyncCompute || !EnumHasAnyFlags(InPassFlags, ERDGPassFlags::AsyncCompute),
			TEXT("Pass %s is set to use 'AsyncCompute', but the pass lambda's first argument is not FRHIComputeCommandList&."), GetName());

		bImmediateCommandList = TIsSame<TRHICommandList, FRHICommandListImmediate>::Value;
	}

private:
	void ExecuteImpl(FRHIComputeCommandList& RHICmdList) override
	{
		check(!kSupportsRaster || RHICmdList.IsImmediate());
		ExecuteLambda(static_cast<TRHICommandList&>(RHICmdList));
	}

	ExecuteLambdaType ExecuteLambda;

	IF_RDG_ENABLE_DEBUG(const ParameterStructType* DebugParameterStruct);
};

template <typename ExecuteLambdaType>
class TRDGEmptyLambdaPass
	: public TRDGLambdaPass<FEmptyShaderParameters, ExecuteLambdaType>
{
public:
	TRDGEmptyLambdaPass(FRDGEventName&& InName, ERDGPassFlags InPassFlags, ExecuteLambdaType&& InExecuteLambda)
		: TRDGLambdaPass<FEmptyShaderParameters, ExecuteLambdaType>(MoveTemp(InName), FEmptyShaderParameters::FTypeInfo::GetStructMetadata(), &EmptyShaderParameters, InPassFlags, MoveTemp(InExecuteLambda))
	{}

private:
	FEmptyShaderParameters EmptyShaderParameters;
	friend class FRDGBuilder;
};

/** Render graph pass used for the prologue / epilogue passes. */
class FRDGSentinelPass final
	: public FRDGPass
{
public:
	FRDGSentinelPass(FRDGEventName&& Name)
		: FRDGPass(MoveTemp(Name), FRDGParameterStruct(&EmptyShaderParameters), ERDGPassFlags::NeverCull)
	{}

private:
	void ExecuteImpl(FRHIComputeCommandList&) override {}
	FEmptyShaderParameters EmptyShaderParameters;
};

#include "RenderGraphParameters.inl"