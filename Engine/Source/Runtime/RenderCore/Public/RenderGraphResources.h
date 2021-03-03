// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphParameter.h"
#include "RenderGraphTextureSubresource.h"

struct FPooledRenderTarget;
class FRenderTargetPool;

/** Used for tracking the state of an individual subresource during execution. */
struct FRDGSubresourceState
{
	/** Given a before and after state, returns whether a resource barrier is required. */
	static bool IsTransitionRequired(const FRDGSubresourceState& Previous, const FRDGSubresourceState& Next);

	/** Given a before and after state, returns whether they can be merged into a single state. */
	static bool IsMergeAllowed(ERDGParentResourceType ResourceType, const FRDGSubresourceState& Previous, const FRDGSubresourceState& Next);

	FRDGSubresourceState() = default;

	inline void SetPass(FRDGPassHandle PassHandle)
	{
		FirstPass = LastPass = PassHandle;
	}

	/** Finalizes the state at the end of the transition chain; keeps access intact. */
	void Finalize();

	/** The last used access on the pass. */
	ERHIAccess Access = ERHIAccess::Unknown;

	/** The last used transition flags on the pass. */
	EResourceTransitionFlags Flags = EResourceTransitionFlags::None;

	/** The last used pass hardware pipeline. */
	ERHIPipeline Pipeline = ERHIPipeline::Graphics;

	/** The first pass in this state. */
	FRDGPassHandle FirstPass;

	/** The last pass in this state. */
	FRDGPassHandle LastPass;

	/** The last no-UAV barrier to be used by this subresource. */
	FRDGViewUniqueFilter NoUAVBarrierFilter;
};

using FRDGTextureSubresourceState = TRDGTextureSubresourceArray<FRDGSubresourceState, FDefaultAllocator>;
using FRDGTextureTransientSubresourceState = TRDGTextureSubresourceArray<FRDGSubresourceState, SceneRenderingAllocator>;
using FRDGTextureTransientSubresourceStateIndirect = TRDGTextureSubresourceArray<FRDGSubresourceState*, SceneRenderingAllocator>;

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
		ValidateRHIAccess();
		return ResourceRHI;
	}

	//////////////////////////////////////////////////////////////////////////

protected:
	FRDGResource(const TCHAR* InName)
		: Name(InName)
	{}

	/** Assigns this resource as a simple passthrough container for an RHI resource. */
	void SetPassthroughRHI(FRHIResource* InResourceRHI)
	{
		ResourceRHI = InResourceRHI;
#if RDG_ENABLE_DEBUG
		DebugData.bAllowRHIAccess = true;
		DebugData.bPassthrough = true;
#endif
	}

	bool IsPassthrough() const
	{
#if RDG_ENABLE_DEBUG
		return DebugData.bPassthrough;
#else
		return false;
#endif
	}

	/** Verify that the RHI resource can be accessed at a pass execution. */
	void ValidateRHIAccess() const
	{
#if RDG_ENABLE_DEBUG
		checkf(DebugData.bAllowRHIAccess,
			TEXT("Accessing the RHI resource of %s at this time is not allowed. If you hit this check in pass, ")
			TEXT("that is due to this resource not being referenced in the parameters of your pass."),
			Name);
#endif
	}

	FRHIResource* GetRHIUnchecked() const
	{
		return ResourceRHI;
	}

	FRHIResource* ResourceRHI = nullptr;

private:
#if RDG_ENABLE_DEBUG
	class FDebugData
	{
	private:
		/** Boolean to track at runtime whether a resource is actually used by the lambda of a pass or not, to detect unnecessary resource dependencies on passes. */
		bool bIsActuallyUsedByPass = false;

		/** Boolean to track at pass execution whether the underlying RHI resource is allowed to be accessed. */
		bool bAllowRHIAccess = false;

		/** If true, the resource is not attached to any builder and exists as a dummy container for staging code to RDG. */
		bool bPassthrough = false;

		friend FRDGResource;
		friend FRDGUserValidation;
	} DebugData;
#endif

	friend FRDGBuilder;
	friend FRDGUserValidation;
};

