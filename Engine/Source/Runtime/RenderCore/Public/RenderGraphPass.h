// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphResources.h"
#include "RenderGraphEvent.h"
#include "Containers/SortedMap.h"

class RENDERCORE_API FRDGBarrierBatch
{
public:
	FRDGBarrierBatch(const FRDGBarrierBatch&) = delete;

	bool IsSubmitted() const
	{
		return bSubmitted;
	}

	FString GetName() const;

protected:
	FRDGBarrierBatch(const FRDGPass* InPass, const TCHAR* InName);

	void SetSubmitted();

	ERHIPipeline GetPipeline() const
	{
		return Pipeline;
	}

private:
	bool bSubmitted = false;
	ERHIPipeline Pipeline;

#if RDG_ENABLE_DEBUG
	const FRDGPass* Pass;
	const TCHAR* Name;
#endif
};

class RENDERCORE_API FRDGBarrierBatchBegin final : public FRDGBarrierBatch
{
public:
	FRDGBarrierBatchBegin(const FRDGPass* InPass, const TCHAR* InName, TOptional<ERHIPipeline> InOverridePipelineForEnd = {});
	~FRDGBarrierBatchBegin();

	/** Adds a resource transition into the batch. */
	void AddTransition(FRDGParentResourceRef Resource, const FRHITransitionInfo& Info);

	const FRHITransition* GetTransition() const
	{
		return Transition;
	}

	bool IsTransitionValid() const
	{
		return Transition != nullptr;
	}

	void SetUseCrossPipelineFence()
	{
		check(!bUseCrossPipelineFence);
		bUseCrossPipelineFence = true;
	}

	void Submit(FRHIComputeCommandList& RHICmdList);

private:
	TOptional<ERHIPipeline> OverridePipelineToEnd;
	bool bUseCrossPipelineFence = false;

	/** The transition to store after submission. It is assigned back to null by the end batch. */
	const FRHITransition* Transition = nullptr;

	/** An array of asynchronous resource transitions to perform. */
	TArray<FRHITransitionInfo, TInlineAllocator<1, SceneRenderingAllocator>> Transitions;

#if RDG_ENABLE_DEBUG
	/** An array of RDG resources matching with the Transitions array. For debugging only. */
	TArray<FRDGParentResource*, SceneRenderingAllocator> Resources;
#endif

	friend class FRDGBarrierBatchEnd;
	friend class FRDGBarrierValidation;
};

class RENDERCORE_API FRDGBarrierBatchEnd final : public FRDGBarrierBatch
{
public:
	FRDGBarrierBatchEnd(const FRDGPass* InPass, const TCHAR* InName)
		: FRDGBarrierBatch(InPass, InName)
	{}

	~FRDGBarrierBatchEnd();

	void ReserveMemory(uint32 ExpectedDependencyCount);

	/** Inserts a dependency on a begin batch. A begin batch can be inserted into more than one end batch. */
	void AddDependency(FRDGBarrierBatchBegin* BeginBatch);

	void Submit(FRHIComputeCommandList& RHICmdList);

private:
	TArray<FRDGBarrierBatchBegin*, TInlineAllocator<1, SceneRenderingAllocator>> Dependencies;

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

private:
	FRDGBarrierBatchBegin& GetPrologueBarriersToBegin(FRDGAllocator& Allocator);
	FRDGBarrierBatchEnd& GetPrologueBarriersToEnd(FRDGAllocator& Allocator);
	FRDGBarrierBatchBegin& GetEpilogueBarriersToBeginForGraphics(FRDGAllocator& Allocator);
	FRDGBarrierBatchBegin& GetEpilogueBarriersToBeginForAsyncCompute(FRDGAllocator& Allocator);

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
		}
	}

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

			/** Whether the pass writes to a UAV. */
			uint32 bUAVAccess : 1;

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
	TSortedMap<FRDGTexture*, FTextureState, SceneRenderingAllocator> TextureStates;
	TSortedMap<FRDGBuffer*, FBufferState, SceneRenderingAllocator> BufferStates;

	/** Lists of pass parameters scheduled for begin during execution of this pass. */
	TArray<FRDGPass*, TInlineAllocator<1, SceneRenderingAllocator>> ResourcesToBegin;
	TArray<FRDGPass*, TInlineAllocator<1, SceneRenderingAllocator>> ResourcesToEnd;

	/** List of textures to acquire *after* the pass completes, *before* discards. Acquires apply to all allocated textures. */
	TArray<FRHITexture*, SceneRenderingAllocator> TexturesToAcquire;

	/** List of textures to discard *after* the pass completes, *after* acquires. Discards only apply to textures marked as
	 *  transient and the last alias of the texture uses the automatic discard behavior (in order to support cleaner hand-off
	 *  to the user or back to the pool.
	 */
	TArray<FRHITexture*, SceneRenderingAllocator> TexturesToDiscard;

	/** Barriers to begin / end prior to executing a pass. */
	FRDGBarrierBatchBegin* PrologueBarriersToBegin = nullptr;
	FRDGBarrierBatchEnd* PrologueBarriersToEnd = nullptr;

	/** Barriers to begin after executing a pass. */
	FRDGBarrierBatchBegin* EpilogueBarriersToBeginForGraphics = nullptr;
	FRDGBarrierBatchBegin* EpilogueBarriersToBeginForAsyncCompute = nullptr;

	EAsyncComputeBudget AsyncComputeBudget = EAsyncComputeBudget::EAll_4;

#if WITH_MGPU
	FRHIGPUMask GPUMask;
#endif

	IF_RDG_CPU_SCOPES(FRDGCPUScopes CPUScopes);
	IF_RDG_GPU_SCOPES(FRDGGPUScopes GPUScopes);

	friend FRDGBuilder;
	friend FRDGPassRegistry;
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
		const ParameterStructType* InParameterStruct,
		ERDGPassFlags InPassFlags,
		ExecuteLambdaType&& InExecuteLambda)
		: FRDGPass(MoveTemp(InName), FRDGParameterStruct(InParameterStruct), InPassFlags)
		, ExecuteLambda(MoveTemp(InExecuteLambda))
	{
		checkf(kSupportsAsyncCompute || !EnumHasAnyFlags(InPassFlags, ERDGPassFlags::AsyncCompute),
			TEXT("Pass %s is set to use 'AsyncCompute', but the pass lambda's first argument is not FRHIComputeCommandList&."), GetName());
	}

private:
	void ExecuteImpl(FRHIComputeCommandList& RHICmdList) override
	{
		check(!kSupportsRaster || RHICmdList.IsImmediate());
		ExecuteLambda(static_cast<TRHICommandList&>(RHICmdList));
	}

	ExecuteLambdaType ExecuteLambda;
};

template <typename ExecuteLambdaType>
class TRDGEmptyLambdaPass
	: public TRDGLambdaPass<FEmptyShaderParameters, ExecuteLambdaType>
{
public:
	TRDGEmptyLambdaPass(FRDGEventName&& InName, ERDGPassFlags InPassFlags, ExecuteLambdaType&& InExecuteLambda)
		: TRDGLambdaPass<FEmptyShaderParameters, ExecuteLambdaType>(MoveTemp(InName), &EmptyShaderParameters, InPassFlags, MoveTemp(InExecuteLambda))
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