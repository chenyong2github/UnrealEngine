// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"
#include "RenderGraphResources.h"
#include "RenderGraphEvent.h"
#include "ShaderParameterMacros.h"
#include "Containers/SortedMap.h"

class FRDGPass;
class FRDGBuilder;

/** A helper class for identifying and accessing a render graph pass parameter. */
class FRDGPassParameter final
{
public:
	FRDGPassParameter() = default;

	bool IsResource() const
	{
		return !IsRenderTargetBindingSlots();
	}

	bool IsSRV() const
	{
		return MemberType == UBMT_RDG_TEXTURE_SRV || MemberType == UBMT_RDG_BUFFER_SRV;
	}

	bool IsUAV() const
	{
		return MemberType == UBMT_RDG_TEXTURE_UAV || MemberType == UBMT_RDG_BUFFER_UAV;
	}

	bool IsChildResource() const
	{
		return IsSRV() || IsUAV();
	}

	bool IsTexture() const
	{
		return
			MemberType == UBMT_RDG_TEXTURE ||
			MemberType == UBMT_RDG_TEXTURE_COPY_DEST;
	}

	bool IsBuffer() const
	{
		return
			MemberType == UBMT_RDG_BUFFER ||
			MemberType == UBMT_RDG_BUFFER_COPY_DEST;
	}

	bool IsParentResource() const
	{
		return IsTexture() || IsBuffer();
	}

	bool IsRenderTargetBindingSlots() const
	{
		return MemberType == UBMT_RENDER_TARGET_BINDING_SLOTS;
	}

	EUniformBufferBaseType GetType() const
	{
		return MemberType;
	}

	FRDGResourceRef GetAsResource() const
	{
		check(IsResource());
		return *GetAs<FRDGResourceRef>();
	}

	FRDGParentResourceRef GetAsParentResource() const
	{
		check(IsParentResource());
		return *GetAs<FRDGParentResourceRef>();
	}

	FRDGChildResourceRef GetAsChildResource() const
	{
		check(IsChildResource());
		return *GetAs<FRDGChildResourceRef>();
	}

	FRDGShaderResourceViewRef GetAsSRV() const
	{
		check(IsSRV());
		return *GetAs<FRDGShaderResourceViewRef>();
	}

	FRDGUnorderedAccessViewRef GetAsUAV() const
	{
		check(IsUAV());
		return *GetAs<FRDGUnorderedAccessViewRef>();
	}

	FRDGTextureRef GetAsTexture() const
	{
		check(IsTexture());
		return *GetAs<FRDGTextureRef>();
	}

	FRDGBufferRef GetAsBuffer() const
	{
		check(IsBuffer());
		return *GetAs<FRDGBufferRef>();
	}

	FRDGTextureSRVRef GetAsTextureSRV() const
	{
		check(MemberType == UBMT_RDG_TEXTURE_SRV);
		return *GetAs<FRDGTextureSRVRef>();
	}

	FRDGBufferSRVRef GetAsBufferSRV() const
	{
		check(MemberType == UBMT_RDG_BUFFER_SRV);
		return *GetAs<FRDGBufferSRVRef>();
	}

	FRDGTextureUAVRef GetAsTextureUAV() const
	{
		check(MemberType == UBMT_RDG_TEXTURE_UAV);
		return *GetAs<FRDGTextureUAVRef>();
	}

	FRDGBufferUAVRef GetAsBufferUAV() const
	{
		check(MemberType == UBMT_RDG_BUFFER_UAV);
		return *GetAs<FRDGBufferUAVRef>();
	}

	const FRenderTargetBindingSlots& GetAsRenderTargetBindingSlots() const
	{
		check(IsRenderTargetBindingSlots());
		return *GetAs<FRenderTargetBindingSlots>();
	}

private:
	FRDGPassParameter(EUniformBufferBaseType InMemberType, void* InMemberPtr)
		: MemberType(InMemberType)
		, MemberPtr(InMemberPtr)
	{}

	template <typename T>
	T* GetAs() const
	{
		return reinterpret_cast<T*>(MemberPtr);
	}

	const EUniformBufferBaseType MemberType = UBMT_INVALID;
	void* const MemberPtr = nullptr;

	friend class FRDGPassParameterStruct;
};

/** Wraps a pass parameter struct payload and provides helpers for traversing members. */
class RENDERCORE_API FRDGPassParameterStruct final
{
public:
	explicit FRDGPassParameterStruct(void* InContents, const FRHIUniformBufferLayout* InLayout)
		: Contents(reinterpret_cast<uint8*>(InContents))
		, Layout(InLayout)
	{
		checkf(Contents && Layout, TEXT("Pass parameter struct created with null inputs."));
	}

	template <typename FParameterStruct>
	explicit FRDGPassParameterStruct(FParameterStruct* Parameters)
		: FRDGPassParameterStruct(Parameters, &FParameterStruct::FTypeInfo::GetStructMetadata()->GetLayout())
	{}