class FRDGUniformBuffer
	: public FRDGResource
{
public:
	FORCEINLINE bool IsGlobal() const
	{
		return bGlobal;
	}

	FORCEINLINE const FRDGParameterStruct& GetParameters() const
	{
		return ParameterStruct;
	}

#if RDG_ENABLE_DEBUG
	RENDERCORE_API void MarkResourceAsUsed() override;
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
	explicit FRDGUniformBuffer(TParameterStruct* InParameters, const TCHAR* InName)
		: FRDGResource(InName)
		, ParameterStruct(InParameters)
		, bGlobal(ParameterStruct.HasStaticSlot())
	{}

private:

	const FRDGParameterStruct ParameterStruct;
	TRefCountPtr<FRHIUniformBuffer> UniformBufferRHI;
	FRDGUniformBufferHandle Handle;

	/** Whether this uniform buffer is bound globally or locally to a shader. */
	uint8 bGlobal : 1;

	friend FRDGBuilder;
	friend FRDGUniformBufferRegistry;
	friend FRDGAllocator;
};

template <typename ParameterStructType>
class TRDGUniformBuffer : public FRDGUniformBuffer
{
public:
	FORCEINLINE const TRDGParameterStruct<ParameterStructType>& GetParameters() const
	{
		return static_cast<const TRDGParameterStruct<ParameterStructType>&>(FRDGUniformBuffer::GetParameters());
	}

	FORCEINLINE TUniformBufferRef<ParameterStructType> GetRHIRef() const
	{
		return TUniformBufferRef<ParameterStructType>(GetRHI());
	}

	FORCEINLINE const ParameterStructType* operator->() const
	{
		return GetParameters().GetContents();
	}

private:
	explicit TRDGUniformBuffer(ParameterStructType* InParameters, const TCHAR* InName)
		: FRDGUniformBuffer(InParameters, InName)
	{}

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

	bool IsExternal() const
	{
		return bExternal;
	}

protected:
	FRDGParentResource(const TCHAR* InName, ERDGParentResourceType InType);

	/** Whether this is an externally registered resource. */
	uint8 bExternal : 1;

	/** Whether this is an extracted resource. */
	uint8 bExtracted : 1;

	/** Whether this resource needs acquire / discard. */
	uint8 bTransient : 1;

	/** Whether this resource is the last owner of its allocation (i.e. nothing aliases the allocation later in the execution timeline). */
	uint8 bLastOwner : 1;

	/** If true, the resource was not used by any pass not culled by the graph. */
	uint8 bCulled : 1;

	/** If true, the resource has been used on an async compute pass and may have async compute states. */
	uint8 bUsedByAsyncComputePass : 1;

private:
	/** Number of references in passes and deferred queries. */
	uint16 ReferenceCount = 0;

	/** The initial and final states of the resource assigned by the user, if known. */
	ERHIAccess AccessInitial = ERHIAccess::Unknown;
	ERHIAccess AccessFinal = ERHIAccess::Unknown;

	FRDGPassHandle FirstPass;
	FRDGPassHandle LastPass;

#if RDG_ENABLE_DEBUG
	class FParentDebugData
	{
	private:
		/** Pointer towards the pass that is the first to produce it, for even more convenient error message. */
		const FRDGPass* FirstProducer = nullptr;

		/** Count the number of times it has been used by a pass (without culling). */
		uint32 PassAccessCount = 0;

		/** Tracks at wiring time if a resource has ever been produced by a pass, to error out early if accessing a resource that has not been produced. */
		bool bHasBeenProduced = false;

		/** Tracks whether this resource was clobbered by the builder prior to use. */
		bool bHasBeenClobbered = false;

		friend FRDGUserValidation;
	} ParentDebugData;
#endif

	friend FRDGBuilder;
	friend FRDGUserValidation;
	friend FRDGBarrierBatchBegin;
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

	friend FRDGBuilder;
	friend FRDGViewRegistry;
	friend FRDGAllocator;
};

/** Descriptor used to create a render graph texture. */
struct RENDERCORE_API FRDGTextureDesc
{
	UE_DEPRECATED(4.26, "FRDGTextureDesc has been refactored. Use Create2D instead.")
	static FRDGTextureDesc Create2DDesc(
		FIntPoint InExtent,
		EPixelFormat InFormat,
		const FClearValueBinding& InClearValue,
		ETextureCreateFlags InFlags,
		ETextureCreateFlags InTargetableFlags,
		bool bInForceSeparateTargetAndShaderResource,
		uint16 InNumMips = 1);

	UE_DEPRECATED(4.26, "FRDGTextureDesc has been refactored. Use Create3D instead.")
	static FRDGTextureDesc CreateVolumeDesc(
		uint32 InSizeX,
		uint32 InSizeY,
		uint32 InSizeZ,
		EPixelFormat InFormat,
		const FClearValueBinding& InClearValue,
		ETextureCreateFlags InFlags,
		ETextureCreateFlags InTargetableFlags,
		bool bInForceSeparateTargetAndShaderResource,
		uint16 InNumMips = 1);

