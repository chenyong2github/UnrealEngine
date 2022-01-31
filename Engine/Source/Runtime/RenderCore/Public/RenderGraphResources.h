// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphParameter.h"
#include "RenderGraphTextureSubresource.h"
#include "RendererInterface.h"
#include "RHITransientResourceAllocator.h"
#include "RHIResources.h"

struct FPooledRenderTarget;
class FRenderTargetPool;

/** Used for tracking pass producer / consumer edges in the graph for culling and pipe fencing. */
struct FRDGProducerState
{
	/** Returns whether the next state is dependent on the last producer in the producer graph. */
	static bool IsDependencyRequired(FRDGProducerState LastProducer, ERHIPipeline LastPipeline, FRDGProducerState NextState, ERHIPipeline NextPipeline);

	FRDGProducerState() = default;

	ERHIAccess Access = ERHIAccess::Unknown;
	FRDGPassHandle PassHandle;
	FRDGViewHandle NoUAVBarrierHandle;
};

using FRDGProducerStatesByPipeline = TRHIPipelineArray<FRDGProducerState>;

/** Used for tracking the state of an individual subresource during execution. */
struct FRDGSubresourceState
{
	/** Given a before and after state, returns whether a resource barrier is required. */
	static bool IsTransitionRequired(const FRDGSubresourceState& Previous, const FRDGSubresourceState& Next);

	/** Given a before and after state, returns whether they can be merged into a single state. */
	static bool IsMergeAllowed(ERDGParentResourceType ResourceType, const FRDGSubresourceState& Previous, const FRDGSubresourceState& Next);

	FRDGSubresourceState() = default;

	explicit FRDGSubresourceState(ERHIAccess InAccess)
		: Access(InAccess)
	{}

	/** Initializes the first and last pass and the pipeline. Clears any other pass state. */
	void SetPass(ERHIPipeline Pipeline, FRDGPassHandle PassHandle);

	/** Finalizes the state at the end of the transition chain; keeps access intact. */
	void Finalize();

	/** Validates that the state is in a correct configuration for use. */
	void Validate();

	/** Returns whether the state is used by the pipeline. */
	bool IsUsedBy(ERHIPipeline Pipeline) const;

	/** Returns the last pass across either pipe. */
	FRDGPassHandle GetLastPass() const;

	/** Returns the first pass across either pipe. */
	FRDGPassHandle GetFirstPass() const;

	/** Returns the pipeline mask this state is used on. */
	ERHIPipeline GetPipelines() const;

	/** The last used access on the pass. */
	ERHIAccess Access = ERHIAccess::Unknown;

	/** The last used transition flags on the pass. */
	EResourceTransitionFlags Flags = EResourceTransitionFlags::None;

	/** The first pass in this state. */
	FRDGPassHandlesByPipeline FirstPass;

	/** The last pass in this state. */
	FRDGPassHandlesByPipeline LastPass;

	/** The last no-UAV barrier to be used by this subresource. */
	FRDGViewUniqueFilter NoUAVBarrierFilter;

	/** The last used pass for log file debugging. */
	IF_RDG_ENABLE_DEBUG(mutable FRDGPassHandle LogFilePass);
};

using FRDGTextureSubresourceState = TRDGTextureSubresourceArray<FRDGSubresourceState, FDefaultAllocator>;
using FRDGTextureTransientSubresourceState = TRDGTextureSubresourceArray<FRDGSubresourceState, FRDGArrayAllocator>;
using FRDGTextureTransientSubresourceStateIndirect = TRDGTextureSubresourceArray<FRDGSubresourceState*, FRDGArrayAllocator>;

using FRDGPooledTextureArray = TArray<TRefCountPtr<IPooledRenderTarget>, FRDGArrayAllocator>;
using FRDGPooledBufferArray = TArray<TRefCountPtr<FRDGPooledBuffer>, FRDGArrayAllocator>;

/** Generic graph resource. */
class RENDERCORE_API FRDGResource
{
public:
	FRDGResource(const FRDGResource&) = delete;
	virtual ~FRDGResource() = default;

	// Name of the resource for debugging purpose.
	const TCHAR* const Name = nullptr;

	//////////////////////////////////////////////////////////////////////////
	//! The following methods may only be called during pass execution.

	/** Marks this resource as actually used by a resource. This is to track what dependencies on pass was actually unnecessary. */
#if RDG_ENABLE_DEBUG
	virtual void MarkResourceAsUsed();
#else
	inline  void MarkResourceAsUsed() {}
#endif

	FRHIResource* GetRHI() const
	{
		IF_RDG_ENABLE_DEBUG(ValidateRHIAccess());
		return ResourceRHI;
	}

	//////////////////////////////////////////////////////////////////////////

protected:
	FRDGResource(const TCHAR* InName)
		: Name(InName)
	{}

	FRHIResource* GetRHIUnchecked() const
	{
		return ResourceRHI;
	}

	bool HasRHI() const
	{
		return ResourceRHI != nullptr;
	}

	FRHIResource* ResourceRHI = nullptr;

#if RDG_ENABLE_DEBUG
	void ValidateRHIAccess() const;
#endif

private:
#if RDG_ENABLE_DEBUG
	struct FRDGResourceDebugData* DebugData = nullptr;
	FRDGResourceDebugData& GetDebugData() const;
#endif

	friend FRDGBuilder;
	friend FRDGUserValidation;
	friend FRDGBarrierValidation;
};