	uint8* GetContents() const
	{
		return Contents;
	}

	uint32 GetParameterCount() const
	{
		return Layout->GraphResources.Num();
	}

	FRDGPassParameter GetParameter(uint32 ParameterIndex) const
	{
		return GetParameterInternal(Layout->GraphResources, ParameterIndex);
	}

	uint32 GetTextureParameterCount() const
	{
		return Layout->GraphTextures.Num();
	}

	FRDGPassParameter GetTextureParameter(uint32 TextureParameterIndex) const
	{
		return GetParameterInternal(Layout->GraphTextures, TextureParameterIndex);
	}

	uint32 GetBufferParameterCount() const
	{
		return Layout->GraphBuffers.Num();
	}

	FRDGPassParameter GetBufferParameter(uint32 BufferParameterIndex) const
	{
		return GetParameterInternal(Layout->GraphBuffers, BufferParameterIndex);
	}

	/** Releases all active uniform buffer references held inside the struct. */
	void ClearUniformBuffers() const;

	FUniformBufferStaticBindings GetGlobalUniformBuffers() const;

	/** Returns whether the pass has outputs not represented by the graph resources. */
	bool HasExternalOutputs() const;

	/** Returns the render target binding slots, if they exist. Otherwise nullptr. */
	const FRenderTargetBindingSlots* GetRenderTargetBindingSlots() const;

private:
	FRDGPassParameter GetParameterInternal(TArrayView<const FRHIUniformBufferLayout::FResourceParameter> Parameters, uint32 ParameterIndex) const
	{
		checkf(ParameterIndex < static_cast<uint32>(Parameters.Num()), TEXT("Attempted to access RDG pass parameter outside of index for Layout '%s'"), *Layout->GetDebugName());
		const EUniformBufferBaseType MemberType = Parameters[ParameterIndex].MemberType;
		const uint16 MemberOffset = Parameters[ParameterIndex].MemberOffset;
		return FRDGPassParameter(MemberType, Contents + MemberOffset);
	}

	uint8* Contents;
	const FRHIUniformBufferLayout* Layout;
};

class RENDERCORE_API FRDGBarrierBatch
{
public:
	FRDGBarrierBatch(const FRDGBarrierBatch&) = delete;
	virtual ~FRDGBarrierBatch() = default;

	bool IsSubmitted() const
	{
		return bSubmitted;
	}

	FString GetName() const;

protected:
	FRDGBarrierBatch(const FRDGPass* InPass, const TCHAR* InName)
		: Pass(InPass)
		, Name(InName)
	{
		check(Pass && Name);
	}

	void SetSubmitted();

	const FRDGPass* Pass;
	const TCHAR* Name;
	bool bSubmitted = false;
};

class RENDERCORE_API FRDGBarrierBatchBegin final : public FRDGBarrierBatch
{
public:
	FRDGBarrierBatchBegin(const FRDGPass* InPass, const TCHAR* InName, TOptional<ERDGPipeline> InOverridePipelineForEnd = {});
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

	/** The transition to store after submission. It is assigned back to null by the end batch. */
	const FRHITransition* Transition = nullptr;

	/** An array of asynchronous resource transitions to perform. */
	TArray<FRHITransitionInfo, TInlineAllocator<4, SceneRenderingAllocator>> Transitions;

	/** An array of RDG resources matching with the Transitions array. Used for patching the RHI resource pointers prior to submission. */
	TArray<FRDGParentResource*, TInlineAllocator<4, SceneRenderingAllocator>> Resources;

	TOptional<ERDGPipeline> OverridePipelineToEnd;
	bool bUseCrossPipelineFence = false;

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
	FRDGPass(
		FRDGEventName&& InName,
		FRDGPassParameterStruct InParameterStruct,
		ERDGPassFlags InFlags);

	FRDGPass(const FRDGPass&) = delete;

	virtual ~FRDGPass() = default;

	const TCHAR* GetName() const
	{
		return Name.GetTCHAR();
	}

	ERDGPassFlags GetFlags() const
	{
		return Flags;
	}

	ERDGPipeline GetPipeline() const
	{
		return Pipeline;
	}

	FRDGPassParameterStruct GetParameters() const
	{
		return ParameterStruct;
	}

	FRDGPassHandle GetHandle() const
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
		return Pipeline == ERDGPipeline::AsyncCompute;
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