	UE_DEPRECATED(4.26, "FRDGTextureDesc has been refactored. Use CreateCube instead.")
	static FRDGTextureDesc CreateCubemapDesc(
		uint32 InExtent,
		EPixelFormat InFormat,
		const FClearValueBinding& InClearValue,
		ETextureCreateFlags InFlags,
		ETextureCreateFlags InTargetableFlags,
		bool bInForceSeparateTargetAndShaderResource,
		uint32 InArraySize = 1,
		uint16 InNumMips = 1);

	UE_DEPRECATED(4.26, "FRDGTextureDesc has been refactored. Use CreateCubeArray instead.")
	static FRDGTextureDesc CreateCubemapArrayDesc(
		uint32 InExtent,
		EPixelFormat InFormat,
		const FClearValueBinding& InClearValue,
		ETextureCreateFlags InFlags,
		ETextureCreateFlags InTargetableFlags,
		bool bInForceSeparateTargetAndShaderResource,
		uint32 InArraySize,
		uint16 InNumMips = 1);

	static FRDGTextureDesc Create2D(
		FIntPoint InExtent,
		EPixelFormat InFormat,
		FClearValueBinding InClearValue,
		ETextureCreateFlags InFlags,
		uint8 InNumMips = 1,
		uint8 InNumSamples = 1)
	{
		return FRDGTextureDesc(InClearValue, ETextureDimension::Texture2D, InFlags, InFormat, InExtent, 1, 1, InNumMips, InNumSamples);
	}

	static FRDGTextureDesc Create2DArray(
		FIntPoint InExtent,
		EPixelFormat InFormat,
		FClearValueBinding InClearValue,
		ETextureCreateFlags InFlags,
		uint32 InArraySize,
		uint8 InNumMips = 1,
		uint8 InNumSamples = 1)
	{
		return FRDGTextureDesc(InClearValue, ETextureDimension::Texture2DArray, InFlags, InFormat, InExtent, 1, InArraySize, InNumMips, InNumSamples);
	}

	static FRDGTextureDesc Create3D(
		FIntVector InSize,
		EPixelFormat InFormat,
		FClearValueBinding InClearValue,
		ETextureCreateFlags InFlags,
		uint8 InNumMips = 1,
		uint8 InNumSamples = 1)
	{
		return FRDGTextureDesc(InClearValue, ETextureDimension::Texture3D, InFlags, InFormat, FIntPoint(InSize.X, InSize.Y), InSize.Z, 1, InNumMips, InNumSamples);
	}

	static FRDGTextureDesc CreateCube(
		uint32 InSizeInPixels,
		EPixelFormat InFormat,
		FClearValueBinding InClearValue,
		ETextureCreateFlags InFlags,
		uint8 InNumMips = 1,
		uint8 InNumSamples = 1)
	{
		return FRDGTextureDesc(InClearValue, ETextureDimension::TextureCube, InFlags, InFormat, FIntPoint(InSizeInPixels, InSizeInPixels), 1, 1, InNumMips, InNumSamples);
	}

	static FRDGTextureDesc CreateCubeArray(
		uint32 InSizeInPixels,
		EPixelFormat InFormat,
		FClearValueBinding InClearValue,
		ETextureCreateFlags InFlags,
		uint32 InArraySize,
		uint8 InNumMips = 1,
		uint8 InNumSamples = 1)
	{
		return FRDGTextureDesc(InClearValue, ETextureDimension::TextureCubeArray, InFlags, InFormat, FIntPoint(InSizeInPixels, InSizeInPixels), 1, InArraySize, InNumMips, InNumSamples);
	}

	FRDGTextureDesc() = default;
	FRDGTextureDesc(
		FClearValueBinding InClearValue,
		ETextureDimension InDimension,
		ETextureCreateFlags InFlags,
		EPixelFormat InFormat,
		FIntPoint InExtent,
		uint16 InDepth = 1,
		uint16 InArraySize = 1,
		uint8 InNumMips = 1,
		uint8 InNumSamples = 1)
		: ClearValue(InClearValue)
		, Dimension(InDimension)
		, Flags(InFlags)
		, Format(InFormat)
		, Extent(InExtent)
		, Depth(InDepth)
		, ArraySize(InArraySize)
		, NumMips(InNumMips)
		, NumSamples(InNumSamples)
	{}