class RENDERCORE_API FRDGUniformBuffer
	: public FRDGResource
{
public:

	virtual ~FRDGUniformBuffer() {};

	FORCEINLINE const FRDGParameterStruct& GetParameters() const
	{
		return ParameterStruct;
	}

#if RDG_ENABLE_DEBUG
	void MarkResourceAsUsed() override;
#else
	inline void MarkResourceAsUsed() {}
#endif

	//////////////////////////////////////////////////////////////////////////
	//! The following methods may only be called during pass execution.

	FRHIUniformBuffer* GetRHI() const
	{
		return static_cast<FRHIUniformBuffer*>(FRDGResource::GetRHI());
	}

	//////////////////////////////////////////////////////////////////////////

protected:
	template <typename TParameterStruct>
	explicit FRDGUniformBuffer(const TParameterStruct* InParameters, const TCHAR* InName)
		: FRDGResource(InName)
		, ParameterStruct(InParameters, TParameterStruct::FTypeInfo::GetStructMetadata())
	{}

private:
	FRHIUniformBuffer* GetRHIUnchecked() const
	{
		return static_cast<FRHIUniformBuffer*>(FRDGResource::GetRHIUnchecked());
	}

	void InitRHI();

	const FRDGParameterStruct ParameterStruct;
	TRefCountPtr<FRHIUniformBuffer> UniformBufferRHI;
	FRDGUniformBufferHandle Handle;
	bool bQueuedForCreate = false;

	friend FRDGBuilder;
	friend FRDGUniformBufferRegistry;
	friend FRDGAllocator;
};

template <typename ParameterStructType>
class TRDGUniformBuffer : public FRDGUniformBuffer
{
public:
	virtual ~TRDGUniformBuffer() {};

	FORCEINLINE const TRDGParameterStruct<ParameterStructType>& GetParameters() const
	{
		return static_cast<const TRDGParameterStruct<ParameterStructType>&>(FRDGUniformBuffer::GetParameters());
	}

	FORCEINLINE const ParameterStructType* GetContents() const
	{
		return Parameters;
	}

	FORCEINLINE TUniformBufferRef<ParameterStructType> GetRHIRef() const
	{
		return TUniformBufferRef<ParameterStructType>(GetRHI());
	}

	FORCEINLINE const ParameterStructType* operator->() const
	{
		return Parameters;
	}

private:
	explicit TRDGUniformBuffer(const ParameterStructType* InParameters, const TCHAR* InName)
		: FRDGUniformBuffer(InParameters, InName)
		, Parameters(InParameters)
	{}

	const ParameterStructType* Parameters;

	friend FRDGBuilder;
	friend FRDGUniformBufferRegistry;
	friend FRDGAllocator;
};

/** A render graph resource with an allocation lifetime tracked by the graph. May have child resources which reference it (e.g. views). */
class RENDERCORE_API FRDGParentResource
	: public FRDGResource
{
public:
	/** The type of this resource; useful for casting between types. */
	const ERDGParentResourceType Type;

	/** Whether this resource is externally registered with the graph (i.e. the user holds a reference to the underlying resource outside the graph). */
	bool IsExternal() const
	{
		return bExternal;
	}

	/** Whether this resource is has been queued for extraction at the end of graph execution. */
	bool IsExtracted() const
	{
		return bExtracted;
	}

	bool IsCulled() const
	{
		return bCulled;
	}

	/** Whether a prior pass added to the graph produced contents for this resource. External resources are not considered produced
	 *  until used for a write operation. This is a union of all subresources, so any subresource write will set this to true.
	 */
	bool HasBeenProduced() const
	{
		return bProduced;
	}

protected:
	FRDGParentResource(const TCHAR* InName, ERDGParentResourceType InType);

	enum class ETransientExtractionHint
	{
		None,
		Disable,
		Enable
	};

	/** Whether this is an externally registered resource. */
	uint8 bExternal : 1;

	/** Whether this is an extracted resource. */
	uint8 bExtracted : 1;

	/** Whether any sub-resource has been used for write by a pass. */
	uint8 bProduced : 1;

	/** Whether this resource is allocated through the transient resource allocator. */
	uint8 bTransient : 1;

	/** Whether this resource cannot be made transient. */
	uint8 bForceNonTransient : 1;

	/** Whether this resource is allowed to be both transient and extracted. */
	ETransientExtractionHint TransientExtractionHint : 2;

	/** (External | Extracted only) If true, the resource is locked in its current state and will not be transitioned any more. */
	uint8 bFinalizedAccess : 1;

	/** Whether this resource is the last owner of its allocation (i.e. nothing aliases the allocation later in the execution timeline). */
	uint8 bLastOwner : 1;

	/** If true, the resource was not used by any pass not culled by the graph. */
	uint8 bCulled : 1;

	/** If true, the resource has been used on an async compute pass and may have async compute states. */
	uint8 bUsedByAsyncComputePass : 1;

	/** If true, the resource has been queued for an upload operation. */
	uint8 bQueuedForUpload : 1;

	/** If true, this resource is a swap chain texture. */
	uint8 bSwapChain : 1;

	/** If true, the swap chain transition has been moved to occur at the latest possible time. */
	uint8 bSwapChainAlreadyMoved : 1;

	/** If true, the resource is access through at least one UAV. */
	uint8 bUAVAccessed : 1;

	FRDGPassHandle FirstPass;
	FRDGPassHandle LastPass;

private:
	/** Number of references in passes and deferred queries. */
	uint16 ReferenceCount = 0;

	/** Scratch index allocated for the resource in the pass being setup. */
	uint16 PassStateIndex = 0;

	/** The final state of the resource at the end of graph execution, if known. */
	ERHIAccess AccessFinal = ERHIAccess::Unknown;

#if RDG_ENABLE_TRACE
	uint16 TraceOrder = 0;
	TArray<FRDGPassHandle, FRDGArrayAllocator> TracePasses;
#endif

#if RDG_ENABLE_DEBUG
	struct FRDGParentResourceDebugData* ParentDebugData = nullptr;
	FRDGParentResourceDebugData& GetParentDebugData() const;
#endif

	friend FRDGBuilder;
	friend FRDGUserValidation;
	friend FRDGBarrierBatchBegin;
	friend FRDGTrace;
};