	const FRDGPassHandleArray& GetConsumers() const
	{
		return Consumers;
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

	FRDGScopes GetScopes() const
	{
		return Scopes;
	}

private:
	FRDGBarrierBatchBegin* GetEpilogueBarriersToBeginFor(ERDGPipeline PipelineForEnd)
	{
		check(PipelineForEnd != ERDGPipeline::MAX);
		return PipelineForEnd == ERDGPipeline::Graphics
			? &EpilogueBarriersToBeginForGraphics
			: &EpilogueBarriersToBeginForAsyncCompute;
	}

	//////////////////////////////////////////////////////////////////////////
	//! User Methods to Override

	virtual void ExecuteImpl(FRHIComputeCommandList& RHICmdList) const = 0;

	//////////////////////////////////////////////////////////////////////////

	void Execute(FRHIComputeCommandList& RHICmdList) const;

	const FRDGEventName Name;
	const FRDGPassParameterStruct ParameterStruct;
	const ERDGPassFlags Flags;
	const ERDGPipeline Pipeline;

	/** Handle of the pass and its dependencies in the graph builder pass list. */
	FRDGPassHandle Handle;

	/** Handle of the latest cross-pipeline producer and earliest cross-pipeline consumer. */
	FRDGPassHandle CrossPipelineProducer;
	FRDGPassHandle CrossPipelineConsumer;

	/** (AsyncCompute only) Graphics passes which are the fork / join for async compute interval this pass is in. */
	FRDGPassHandle GraphicsForkPass;
	FRDGPassHandle GraphicsJoinPass;

	/** Lists of producer / consumer passes. */
	FRDGPassHandleArray Producers;
	FRDGPassHandleArray Consumers;

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

	/** Whether subresource accesses are the same across textures. */
	uint32 bSubresourceTrackingRequired : 1;

	struct FAccessInfo
	{
		/** The combined set of access across all resource references. */
		EResourceTransitionAccess AccessUnion = EResourceTransitionAccess::Unknown;

		/** The number of resource references across the pass. */
		uint16 ReferenceCount = 0;

		/** The last no-barrier UAV assigned. */
		FRDGResourceUniqueFilter NoUAVBarrierFilter;

		/** Whether the resource requires subresource tracking. */
		bool bSubresourceTrackingRequired = false;

		/** The combined set of transition flags required by all resource references. */
		EResourceTransitionFlags FlagsUnion = EResourceTransitionFlags::None;
	};

	/** Maps textures / buffers to information on how they are used in the pass. */
	TSortedMap<FRDGTexture*, FAccessInfo, SceneRenderingAllocator> TextureAccessMap;
	TSortedMap<FRDGBuffer*, FAccessInfo, SceneRenderingAllocator> BufferAccessMap;

	/** Lists of pass parameters scheduled for begin during execution of this pass. */
	TArray<FRDGPassParameterStruct, TInlineAllocator<1, SceneRenderingAllocator>> PassesToBegin;

	/** List of resources this pass will release after executing the pass. Used to extend async compute resource lifetimes. */
	TArray<FRDGTexture*, SceneRenderingAllocator> TexturesToRelease;
	TArray<FRDGBuffer*, SceneRenderingAllocator> BuffersToRelease;

	/** Barriers to begin / end prior to executing a pass. */
	FRDGBarrierBatchBegin PrologueBarriersToBegin;
	FRDGBarrierBatchEnd PrologueBarriersToEnd;

	/** Barriers to begin after executing a pass. */
	FRDGBarrierBatchBegin EpilogueBarriersToBeginForGraphics;
	FRDGBarrierBatchBegin EpilogueBarriersToBeginForAsyncCompute;

	FRDGScopes Scopes;

	friend FRDGBuilder;
};

/** Render graph pass with lambda execute function. */
template <typename ParameterStructType, typename ExecuteLambdaType>
class TRDGLambdaPass final
	: public FRDGPass
{
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
		FRDGPassParameterStruct InParameterStruct,
		ERDGPassFlags InPassFlags,
		ExecuteLambdaType&& InExecuteLambda)
		: FRDGPass(MoveTemp(InName), InParameterStruct, InPassFlags)
		, ExecuteLambda(MoveTemp(InExecuteLambda))
	{
		if (!kSupportsAsyncCompute)
		{
			checkf(InPassFlags != ERDGPassFlags::AsyncCompute, TEXT("Pass %s is set to use 'AsyncCompute', but the pass lambda's first argument is not FRHIComputeCommandList&."), GetName());
		}
	}

private:
	void ExecuteImpl(FRHIComputeCommandList& RHICmdList) const override
	{
		if (kSupportsRaster)
		{
			check(RHICmdList.IsImmediate());
		}

		ExecuteLambda(static_cast<TRHICommandList&>(RHICmdList));
	}

	ExecuteLambdaType ExecuteLambda;
};

class FRDGSentinelPass final
	: public FRDGPass
{
public:
	FRDGSentinelPass(FRDGEventName&& Name)
		: FRDGPass(MoveTemp(Name), FRDGPassParameterStruct(&EmptyShaderParameters), ERDGPassFlags::Copy)
	{}

private:
	void ExecuteImpl(FRHIComputeCommandList&) const override {}
	FEmptyShaderParameters EmptyShaderParameters;
};