	void Reset()
	{
		// Usually we don't want to propagate MSAA samples.
		NumSamples = 1;

		// Remove UAV flag for textures that don't need it (some formats are incompatible).
		Flags |= TexCreate_RenderTargetable;
		Flags &= ~(TexCreate_UAV | TexCreate_ResolveTargetable | TexCreate_DepthStencilResolveTarget);
	}

	bool IsTexture2D() const
	{
		return Dimension == ETextureDimension::Texture2D || Dimension == ETextureDimension::Texture2DArray;
	}

	bool IsTexture3D() const
	{
		return Dimension == ETextureDimension::Texture3D;
	}

	bool IsTextureCube() const
	{
		return Dimension == ETextureDimension::TextureCube || Dimension == ETextureDimension::TextureCubeArray;
	}

	bool IsTextureArray() const
	{
		return Dimension == ETextureDimension::Texture2DArray || Dimension == ETextureDimension::TextureCubeArray;
	}

	bool IsMipChain() const
	{
		return NumMips > 1;
	}

	bool IsMultisample() const
	{
		return NumSamples > 1;
	}

	FIntVector GetSize() const
	{
		return FIntVector(Extent.X, Extent.Y, Depth);
	}

	FRDGTextureSubresourceLayout GetSubresourceLayout() const
	{
		return FRDGTextureSubresourceLayout(NumMips, ArraySize * (IsTextureCube() ? 6 : 1), IsStencilFormat(Format) ? 2 : 1);
	}

	/** Returns whether this descriptor conforms to requirements. */
	bool IsValid() const;

	/** Clear value to use when fast-clearing the texture. */
	FClearValueBinding ClearValue;

	/** Texture dimension to use when creating the RHI texture. */
	ETextureDimension Dimension = ETextureDimension::Texture2D;

	/** Texture flags passed on to RHI texture. */
	ETextureCreateFlags Flags = TexCreate_None;

	/** Pixel format used to create RHI texture. */
	EPixelFormat Format = PF_Unknown;

	/** Extent of the texture in x and y. */
	FIntPoint Extent = FIntPoint(1, 1);

	/** Depth of the texture if the dimension is 3D. */
	uint16 Depth = 1;

	/** The number of array elements in the texture. (Keep at 1 if dimension is 3D). */
	uint16 ArraySize = 1;

	/** Number of mips in the texture mip-map chain. */
	uint8 NumMips = 1;

	/** Number of samples in the texture. >1 for MSAA. */
	uint8 NumSamples = 1;
};

/** Translates from a pooled render target descriptor to an RDG texture descriptor. */
inline FRDGTextureDesc Translate(const FPooledRenderTargetDesc& InDesc, ERenderTargetTexture InTexture = ERenderTargetTexture::Targetable);

/** Translates from an RDG texture descriptor to a pooled render target descriptor. */
inline FPooledRenderTargetDesc Translate(const FRDGTextureDesc& InDesc);

class RENDERCORE_API FRDGPooledTexture
{
public:
	const FRDGTextureDesc Desc;

	FRHITexture* GetRHI() const
	{
		return Texture;
	}

	FRDGTexture* GetOwner() const
	{
		return Owner;
	}

	uint32 GetRefCount() const
	{
		return RefCount;
	}

	uint32 AddRef() const
	{
		return ++RefCount;
	}

	uint32 Release() const
	{
		if (--RefCount == 0)
		{
			delete this;
			return 0;
		}
		return RefCount;
	}

private:
	FRDGPooledTexture(FRHITexture* InTexture, const FRDGTextureDesc& InDesc, const FUnorderedAccessViewRHIRef& FirstMipUAV)
		: Desc(InDesc)
		, Texture(InTexture)
		, Layout(InDesc.GetSubresourceLayout())
	{
		InitViews(FirstMipUAV);
		Reset();
	}

	/** Initializes cached views. Safe to call multiple times; each call will recreate. */
	void InitViews(const FUnorderedAccessViewRHIRef& FirstMipUAV);

	void Finalize()
	{
		for (FRDGSubresourceState& SubresourceState : State)
		{
			SubresourceState.Finalize();
		}
		Owner = nullptr;
	}

	void Reset()
	{
		InitAsWholeResource(State);
		Owner = nullptr;
	}

	FRHITexture* Texture = nullptr;
	FRDGTexture* Owner = nullptr;
	FRDGTextureSubresourceLayout Layout;
	FRDGTextureSubresourceState State;