/** A render graph resource (e.g. a view) which references a single parent resource (e.g. a texture / buffer). Provides an abstract way to access the parent resource. */
class FRDGView
	: public FRDGResource
{
public:
	/** The type of this child resource; useful for casting between types. */
	const ERDGViewType Type;

	/** Returns the referenced parent render graph resource. */
	virtual FRDGParentResourceRef GetParent() const = 0;

	FRDGViewHandle GetHandle() const
	{
		return Handle;
	}

protected:
	FRDGView(const TCHAR* Name, ERDGViewType InType)
		: FRDGResource(Name)
		, Type(InType)
	{}

private:
	FRDGViewHandle Handle;
	FRDGPassHandle LastPass;

	friend FRDGBuilder;
	friend FRDGViewRegistry;
	friend FRDGAllocator;
};

/** Translates from a pooled render target descriptor to an RDG texture descriptor. */
inline FRDGTextureDesc Translate(const FPooledRenderTargetDesc& InDesc);

/** Translates from an RDG texture descriptor to a pooled render target descriptor. */
inline FPooledRenderTargetDesc Translate(const FRDGTextureDesc& InDesc);

UE_DEPRECATED(5.0, "Translate with ERenderTargetTexture is deprecated. Please use the single parameter variant.")
PRAGMA_DISABLE_DEPRECATION_WARNINGS
inline FRDGTextureDesc Translate(const FPooledRenderTargetDesc& InDesc, ERenderTargetTexture InTexture)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
	return Translate(InDesc);
}

class RENDERCORE_API FRDGPooledTexture final
	: public FRefCountedObject
{
public:
	FRDGPooledTexture(FRHITexture* InTexture, const FRDGTextureSubresourceLayout& InLayout, ERHIAccess AccessInitial = ERHIAccess::Unknown)
		: Texture(InTexture)
	{
		InitAsWholeResource(State, FRDGSubresourceState(AccessInitial));
	}

	/** Finds a UAV matching the descriptor in the cache or creates a new one and updates the cache. */
	FORCEINLINE FRHIUnorderedAccessView* GetOrCreateUAV(const FRHITextureUAVCreateInfo& UAVDesc) { return ViewCache.GetOrCreateUAV(Texture, UAVDesc); }

	/** Finds a SRV matching the descriptor in the cache or creates a new one and updates the cache. */
	FORCEINLINE FRHIShaderResourceView* GetOrCreateSRV(const FRHITextureSRVCreateInfo& SRVDesc) { return ViewCache.GetOrCreateSRV(Texture, SRVDesc); }

	FORCEINLINE FRHITexture* GetRHI() const { return Texture; }

	FORCEINLINE FRDGTexture* GetOwner() const { return Owner; }

private:
	/** Prepares the pooled texture state for re-use across RDG builder instances. */
	void Finalize();

	/** Resets the pooled texture state back an unknown value. */
	void Reset();

	TRefCountPtr<FRHITexture> Texture;
	FRDGTexture* Owner = nullptr;
	FRDGTextureSubresourceState State;
	FRHITextureViewCache ViewCache;

	friend FRDGTexture;
	friend FRDGBuilder;
	friend FRenderTargetPool;
	friend FRDGAllocator;
};

/** Render graph tracked Texture. */
class RENDERCORE_API FRDGTexture final
	: public FRDGParentResource
{
public:
	const FRDGTextureDesc Desc;
	const ERDGTextureFlags Flags;

	//////////////////////////////////////////////////////////////////////////
	//! The following methods may only be called during pass execution.

	/** Returns the allocated pooled render target. */
	UE_DEPRECATED(5.0, "Accessing the underlying pooled render target has been deprecated. Use GetRHI() instead.")
	IPooledRenderTarget* GetPooledRenderTarget() const;

	/** Returns the allocated RHI texture. */
	FORCEINLINE FRHITexture* GetRHI() const
	{
		return static_cast<FRHITexture*>(FRDGResource::GetRHI());
	}

	//////////////////////////////////////////////////////////////////////////

	FORCEINLINE FRDGTextureHandle GetHandle() const
	{
		return Handle;
	}

	FORCEINLINE FRDGTextureSubresourceLayout GetSubresourceLayout() const
	{
		return Layout;
	}

	FORCEINLINE FRDGTextureSubresourceRange GetSubresourceRange() const
	{
		return WholeRange;
	}

	FORCEINLINE uint32 GetSubresourceCount() const
	{
		return SubresourceCount;
	}

	FORCEINLINE FRDGTextureSubresource GetSubresource(uint32 SubresourceIndex) const
	{
		return Layout.GetSubresource(SubresourceIndex);
	}

	FRDGTextureSubresourceRange GetSubresourceRangeSRV() const;

private:
	FRDGTexture(const TCHAR* InName, const FRDGTextureDesc& InDesc, ERDGTextureFlags InFlags)
		: FRDGParentResource(InName, ERDGParentResourceType::Texture)
		, Desc(InDesc)
		, Flags(InFlags)
		, Layout(InDesc)
		, WholeRange(Layout)
		, SubresourceCount(Layout.GetSubresourceCount())
	{
		MergeState.Reserve(SubresourceCount);
		MergeState.SetNum(SubresourceCount);
		LastProducers.Reserve(SubresourceCount);
		LastProducers.SetNum(SubresourceCount);
		bSwapChain = EnumHasAnyFlags(Desc.Flags, ETextureCreateFlags::Presentable);
	}

	/** Assigns a render target as the backing RHI resource. */
	void SetRHI(IPooledRenderTarget* PooledRenderTarget);

	/** Assigns a pooled texture as the backing RHI resource. */
	void SetRHI(FRDGPooledTexture* PooledTexture);

	/** Assigns a transient texture as the backing RHI resource. */
	void SetRHI(FRHITransientTexture* TransientTexture, FRDGTextureSubresourceState* TransientTextureState);

	/** Finalizes the texture for execution; no other transitions are allowed after calling this. */
	void Finalize(FRDGPooledTextureArray& PooledTextureArray);

	/** Returns RHI texture without access checks. */
	FRHITexture* GetRHIUnchecked() const
	{
		return static_cast<FRHITexture*>(FRDGResource::GetRHIUnchecked());
	}

	/** Returns the current texture state. Only valid to call after SetRHI. */
	FRDGTextureSubresourceState& GetState()
	{
		check(State);
		return *State;
	}

	/** The next texture to own the PooledTexture allocation during execution. */
	FRDGTextureHandle NextOwner;

	/** The handle registered with the builder. */
	FRDGTextureHandle Handle;

	/** The layout used to facilitate subresource transitions. */
	FRDGTextureSubresourceLayout Layout;
	FRDGTextureSubresourceRange  WholeRange;
	const uint32 SubresourceCount;

	/** The assigned pooled render target to use during execution. Never reset. */
	IPooledRenderTarget* PooledRenderTarget = nullptr;

	union
	{
		/** The assigned pooled texture to use during execution. Never reset. */
		FRDGPooledTexture* PooledTexture = nullptr;

		/** The assigned transient texture to use during execution. Never reset. */
		FRHITransientTexture* TransientTexture;
	};

	/** The assigned view cache for this texture (sourced from transient / pooled texture). Never reset. */
	FRHITextureViewCache* ViewCache = nullptr;

	/** Valid strictly when holding a strong reference; use PooledRenderTarget instead. */
	TRefCountPtr<IPooledRenderTarget> Allocation;

	/** Cached state pointer from the pooled texture. */
	FRDGTextureSubresourceState* State = nullptr;

	/** Tracks merged subresource states as the graph is built. */
	FRDGTextureTransientSubresourceStateIndirect MergeState;

	/** Tracks pass producers for each subresource as the graph is built. */
	TRDGTextureSubresourceArray<FRDGProducerStatesByPipeline, FRDGArrayAllocator> LastProducers;

#if RDG_ENABLE_DEBUG
	struct FRDGTextureDebugData* TextureDebugData = nullptr;
	FRDGTextureDebugData& GetTextureDebugData() const;
#endif

	friend FRDGBuilder;
	friend FRDGUserValidation;
	friend FRDGBarrierValidation;
	friend FRDGTextureRegistry;
	friend FRDGAllocator;
	friend FPooledRenderTarget;
	friend FRDGTrace;
	friend FRDGTextureUAV;
};

/** Render graph tracked SRV. */
class FRDGShaderResourceView
	: public FRDGView
{
public:
	/** Returns the allocated RHI SRV. */
	FRHIShaderResourceView* GetRHI() const
	{
		return static_cast<FRHIShaderResourceView*>(FRDGResource::GetRHI());
	}

protected:
	FRDGShaderResourceView(const TCHAR* InName, ERDGViewType InType)
		: FRDGView(InName, InType)
	{}

	/** Returns the allocated RHI SRV without access checks. */
	FRHIShaderResourceView* GetRHIUnchecked() const
	{
		return static_cast<FRHIShaderResourceView*>(FRDGResource::GetRHIUnchecked());
	}
};

/** Render graph tracked UAV. */
class FRDGUnorderedAccessView
	: public FRDGView
{
public:
	const ERDGUnorderedAccessViewFlags Flags;

	/** Returns the allocated RHI UAV. */
	FRHIUnorderedAccessView* GetRHI() const
	{
		return static_cast<FRHIUnorderedAccessView*>(FRDGResource::GetRHI());
	}

protected:
	FRDGUnorderedAccessView(const TCHAR* InName, ERDGViewType InType, ERDGUnorderedAccessViewFlags InFlags)
		: FRDGView(InName, InType)
		, Flags(InFlags)
	{}

	/** Returns the allocated RHI UAV without access checks. */
	FRHIUnorderedAccessView* GetRHIUnchecked() const
	{
		return static_cast<FRHIUnorderedAccessView*>(FRDGResource::GetRHIUnchecked());
	}
};