	/** Cached views created for the RHI texture. */
	TArray<FUnorderedAccessViewRHIRef, TInlineAllocator<1>> MipUAVs;
	TArray<TPair<FRHITextureSRVCreateInfo, FShaderResourceViewRHIRef>, TInlineAllocator<1>> SRVs;
	FUnorderedAccessViewRHIRef HTileUAV;
	FShaderResourceViewRHIRef  HTileSRV;
	FUnorderedAccessViewRHIRef StencilUAV;
	FShaderResourceViewRHIRef  FMaskSRV;
	FShaderResourceViewRHIRef  CMaskSRV;

	mutable uint32 RefCount = 0;

	friend FRDGTexture;
	friend FRDGBuilder;
	friend FPooledRenderTarget;
	friend FRenderTargetPool;
};

/** Render graph tracked Texture. */
class RENDERCORE_API FRDGTexture final
	: public FRDGParentResource
{
public:
	/** Creates a passthrough texture suitable for filling RHI uniform buffers with RDG parameters for passes not yet ported to RDG. */
	static FRDGTextureRef GetPassthrough(const TRefCountPtr<IPooledRenderTarget>& PooledRenderTarget);

	const FRDGTextureDesc Desc;
	const ERDGTextureFlags Flags;

	//////////////////////////////////////////////////////////////////////////
	//! The following methods may only be called during pass execution.

	/** Returns the allocated pooled render target. */
	IPooledRenderTarget* GetPooledRenderTarget() const
	{
		ValidateRHIAccess();
		check(PooledRenderTarget);
		return PooledRenderTarget;
	}

	/** Returns the allocated RHI texture. */
	FRHITexture* GetRHI() const
	{
		return static_cast<FRHITexture*>(FRDGResource::GetRHI());
	}

	//////////////////////////////////////////////////////////////////////////

	FRDGTextureSubresourceLayout GetSubresourceLayout() const
	{
		return Layout;
	}

	FRDGTextureSubresourceRange GetSubresourceRange() const
	{
		return FRDGTextureSubresourceRange(Layout);
	}

	FRDGTextureSubresourceRange GetSubresourceRangeSRV() const;

private:
	FRDGTexture(const TCHAR* InName, const FRDGTextureDesc& InDesc, ERDGTextureFlags InFlags, ERenderTargetTexture InRenderTargetTexture)
		: FRDGParentResource(InName, ERDGParentResourceType::Texture)
		, Desc(InDesc)
		, Flags(InFlags)
		, RenderTargetTexture(InRenderTargetTexture)
		, Layout(InDesc.GetSubresourceLayout())
	{
		InitAsWholeResource(MergeState);
		InitAsWholeResource(LastProducers);
	}

	/** Assigns the pooled texture to this texture; returns the previous texture to own the allocation. */
	void SetRHI(FPooledRenderTarget* PooledRenderTarget, FRDGTextureRef& OutPreviousOwner);

	/** Finalizes the texture for execution; no other transitions are allowed after calling this. */
	void Finalize();

	/** Returns RHI texture without access checks. */
	FRHITexture* GetRHIUnchecked() const
	{
		return static_cast<FRHITexture*>(FRDGResource::GetRHIUnchecked());
	}

	/** Whether this texture is the last owner of the allocation in the graph. */
	bool IsLastOwner() const
	{
		return NextOwner.IsNull();
	}

	/** Returns the current texture state. Only valid to call after SetRHI. */
	FRDGTextureSubresourceState& GetState()
	{
		check(State);
		return *State;
	}

	/** Describes which RHI texture this RDG texture represents on a pooled texture. Must be default unless the texture is externally registered. */
	const ERenderTargetTexture RenderTargetTexture;

	/** The layout used to facilitate subresource transitions. */
	FRDGTextureSubresourceLayout Layout;

	/** The next texture to own the PooledTexture allocation during execution. */
	FRDGTextureHandle NextOwner;

	/** The handle registered with the builder. */
	FRDGTextureHandle Handle;

	/** The assigned pooled render target to use during execution. Never reset. */
	IPooledRenderTarget* PooledRenderTarget = nullptr;

	/** The assigned pooled texture to use during execution. Never reset. */
	FRDGPooledTexture* PooledTexture = nullptr;

	/** Cached state pointer from the pooled texture. */
	FRDGTextureSubresourceState* State = nullptr;

	/** Valid strictly when holding a strong reference; use PooledRenderTarget instead. */
	TRefCountPtr<IPooledRenderTarget> Allocation;

	/** Tracks merged subresource states as the graph is built. */
	FRDGTextureTransientSubresourceStateIndirect MergeState;

	/** Tracks pass producers for each subresource as the graph is built. */
	TRDGTextureSubresourceArray<FRDGPassHandle> LastProducers;

#if RDG_ENABLE_DEBUG
	class FTextureDebugData
	{
	private:
		/** Tracks whether a UAV has ever been allocated to catch when TexCreate_UAV was unneeded. */
		bool bHasNeededUAV = false;

		/** Tracks whether has ever been bound as a render target to catch when TexCreate_RenderTargetable was unneeded. */
		bool bHasBeenBoundAsRenderTarget = false;

		friend FRDGUserValidation;
		friend FRDGBarrierValidation;
	} TextureDebugData;
#endif

	friend FRDGBuilder;
	friend FRDGUserValidation;
	friend FRDGBarrierValidation;
	friend FRDGTextureRegistry;
	friend FRDGAllocator;
	friend FPooledRenderTarget;
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
	ERDGTextureMetaDataAccess MetaData = ERDGTextureMetaDataAccess::None;

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
		Desc.MipLevel = MipLevel;
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
class FRDGTextureUAVDesc
{
public:
	FRDGTextureUAVDesc() = default;

	FRDGTextureUAVDesc(FRDGTextureRef InTexture, uint8 InMipLevel = 0)
		: Texture(InTexture)
		, MipLevel(InMipLevel)
	{}

	/** Create UAV with access to a specific meta data plane */
	static FRDGTextureUAVDesc CreateForMetaData(FRDGTextureRef Texture, ERDGTextureMetaDataAccess MetaData)
	{
		FRDGTextureUAVDesc Desc = FRDGTextureUAVDesc(Texture, 0);
		Desc.MetaData = MetaData;
		return Desc;
	}

	FRDGTextureRef Texture = nullptr;
	uint8 MipLevel = 0;
	ERDGTextureMetaDataAccess MetaData = ERDGTextureMetaDataAccess::None;
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
	// Type of buffers to the RHI
	enum class EUnderlyingType
	{
		VertexBuffer,
		IndexBuffer, // not implemented yet.
		StructuredBuffer,
	};

	static inline FRHITransitionInfo::EType GetTransitionResourceType(EUnderlyingType Type)
	{
		switch (Type)
		{
		default: checkNoEntry();
		case EUnderlyingType::VertexBuffer: return FRHITransitionInfo::EType::VertexBuffer;
		case EUnderlyingType::IndexBuffer: return FRHITransitionInfo::EType::IndexBuffer;
		case EUnderlyingType::StructuredBuffer: return FRHITransitionInfo::EType::StructuredBuffer;
		}
	}

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

	static FRDGBufferDesc CreateBufferDesc(uint32 BytesPerElement, uint32 NumElements)
	{
		FRDGBufferDesc Desc;
		Desc.UnderlyingType = EUnderlyingType::VertexBuffer;
		Desc.Usage = (EBufferUsageFlags)(BUF_Static | BUF_UnorderedAccess | BUF_ShaderResource);
		Desc.BytesPerElement = BytesPerElement;
		Desc.NumElements = NumElements;
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

	static FRDGBufferDesc CreateUploadDesc(uint32 BytesPerElement, uint32 NumElements)
	{
		FRDGBufferDesc Desc;
		Desc.UnderlyingType = EUnderlyingType::VertexBuffer;
		Desc.Usage = (EBufferUsageFlags)(BUF_Static | BUF_ShaderResource);
		Desc.BytesPerElement = BytesPerElement;
		Desc.NumElements = NumElements;
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

	/** The underlying RHI type to use. A bit of a work around because RHI still have 3 different objects. */
	EUnderlyingType UnderlyingType = EUnderlyingType::VertexBuffer;
};

struct FRDGBufferSRVDesc
{
	FRDGBufferSRVDesc() = default;

	FRDGBufferSRVDesc(FRDGBufferRef InBuffer);

	FRDGBufferSRVDesc(FRDGBufferRef InBuffer, EPixelFormat InFormat)
		: Buffer(InBuffer)
		, Format(InFormat)
	{
		BytesPerElement = GPixelFormats[Format].BlockBytes;
	}

	FRDGBufferRef Buffer = nullptr;

	/** Number of bytes per element (used for vertex buffer). */
	uint32 BytesPerElement = 1;

	/** Encoding format for the element (used for vertex buffer). */
	EPixelFormat Format = PF_Unknown;
};

struct FRDGBufferUAVDesc
{
	FRDGBufferUAVDesc() = default;

	FRDGBufferUAVDesc(FRDGBufferRef InBuffer);

	FRDGBufferUAVDesc(FRDGBufferRef InBuffer, EPixelFormat InFormat)
		: Buffer(InBuffer)
		, Format(InFormat)
	{}

	FRDGBufferRef Buffer = nullptr;

	/** Number of bytes per element (used for vertex buffer). */
	EPixelFormat Format = PF_Unknown;

	/** Whether the uav supports atomic counter or append buffer operations (used for structured buffers) */
	bool bSupportsAtomicCounter = false;
	bool bSupportsAppendBuffer = false;
};

class RENDERCORE_API FRDGPooledBuffer
{
public:
	const FRDGBufferDesc Desc;

	/** Finds a UAV matching the descriptor in the cache or creates a new one and updates the cache. */
	FRHIUnorderedAccessView* GetOrCreateUAV(FRDGBufferUAVDesc UAVDesc);

	/** Finds a SRV matching the descriptor in the cache or creates a new one and updates the cache. */
	FRHIShaderResourceView* GetOrCreateSRV(FRDGBufferSRVDesc SRVDesc);

	FRHIVertexBuffer* GetVertexBufferRHI() const
	{
		return VertexBuffer;
	}

	FRHIIndexBuffer* GetIndexBufferRHI() const
	{
		return IndexBuffer;
	}

	FRHIStructuredBuffer* GetStructuredBufferRHI() const
	{
		return StructuredBuffer;
	}

	uint32 GetRefCount() const
	{
		return RefCount;
	}

	uint32 AddRef() const
	{
		return ++RefCount;
	}

	uint32 Release() const
	{
		const uint32 LocalRefCount = --RefCount;
		if (LocalRefCount == 0)
		{
			delete this;
		}
		return LocalRefCount;
	}

	template<typename KeyType, typename ValueType>
	struct TSRVFuncs : BaseKeyFuncs<TPair<KeyType, ValueType>, KeyType, /* bInAllowDuplicateKeys = */ false>
	{
		typedef typename TTypeTraits<KeyType>::ConstPointerType KeyInitType;
		typedef const TPairInitializer<typename TTypeTraits<KeyType>::ConstInitType, typename TTypeTraits<ValueType>::ConstInitType>& ElementInitType;

		static FORCEINLINE KeyInitType GetSetKey(ElementInitType Element)
		{
			return Element.Key;
		}
		static FORCEINLINE bool Matches(KeyInitType A, KeyInitType B)
		{
			return A.BytesPerElement == B.BytesPerElement && A.Format == B.Format;
		}
		static FORCEINLINE uint32 GetKeyHash(KeyInitType Key)
		{
			return HashCombine(uint32(Key.BytesPerElement), uint32(Key.Format));
		}
	};

	template<typename KeyType, typename ValueType>
	struct TUAVFuncs : BaseKeyFuncs<TPair<KeyType, ValueType>, KeyType, /* bInAllowDuplicateKeys = */ false>
	{
		typedef typename TTypeTraits<KeyType>::ConstPointerType KeyInitType;
		typedef const TPairInitializer<typename TTypeTraits<KeyType>::ConstInitType, typename TTypeTraits<ValueType>::ConstInitType>& ElementInitType;

		static FORCEINLINE KeyInitType GetSetKey(ElementInitType Element)
		{
			return Element.Key;
		}
		static FORCEINLINE bool Matches(KeyInitType A, KeyInitType B)
		{
			return A.Format == B.Format && A.bSupportsAtomicCounter == B.bSupportsAtomicCounter && A.bSupportsAppendBuffer == B.bSupportsAppendBuffer;
		}
		static FORCEINLINE uint32 GetKeyHash(KeyInitType Key)
		{
			return (uint32(Key.bSupportsAtomicCounter) << 8) | (uint32(Key.bSupportsAppendBuffer) << 9) | uint32(Key.Format);
		}
	};

private:
	FRDGPooledBuffer(const FRDGBufferDesc& InDesc)
		: Desc(InDesc)
	{}

	FVertexBufferRHIRef VertexBuffer;
	FIndexBufferRHIRef IndexBuffer;
	FStructuredBufferRHIRef StructuredBuffer;
	TMap<FRDGBufferUAVDesc, FUnorderedAccessViewRHIRef, FDefaultSetAllocator, TUAVFuncs<FRDGBufferUAVDesc, FUnorderedAccessViewRHIRef>> UAVs;
	TMap<FRDGBufferSRVDesc, FShaderResourceViewRHIRef, FDefaultSetAllocator, TSRVFuncs<FRDGBufferSRVDesc, FShaderResourceViewRHIRef>> SRVs;

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

	const TCHAR* Name = nullptr;

	FRDGBufferRef Owner = nullptr;
	FRDGSubresourceState State;

	mutable uint32 RefCount = 0;
	uint32 LastUsedFrame = 0;

	friend FRenderGraphResourcePool;
	friend FRDGBuilder;
	friend FRDGBuffer;
};

UE_DEPRECATED(4.26, "FRDGPooledBuffer has been renamed to FRDGPooledBuffer.")
typedef FRDGPooledBuffer FPooledRDGBuffer;

/** A render graph tracked buffer. */
class RENDERCORE_API FRDGBuffer final
	: public FRDGParentResource
{
public:
	const FRDGBufferDesc Desc;
	const ERDGBufferFlags Flags;

	//////////////////////////////////////////////////////////////////////////
	//! The following methods may only be called during pass execution.

	/** Returns the buffer to use for indirect RHI calls. */
	FRHIVertexBuffer* GetIndirectRHICallBuffer() const
	{
		checkf(Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer, TEXT("Buffer %s is not an underlying vertex buffer."), Name);
		checkf(Desc.Usage & BUF_DrawIndirect, TEXT("Buffer %s was not flagged for indirect draw usage."), Name);
		return static_cast<FRHIVertexBuffer*>(GetRHI());
	}

	/** Returns the buffer to use for RHI calls, eg RHILockVertexBuffer. */
	FRHIVertexBuffer* GetRHIVertexBuffer() const
	{
		checkf(Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer, TEXT("Buffer %s is not an underlying vertex buffer."), Name);
		return static_cast<FRHIVertexBuffer*>(GetRHI());
	}

	/** Returns the buffer to use for structured buffer calls. */
	FRHIStructuredBuffer* GetRHIStructuredBuffer() const
	{
		checkf(Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer, TEXT("Buffer %s is not an underlying structured buffer."), Name);
		return static_cast<FRHIStructuredBuffer*>(GetRHI());
	}

	//////////////////////////////////////////////////////////////////////////

private:
	FRDGBuffer(const TCHAR* InName, const FRDGBufferDesc& InDesc, ERDGBufferFlags InFlags)
		: FRDGParentResource(InName, ERDGParentResourceType::Buffer)
		, Desc(InDesc)
		, Flags(InFlags)
	{}

	/** Assigns the pooled buffer to this buffer; returns the previous buffer to own the allocation. */
	void SetRHI(FRDGPooledBuffer* InPooledBuffer, FRDGBufferRef& OutPreviousOwner);

	/** Finalizes the buffer for execution; no other transitions are allowed after calling this. */
	void Finalize();

	/** Returns the current buffer state. Only valid to call after SetRHI. */
	FRDGSubresourceState& GetState() const
	{
		check(State);
		return *State;
	}

	/** Registered handle set by the builder. */
	FRDGBufferHandle Handle;

	/** Tracks the last pass that produced this resource as the graph is built. */
	FRDGPassHandle LastProducer;

	/** The next buffer to own the PooledBuffer allocation during execution. */
	FRDGBufferHandle NextOwner;

	/** Assigned pooled buffer pointer. Never reset once assigned. */
	FRDGPooledBuffer* PooledBuffer = nullptr;

	/** Cached state pointer from the pooled buffer. */
	FRDGSubresourceState* State = nullptr;

	/** Valid strictly when holding a strong reference; use PooledBuffer instead. */
	TRefCountPtr<FRDGPooledBuffer> Allocation;

	/** Tracks the merged subresource state as the graph is built. */
	FRDGSubresourceState* MergeState = nullptr;

#if RDG_ENABLE_DEBUG
	class FBufferDebugData
	{
	private:
		/** Tracks state changes in order of execution. */
		TArray<TPair<FRDGPassHandle, FRDGSubresourceState>, SceneRenderingAllocator> States;

		friend FRDGBarrierValidation;
	} BufferDebugData;
#endif

	friend FRDGBuilder;
	friend FRDGBarrierValidation;
	friend FRDGBufferRegistry;
	friend FRDGAllocator;
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

#include "RenderGraphResources.inl"