/** Descriptor for render graph tracked SRV. */
class FRDGTextureSRVDesc final
	: public FRHITextureSRVCreateInfo
{
public:
	FRDGTextureSRVDesc() = default;
	
	FRDGTextureRef Texture = nullptr;

	/** Create SRV that access all sub resources of texture. */
	static FRDGTextureSRVDesc Create(FRDGTextureRef Texture)
	{
		FRDGTextureSRVDesc Desc;
		Desc.Texture = Texture;
		Desc.NumMipLevels = Texture->Desc.NumMips;
		return Desc;
	}

	/** Create SRV that access one specific mip level. */
	static FRDGTextureSRVDesc CreateForMipLevel(FRDGTextureRef Texture, int32 MipLevel)
	{
		FRDGTextureSRVDesc Desc;
		Desc.Texture = Texture;
		check(MipLevel >= -1 && MipLevel <= TNumericLimits<int8>::Max()); 
		Desc.MipLevel = (int8)MipLevel;
		Desc.NumMipLevels = 1;
		return Desc;
	}

	/** Create SRV that access one specific mip level. */
	static FRDGTextureSRVDesc CreateWithPixelFormat(FRDGTextureRef Texture, EPixelFormat PixelFormat)
	{
		FRDGTextureSRVDesc Desc = FRDGTextureSRVDesc::Create(Texture);
		Desc.Format = PixelFormat;
		return Desc;
	}

	/** Create SRV with access to a specific meta data plane */
	static FRDGTextureSRVDesc CreateForMetaData(FRDGTextureRef Texture, ERDGTextureMetaDataAccess MetaData)
	{
		FRDGTextureSRVDesc Desc = FRDGTextureSRVDesc::Create(Texture);
		Desc.MetaData = MetaData;
		return Desc;
	}
};

/** Render graph tracked SRV. */
class FRDGTextureSRV final
	: public FRDGShaderResourceView
{
public:
	/** Descriptor of the graph tracked SRV. */
	const FRDGTextureSRVDesc Desc;

	FRDGTextureRef GetParent() const override
	{
		return Desc.Texture;
	}

	FRDGTextureSubresourceRange GetSubresourceRange() const;

private:
	FRDGTextureSRV(const TCHAR* InName, const FRDGTextureSRVDesc& InDesc)
		: FRDGShaderResourceView(InName, ERDGViewType::TextureSRV)
		, Desc(InDesc)
	{}

	friend FRDGBuilder;
	friend FRDGViewRegistry;
	friend FRDGAllocator;
};

/** Descriptor for render graph tracked UAV. */
class FRDGTextureUAVDesc final
	: public FRHITextureUAVCreateInfo
{
public:
	FRDGTextureUAVDesc() = default;

	FRDGTextureUAVDesc(FRDGTextureRef InTexture, uint8 InMipLevel = 0, EPixelFormat InFormat = PF_Unknown, uint16 InFirstArraySlice = 0, uint16 InNumArraySlices = 0)
		: FRHITextureUAVCreateInfo(InMipLevel, InFormat != PF_Unknown ? InFormat : InTexture->Desc.UAVFormat, InFirstArraySlice, InNumArraySlices)
		, Texture(InTexture)
	{}

	/** Create UAV with access to a specific meta data plane */
	static FRDGTextureUAVDesc CreateForMetaData(FRDGTextureRef Texture, ERDGTextureMetaDataAccess MetaData)
	{
		FRDGTextureUAVDesc Desc = FRDGTextureUAVDesc(Texture, 0);
		Desc.MetaData = MetaData;
		return Desc;
	}

	FRDGTextureRef Texture = nullptr;
};

/** Render graph tracked texture UAV. */
class FRDGTextureUAV final
	: public FRDGUnorderedAccessView
{
public:
	/** Descriptor of the graph tracked UAV. */
	const FRDGTextureUAVDesc Desc;

	FRDGTextureRef GetParent() const override
	{
		return Desc.Texture;
	}

	// Can be used instead of GetParent()->GetRHI() to access the underlying texture for a UAV resource in a Pass, without triggering
	// validation errors.  The RDG validation logic only flags the UAV as accessible, not the parent texture.
	FRHITexture* GetParentRHI() const
	{
		IF_RDG_ENABLE_DEBUG(ValidateRHIAccess());
		return Desc.Texture->GetRHIUnchecked();
	}

	FRDGTextureSubresourceRange GetSubresourceRange() const;

private:
	FRDGTextureUAV(const TCHAR* InName, const FRDGTextureUAVDesc& InDesc, ERDGUnorderedAccessViewFlags InFlags)
		: FRDGUnorderedAccessView(InName, ERDGViewType::TextureUAV, InFlags)
		, Desc(InDesc)
	{}

	friend FRDGBuilder;
	friend FRDGViewRegistry;
	friend FRDGAllocator;
};

/** Descriptor for render graph tracked Buffer. */
struct FRDGBufferDesc
{
	enum class EUnderlyingType
	{
		VertexBuffer,
		StructuredBuffer,
		AccelerationStructure
	};

	/** Create the descriptor for an indirect RHI call.
	 *
	 * Note, IndirectParameterStruct should be one of the:
	 *		struct FRHIDispatchIndirectParameters
	 *		struct FRHIDrawIndirectParameters
	 *		struct FRHIDrawIndexedIndirectParameters
	 */
	template<typename IndirectParameterStruct>
	static FRDGBufferDesc CreateIndirectDesc(uint32 NumElements = 1)
	{
		FRDGBufferDesc Desc;
		Desc.UnderlyingType = EUnderlyingType::VertexBuffer;
		Desc.Usage = (EBufferUsageFlags)(BUF_Static | BUF_DrawIndirect | BUF_UnorderedAccess | BUF_ShaderResource);
		Desc.BytesPerElement = sizeof(IndirectParameterStruct);
		Desc.NumElements = NumElements;
		return Desc;
	}

	static FRDGBufferDesc CreateIndirectDesc(uint32 NumElements = 1)
	{
		FRDGBufferDesc Desc;
		Desc.UnderlyingType = EUnderlyingType::VertexBuffer;
		Desc.Usage = (EBufferUsageFlags)(BUF_Static | BUF_DrawIndirect | BUF_UnorderedAccess | BUF_ShaderResource);
		Desc.BytesPerElement = 4;
		Desc.NumElements = NumElements;
		return Desc;
	}

	static FRDGBufferDesc CreateStructuredDesc(uint32 BytesPerElement, uint32 NumElements)
	{
		FRDGBufferDesc Desc;
		Desc.UnderlyingType = EUnderlyingType::StructuredBuffer;
		Desc.Usage = (EBufferUsageFlags)(BUF_Static | BUF_UnorderedAccess | BUF_ShaderResource);
		Desc.BytesPerElement = BytesPerElement;
		Desc.NumElements = NumElements;
		return Desc;
	}

	template<typename ParameterStruct>
	static FRDGBufferDesc CreateStructuredDesc(uint32 NumElements)
	{
		FRDGBufferDesc Desc = CreateStructuredDesc(sizeof(ParameterStruct), NumElements);
		Desc.Metadata = ParameterStruct::FTypeInfo::GetStructMetadata();
		return Desc;
	}

	static FRDGBufferDesc CreateBufferDesc(uint32 BytesPerElement, uint32 NumElements)
	{
		FRDGBufferDesc Desc;
		Desc.UnderlyingType = EUnderlyingType::VertexBuffer;
		Desc.Usage = (EBufferUsageFlags)(BUF_Static | BUF_UnorderedAccess | BUF_ShaderResource);
		Desc.BytesPerElement = BytesPerElement;
		Desc.NumElements = NumElements;
		return Desc;
	}

	template<typename ParameterStruct>
	static FRDGBufferDesc CreateBufferDesc(uint32 NumElements)
	{
		FRDGBufferDesc Desc = CreateBufferDesc(sizeof(ParameterStruct), NumElements);
		Desc.Metadata = ParameterStruct::FTypeInfo::GetStructMetadata();
		return Desc;
	}

	static FRDGBufferDesc CreateByteAddressDesc(uint32 NumBytes)
	{
		check(NumBytes % 4 == 0);
		FRDGBufferDesc Desc;
		Desc.UnderlyingType = EUnderlyingType::StructuredBuffer;
		Desc.Usage = (EBufferUsageFlags)(BUF_UnorderedAccess | BUF_ShaderResource | BUF_ByteAddressBuffer);
		Desc.BytesPerElement = 4;
		Desc.NumElements = NumBytes / 4;
		return Desc;
	}

	template<typename ParameterStruct>
	static FRDGBufferDesc CreateByteAddressDesc(uint32 NumElements)
	{
		FRDGBufferDesc Desc = CreateByteAddressDesc(sizeof(ParameterStruct) * NumElements);
		Desc.Metadata = ParameterStruct::FTypeInfo::GetStructMetadata();
		return Desc;
	}

	static FRDGBufferDesc CreateUploadDesc(uint32 BytesPerElement, uint32 NumElements)
	{
		FRDGBufferDesc Desc;
		Desc.UnderlyingType = EUnderlyingType::VertexBuffer;
		Desc.Usage = (EBufferUsageFlags)(BUF_Static | BUF_ShaderResource);
		Desc.BytesPerElement = BytesPerElement;
		Desc.NumElements = NumElements;
		return Desc;
	}

	template<typename ParameterStruct>
	static FRDGBufferDesc CreateUploadDesc(uint32 NumElements)
	{
		FRDGBufferDesc Desc = CreateUploadDesc(sizeof(ParameterStruct), NumElements);
		Desc.Metadata = ParameterStruct::FTypeInfo::GetStructMetadata();
		return Desc;
	}

	/** Returns the total number of bytes allocated for a such buffer. */
	uint32 GetTotalNumBytes() const
	{
		return BytesPerElement * NumElements;
	}

	bool operator == (const FRDGBufferDesc& Other) const
	{
		return (
			BytesPerElement == Other.BytesPerElement &&
			NumElements == Other.NumElements &&
			Usage == Other.Usage &&
			UnderlyingType == Other.UnderlyingType);
	}

	bool operator != (const FRDGBufferDesc& Other) const
	{
		return !(*this == Other);
	}

	/** Stride in bytes for index and structured buffers. */
	uint32 BytesPerElement = 1;

	/** Number of elements. */
	uint32 NumElements = 1;

	/** Bitfields describing the uses of that buffer. */
	EBufferUsageFlags Usage = BUF_None;

	/** The underlying RHI type to use. */
	EUnderlyingType UnderlyingType = EUnderlyingType::VertexBuffer;

	/** Meta data of the layout of the buffer for debugging purposes. */
	const FShaderParametersMetadata* Metadata = nullptr;
};

inline const TCHAR* GetBufferUnderlyingTypeName(FRDGBufferDesc::EUnderlyingType BufferType)
{
	switch (BufferType)
	{
	case FRDGBufferDesc::EUnderlyingType::VertexBuffer:
		return TEXT("VertexBuffer");
	case FRDGBufferDesc::EUnderlyingType::StructuredBuffer:
		return TEXT("StructuredBuffer");
	case FRDGBufferDesc::EUnderlyingType::AccelerationStructure:
		return TEXT("AccelerationStructure");
	}
	return TEXT("");
}

struct FRDGBufferSRVDesc final
	: public FRHIBufferSRVCreateInfo
{
	FRDGBufferSRVDesc() = default;

	FRDGBufferSRVDesc(FRDGBufferRef InBuffer);

	FRDGBufferSRVDesc(FRDGBufferRef InBuffer, EPixelFormat InFormat)
		: FRHIBufferSRVCreateInfo(InFormat)
		, Buffer(InBuffer)
	{
		BytesPerElement = GPixelFormats[Format].BlockBytes;
	}

	FRDGBufferRef Buffer = nullptr;
};

struct FRDGBufferUAVDesc final
	: public FRHIBufferUAVCreateInfo
{
	FRDGBufferUAVDesc() = default;

	FRDGBufferUAVDesc(FRDGBufferRef InBuffer);

	FRDGBufferUAVDesc(FRDGBufferRef InBuffer, EPixelFormat InFormat)
		: FRHIBufferUAVCreateInfo(InFormat)
		, Buffer(InBuffer)
	{}

	FRDGBufferRef Buffer = nullptr;
};

/** Translates from a RDG buffer descriptor to a RHI buffer creation info */
inline FRHIBufferCreateInfo Translate(const FRDGBufferDesc& InDesc);

class RENDERCORE_API FRDGPooledBuffer final
	: public FRefCountedObject
{
public:
	FRDGPooledBuffer(TRefCountPtr<FRHIBuffer> InBuffer, const FRDGBufferDesc& InDesc, uint32 InNumAllocatedElements, const TCHAR* InName)
		: Desc(InDesc)
		, Buffer(MoveTemp(InBuffer))
		, Name(InName)
		, NumAllocatedElements(InNumAllocatedElements)
	{}

	const FRDGBufferDesc Desc;

	/** Finds a UAV matching the descriptor in the cache or creates a new one and updates the cache. */
	FORCEINLINE FRHIUnorderedAccessView* GetOrCreateUAV(const FRHIBufferUAVCreateInfo& UAVDesc) { return ViewCache.GetOrCreateUAV(Buffer, UAVDesc); }

	/** Finds a SRV matching the descriptor in the cache or creates a new one and updates the cache. */
	FORCEINLINE FRHIShaderResourceView* GetOrCreateSRV(const FRHIBufferSRVCreateInfo& SRVDesc) { return ViewCache.GetOrCreateSRV(Buffer, SRVDesc); }

	/** Returns the RHI buffer. */
	FORCEINLINE FRHIBuffer* GetRHI() const { return Buffer; }

	UE_DEPRECATED(5.0, "Buffers types have been consolidated; use GetRHI() instead.")
	FORCEINLINE FRHIBuffer* GetVertexBufferRHI() const { return Buffer; }

	UE_DEPRECATED(5.0, "Buffers types have been consolidated; use GetRHI() instead.")
	FORCEINLINE FRHIBuffer* GetStructuredBufferRHI() const { return Buffer; }

private:
	TRefCountPtr<FRHIBuffer> Buffer;
	FRHIBufferViewCache ViewCache;

	void Reset()
	{
		Owner = nullptr;
		State = {};
	}

	void Finalize()
	{
		Owner = nullptr;
		State.Finalize();
	}

	FRDGBufferDesc GetAlignedDesc() const
	{
		FRDGBufferDesc AlignedDesc = Desc;
		AlignedDesc.NumElements = NumAllocatedElements;
		return AlignedDesc;
	}

	const TCHAR* Name = nullptr;
	FRDGBufferRef Owner = nullptr;
	FRDGSubresourceState State;

	const uint32 NumAllocatedElements;
	uint32 LastUsedFrame = 0;

	friend FRDGBufferPool;
	friend FRDGBuilder;
	friend FRDGBuffer;
	friend FRDGAllocator;
};

/** A render graph tracked buffer. */
class RENDERCORE_API FRDGBuffer final
	: public FRDGParentResource
{
public:
	FRDGBufferDesc Desc;
	const ERDGBufferFlags Flags;

	//////////////////////////////////////////////////////////////////////////
	//! The following methods may only be called during pass execution.

	/** Returns the underlying RHI buffer resource */
	FRHIBuffer* GetRHI() const
	{
		return static_cast<FRHIBuffer*>(FRDGParentResource::GetRHI());
	}

	/** Returns the buffer to use for indirect RHI calls. */
	FORCEINLINE FRHIBuffer* GetIndirectRHICallBuffer() const
	{
		checkf(Desc.Usage & BUF_DrawIndirect, TEXT("Buffer %s was not flagged for indirect draw usage."), Name);
		return GetRHI();
	}

	/** Returns the buffer to use for RHI calls, eg RHILockBuffer. */
	UE_DEPRECATED(5.0, "Buffers types have been consolidated; use GetRHI() instead.")
	FORCEINLINE FRHIBuffer* GetRHIVertexBuffer() const
	{
		return GetRHI();
	}

	/** Returns the buffer to use for structured buffer calls. */
	UE_DEPRECATED(5.0, "Buffers types have been consolidated; use GetRHI() instead.")
	FORCEINLINE FRHIBuffer* GetRHIStructuredBuffer() const
	{
		return GetRHI();
	}

	//////////////////////////////////////////////////////////////////////////

	FRDGBufferHandle GetHandle() const
	{
		return Handle;
	}

private:
	FRDGBuffer(const TCHAR* InName, const FRDGBufferDesc& InDesc, ERDGBufferFlags InFlags)
		: FRDGParentResource(InName, ERDGParentResourceType::Buffer)
		, Desc(InDesc)
		, Flags(InFlags)
	{}

	FRDGBuffer(const TCHAR* InName, const FRDGBufferDesc& InDesc, ERDGBufferFlags InFlags, FRDGBufferNumElementsCallback&& InNumElementsCallback)
		: FRDGBuffer(InName, InDesc, InFlags)
	{
		NumElementsCallback = MoveTemp(InNumElementsCallback);
	}

	/** Assigns a pooled buffer as the backing RHI resource. */
	void SetRHI(FRDGPooledBuffer* InPooledBuffer);

	/** Assigns a transient buffer as the backing RHI resource. */
	void SetRHI(FRHITransientBuffer* InTransientBuffer, FRDGAllocator& Allocator);

	/** Finalizes the buffer for execution; no other transitions are allowed after calling this. */
	void Finalize(FRDGPooledBufferArray& PooledBufferArray);

	/** Finalizes any pending field of the buffer descriptor. */
	void FinalizeDesc()
	{
		if (NumElementsCallback)
		{
			Desc.NumElements = FMath::Max(NumElementsCallback(), 1u);
		}
	}

	FRHIBuffer* GetRHIUnchecked() const
	{
		return static_cast<FRHIBuffer*>(FRDGResource::GetRHIUnchecked());
	}

	/** Returns the current buffer state. Only valid to call after SetRHI. */
	FRDGSubresourceState& GetState() const
	{
		check(State);
		return *State;
	}

	/** Registered handle set by the builder. */
	FRDGBufferHandle Handle;

	/** The next buffer to own the PooledBuffer allocation during execution. */
	FRDGBufferHandle NextOwner;

	union
	{
		/** Assigned pooled buffer pointer. Never reset once assigned. */
		FRDGPooledBuffer* PooledBuffer = nullptr;

		/** Assigned transient buffer pointer. Never reset once assigned. */
		FRHITransientBuffer* TransientBuffer;
	};

	/** The assigned buffer view cache (sourced from the pooled / transient buffer. Never reset. */
	FRHIBufferViewCache* ViewCache = nullptr;

	/** Valid strictly when holding a strong reference; use PooledBuffer instead. */
	TRefCountPtr<FRDGPooledBuffer> Allocation;

	/** Cached state pointer from the pooled / transient buffer. */
	FRDGSubresourceState* State = nullptr;

	/** Tracks the merged subresource state as the graph is built. */
	FRDGSubresourceState* MergeState = nullptr;

	/** Tracks the last pass that produced this resource as the graph is built. */
	FRDGProducerStatesByPipeline LastProducer;

	/** Optional callback to supply NumElements after the creation of this FRDGBuffer. */
	FRDGBufferNumElementsCallback NumElementsCallback;

#if RDG_ENABLE_DEBUG
	struct FRDGBufferDebugData* BufferDebugData = nullptr;
	FRDGBufferDebugData& GetBufferDebugData() const;
#endif

	friend FRDGBuilder;
	friend FRDGBarrierValidation;
	friend FRDGUserValidation;
	friend FRDGBufferRegistry;
	friend FRDGAllocator;
	friend FRDGTrace;
};

/** Render graph tracked buffer SRV. */
class FRDGBufferSRV final
	: public FRDGShaderResourceView
{
public:
	/** Descriptor of the graph tracked SRV. */
	const FRDGBufferSRVDesc Desc;

	FRDGBufferRef GetParent() const override
	{
		return Desc.Buffer;
	}

private:
	FRDGBufferSRV(const TCHAR* InName, const FRDGBufferSRVDesc& InDesc)
		: FRDGShaderResourceView(InName, ERDGViewType::BufferSRV)
		, Desc(InDesc)
	{}

	friend FRDGBuilder;
	friend FRDGViewRegistry;
	friend FRDGAllocator;
};

/** Render graph tracked buffer UAV. */
class FRDGBufferUAV final
	: public FRDGUnorderedAccessView
{
public:
	/** Descriptor of the graph tracked UAV. */
	const FRDGBufferUAVDesc Desc;

	FRDGBufferRef GetParent() const override
	{
		return Desc.Buffer;
	}

private:
	FRDGBufferUAV(const TCHAR* InName, const FRDGBufferUAVDesc& InDesc, ERDGUnorderedAccessViewFlags InFlags)
		: FRDGUnorderedAccessView(InName, ERDGViewType::BufferUAV, InFlags)
		, Desc(InDesc)
	{}

	friend FRDGBuilder;
	friend FRDGViewRegistry;
	friend FRDGAllocator;
};

inline FGraphicsPipelineRenderTargetsInfo ExtractRenderTargetsInfo(const FRDGParameterStruct& ParameterStruct);

#include "RenderGraphResources.inl"