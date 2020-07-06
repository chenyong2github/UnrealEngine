// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"

class FRDGPass;
class FRDGBuilder;
class FRDGEventName;

class FRDGResource;
using FRDGResourceRef = FRDGResource*;

class FRDGParentResource;
using FRDGParentResourceRef = FRDGParentResource*;

class FRDGChildResource;
using FRDGChildResourceRef = FRDGChildResource*;

class FRDGShaderResourceView;
using FRDGShaderResourceViewRef = FRDGShaderResourceView*;

class FRDGUnorderedAccessView;
using FRDGUnorderedAccessViewRef = FRDGUnorderedAccessView*;

using FRDGPooledTexture = IPooledRenderTarget;

class FRDGTexture;
using FRDGTextureRef = FRDGTexture*;

class FRDGTextureSRV;
using FRDGTextureSRVRef = FRDGTextureSRV*;

class FRDGTextureUAV;
using FRDGTextureUAVRef = FRDGTextureUAV*;

class FRDGBuffer;
using FRDGBufferRef = FRDGBuffer*;

class FRDGBufferSRV;
using FRDGBufferSRVRef = FRDGBufferSRV*;

class FRDGBufferUAV;
using FRDGBufferUAVRef = FRDGBufferUAV*;

/** Used for tracking the state of an individual subresource during execution. */
struct FRDGSubresourceState
{
	static bool IsTransitionRequired(const FRDGSubresourceState& Previous, const FRDGSubresourceState& Next);

	FRDGSubresourceState() = default;

	FRDGSubresourceState(
		FRDGPassHandle InPassHandle,
		ERDGPipeline InPipeline,
		EResourceTransitionAccess InAccess,
		EResourceTransitionFlags InFlags = EResourceTransitionFlags::None,
		FRDGResourceHandle InNoUAVBarrierHandle = FRDGResourceHandle::Null)
		: PassHandle(InPassHandle)
		, NoUAVBarrierFilter(InNoUAVBarrierHandle)
		, Pipeline(InPipeline)
		, Access(InAccess)
		, Flags(InFlags)
	{}

	/** Assigns the pass / pipeline. */
	void SetPass(FRDGPassHandle InPassHandle, ERDGPipeline InPipeline);

	/** Merges pipeline / access info and clears everything else. */
	void MergeSanitizedFrom(const FRDGSubresourceState& Other);

	/** If the other subresource state has a valid access and is on a different pipeline, the merge succeeds but retains the existing pipeline. */
	bool MergeCrossPipelineFrom(const FRDGSubresourceState& StateOther);

	/** If the other subresource state has a valid access, the merge succeeds. */
	bool MergeFrom(const FRDGSubresourceState& StateOther);

	/** Sanitizes all transient graph state, leaving the access and pipeline members. */
	void Sanitize();

	/** The last pass the resource was used. */
	FRDGPassHandle PassHandle;

	/** The last no-UAV barrier to be used by this subresource. */
	FRDGResourceUniqueFilter NoUAVBarrierFilter;

	/** The last used pass hardware pipeline. */
	ERDGPipeline Pipeline = ERDGPipeline::Graphics;

	/** The last used access on the pass. */
	EResourceTransitionAccess Access = EResourceTransitionAccess::Unknown;

	/** The last used transition flags on the pass. */
	EResourceTransitionFlags Flags = EResourceTransitionFlags::None;
};

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
	void MarkResourceAsUsed()
	{
		ValidateRHIAccess();
#if RDG_ENABLE_DEBUG
		DebugData.bIsActuallyUsedByPass = true;
#endif
	}

	FRHIResource* GetRHI() const
	{
		ValidateRHIAccess();
		return ResourceRHI;
	}

	//////////////////////////////////////////////////////////////////////////

	FRDGResourceHandle GetHandle() const
	{
		return Handle;
	}

protected:
	FRDGResource(const TCHAR* InName)
		: Name(InName)
	{}

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
	FRDGResourceHandle Handle;

#if RDG_ENABLE_DEBUG
	class FDebugData
	{
	private:
		/** Boolean to track at runtime whether a resource is actually used by the lambda of a pass or not, to detect unnecessary resource dependencies on passes. */
		bool bIsActuallyUsedByPass = false;

		/** Boolean to track at pass execution whether the underlying RHI resource is allowed to be accessed. */
		bool bAllowRHIAccess = false;

		friend class FRDGResource;
		friend class FRDGUserValidation;
	} DebugData;
#endif

	friend class FRDGBuilder;
	friend class FRDGUserValidation;

	template <typename TRHIResource, typename TRDGResource>
	friend TRHIResource* GetRHIUnchecked(TRDGResource*);
};

/** A render graph resource with an allocation lifetime tracked by the graph. May have child resources which reference it (e.g. views). */
class RENDERCORE_API FRDGParentResource
	: public FRDGResource
{
public:
	/** The type of this resource; useful for casting between types. */
	const ERDGParentResourceType Type;

	/** Flags specific to this resource for render graph. */
	const ERDGParentResourceFlags Flags;

	/** Whether this is an externally registered resource. */
	const bool bIsExternal = false;

protected:
	FRDGParentResource(
		const TCHAR* InName,
		ERDGParentResourceType InType,
		ERDGParentResourceFlags InFlags,
		bool bIsExternal);

private:
	/** Number of references in passes and deferred queries. */
	int32 ReferenceCount = 0;

	/** Number of passes yet to compile the resource. */
	int32 CompilePassCount = 0;

	/** The initial and final states of the resource assigned by the user, if known. */
	EResourceTransitionAccess AccessInitial = EResourceTransitionAccess::Unknown;
	EResourceTransitionAccess AccessFinal = EResourceTransitionAccess::Unknown;

	/** The first state that this resource was used in on the graph. */
	FRDGSubresourceState StateFirst;

	/** The last no-barrier UAV used to write to this resource. */
	FRDGUnorderedAccessView* LastNoBarrierUAV = nullptr;

	/** The last pass that produced this resource. */
	FRDGPassHandle LastProducer;

	/** The last passes that consumed this resource since the last producer. */
	FRDGPassHandleArray LastConsumers;

#if RDG_ENABLE_DEBUG
	class FParentDebugData
	{
	private:
		/** Pointer towards the pass that is the first to produce it, for even more convenient error message. */
		const FRDGPass* FirstProducer = nullptr;

		/** Count the number of times it has been used by a pass. */
		int32 PassAccessCount = 0;

		/** Tracks at wiring time if a resource has ever been produced by a pass, to error out early if accessing a resource that has not been produced. */
		bool bHasBeenProduced = false;

		/** Tracks whether this resource was clobbered by the builder prior to use. */
		bool bHasBeenClobbered = false;

		friend class FRDGUserValidation;
	} ParentDebugData;
#endif

	friend class FRDGBuilder;
	friend class FRDGUserValidation;
};

/** A render graph resource (e.g. a view) which references a single parent resource (e.g. a texture / buffer). Provides an abstract way to access the parent resource. */
class FRDGChildResource
	: public FRDGResource
{
public:
	/** The type of this child resource; useful for casting between types. */
	const ERDGChildResourceType Type;

	/** Flags associated with the child resource. */
	const ERDGChildResourceFlags Flags;

	/** Returns the referenced parent render graph resource. */
	virtual FRDGParentResourceRef GetParent() const = 0;

protected:
	FRDGChildResource(const TCHAR* Name, ERDGChildResourceType InType, ERDGChildResourceFlags InFlags)
		: FRDGResource(Name)
		, Type(InType)
		, Flags(InFlags)
	{}
};

/** Descriptor of a graph tracked texture. */
using FRDGTextureDesc = FPooledRenderTargetDesc;

struct FRDGTextureSubresourceLayout
{
	FRDGTextureSubresourceLayout() = default;

	FRDGTextureSubresourceLayout(uint32 InNumMips, uint32 InNumArraySlices, uint32 InNumPlaneSlices)
		: NumMips(InNumMips)
		, NumArraySlices(InNumArraySlices)
		, NumPlaneSlices(InNumPlaneSlices)
	{}

	inline uint32 GetSubresourceCount() const
	{
		return NumMips * NumArraySlices * NumPlaneSlices;
	}

	inline uint32 GetSubresourceIndex(uint32 MipIndex, uint32 ArraySlice, uint32 PlaneSlice) const
	{
		check(MipIndex < NumMips);
		check(ArraySlice < NumArraySlices);
		check(PlaneSlice < NumPlaneSlices);

		return MipIndex + (ArraySlice * NumMips) + (PlaneSlice * NumMips * NumArraySlices);
	}

	inline bool operator == (FRDGTextureSubresourceLayout const& RHS) const
	{
		return NumMips == RHS.NumMips
			&& NumArraySlices == RHS.NumArraySlices
			&& NumPlaneSlices == RHS.NumPlaneSlices;
	}

	inline bool operator != (FRDGTextureSubresourceLayout const& RHS) const
	{
		return !(*this == RHS);
	}

	uint32 NumMips = 0;
	uint32 NumArraySlices = 0;
	uint32 NumPlaneSlices = 0;
};

struct FRDGTextureSubresourceRange : FRDGTextureSubresourceLayout
{
	FRDGTextureSubresourceRange() = default;

	FRDGTextureSubresourceRange(FRDGTextureSubresourceLayout Layout)
		: FRDGTextureSubresourceLayout(Layout)
	{}

	inline bool operator == (FRDGTextureSubresourceRange const& RHS) const
	{
		return MipIndex == RHS.MipIndex
			&& ArraySlice == RHS.ArraySlice
			&& PlaneSlice == RHS.PlaneSlice
			&& FRDGTextureSubresourceLayout::operator==(RHS);
	}

	inline bool operator != (FRDGTextureSubresourceRange const& RHS) const
	{
		return !(*this == RHS);
	}

	template <typename TFunction>
	inline void EnumerateSubresources(TFunction Function) const
	{
		const uint32 LastMip = MipIndex + NumMips;
		const uint32 LastArraySlice = ArraySlice + NumArraySlices;
		const uint32 LastPlaneSlice = PlaneSlice + NumPlaneSlices;

		for (uint32 LocalPlaneSlice = PlaneSlice; LocalPlaneSlice < LastPlaneSlice; ++LocalPlaneSlice)
		{
			for (uint32 LocalArraySlice = ArraySlice; LocalArraySlice < LastArraySlice; ++LocalArraySlice)
			{
				for (uint32 LocalMipIndex = MipIndex; LocalMipIndex < LastMip; ++LocalMipIndex)
				{
					Function(LocalMipIndex, LocalArraySlice, LocalPlaneSlice);
				}
			}
		}
	}

	uint32 MipIndex = 0;
	uint32 ArraySlice = 0;
	uint32 PlaneSlice = 0;
};

/** Used for tracking the state of a resource and its subresources during execution. */
class FRDGTextureState
{
public:
	FRDGTextureState() = default;
	FRDGTextureState(const FRDGTextureDesc& Desc);

	/** Initializes as a whole resource state. */
	void InitAsWholeResource(FRDGSubresourceState InState);

	/** Initializes as distinct subresources with the same initial state. */
	void InitAsSubresources(FRDGSubresourceState InState);

	/** Assigns a new pass / pipeline to all subresource states. */
	void SetPass(FRDGPassHandle PassHandle, ERDGPipeline Pipeline);

	/** Merges pipeline / access info and clears everything else. */
	void MergeSanitizedFrom(const FRDGTextureState& Other);

	/** Merges only those subresource states which have a known access and are on a different pipeline, keeping the current pipeline intact. */
	void MergeCrossPipelineFrom(const FRDGTextureState& Other);

	/** Merges only those subresource states which have a known access. */
	void MergeFrom(const FRDGTextureState& Other);

	/** Sanitizes transient graph state, leaving the pipeline and access members. */
	void Sanitize();

	inline const FRDGTextureSubresourceLayout& GetSubresourceLayout() const
	{
		return Layout;
	}

	inline bool IsWholeResourceState() const
	{
		return SubresourceStates.Num() == 0;
	}

	inline const FRDGSubresourceState& GetSubresourceState(uint32 MipIndex, uint32 ArraySlice, uint32 PlaneSlice) const
	{
		check(!IsWholeResourceState());
		return SubresourceStates[Layout.GetSubresourceIndex(MipIndex, ArraySlice, PlaneSlice)];
	}

	inline FRDGSubresourceState& GetSubresourceState(uint32 MipIndex, uint32 ArraySlice, uint32 PlaneSlice)
	{
		check(!IsWholeResourceState());
		return SubresourceStates[Layout.GetSubresourceIndex(MipIndex, ArraySlice, PlaneSlice)];
	}

	inline const FRDGSubresourceState& GetWholeResourceState() const
	{
		check(IsWholeResourceState());
		return WholeResourceState;
	}

	inline FRDGSubresourceState& GetWholeResourceState()
	{
		check(IsWholeResourceState());
		return WholeResourceState;
	}

private:
	FRDGTextureSubresourceLayout Layout;
	FRDGSubresourceState WholeResourceState;
	TArray<FRDGSubresourceState, TInlineAllocator<2>> SubresourceStates;
};

/** Render graph tracked Texture. */
class RENDERCORE_API FRDGTexture final
	: public FRDGParentResource
{
public:
	/** Descriptor of the graph tracked texture. */
	const FRDGTextureDesc Desc;

	//////////////////////////////////////////////////////////////////////////
	//! The following methods may only be called during pass execution.

	/** Returns the allocated pooled render target. */
	IPooledRenderTarget* GetPooledRenderTarget() const
	{
		ValidateRHIAccess();
		check(PooledTexture);
		return PooledTexture;
	}

	/** Returns the allocated RHI texture. */
	FRHITexture* GetRHI() const
	{
		return static_cast<FRHITexture*>(FRDGResource::GetRHI());
	}

	//////////////////////////////////////////////////////////////////////////

	const FRDGTextureSubresourceLayout& GetSubresourceLayout() const
	{
		return State.GetSubresourceLayout();
	}

	FRDGTextureSubresourceRange GetSubresourceRange() const
	{
		return FRDGTextureSubresourceRange(State.GetSubresourceLayout());
	}

private:
	FRDGTexture(
		const TCHAR* InName,
		const FPooledRenderTargetDesc& InDesc,
		ERDGParentResourceFlags InFlags,
		bool bIsExternal = false);

	void Init(const TRefCountPtr<IPooledRenderTarget>& PooledTexture);

	/** Returns the allocated RHI texture without access checks. */
	FRHITexture* GetRHIUnchecked() const
	{
		return static_cast<FRHITexture*>(FRDGResource::GetRHIUnchecked());
	}

	TRefCountPtr<IPooledRenderTarget> PooledTexture;

	FRDGTextureState State;
	FRDGTextureState StatePending;

#if RDG_ENABLE_DEBUG
	class FTextureDebugData
	{
	private:
		/** Tracks whether a UAV has ever been allocated to catch when TexCreate_UAV was unneeded. */
		bool bHasNeededUAV = false;

		/** Tracks whether has ever been bound as a render target to catch when TexCreate_RenderTargetable was unneeded. */
		bool bHasBeenBoundAsRenderTarget = false;

		/** Tracks state changes in order of execution. */
		TArray<TPair<const FRDGPass*, FRDGTextureState>, SceneRenderingAllocator> States;

		friend class FRDGUserValidation;
		friend class FRDGBarrierValidation;
	} TextureDebugData;
#endif

	friend class FRDGBuilder;
	friend class FRDGUserValidation;
	friend class FRDGBarrierValidation;

	template <typename TRHIResource, typename TRDGResource>
	friend TRHIResource* GetRHIUnchecked(TRDGResource*);
};

/** Render graph tracked SRV. */
class FRDGShaderResourceView
	: public FRDGChildResource
{
public:
	/** Returns the allocated RHI SRV. */
	FRHIShaderResourceView* GetRHI() const
	{
		return static_cast<FRHIShaderResourceView*>(FRDGResource::GetRHI());
	}

protected:
	FRDGShaderResourceView(
		const TCHAR* InName,
		ERDGChildResourceType InType,
		ERDGChildResourceFlags InFlags)
		: FRDGChildResource(InName, InType, InFlags)
	{}

	/** Returns the allocated RHI SRV without access checks. */
	FRHIShaderResourceView* GetRHIUnchecked() const
	{
		return static_cast<FRHIShaderResourceView*>(FRDGResource::GetRHIUnchecked());
	}
};

/** Render graph tracked UAV. */
class FRDGUnorderedAccessView
	: public FRDGChildResource
{
public:
	/** Returns the allocated RHI UAV. */
	FRHIUnorderedAccessView* GetRHI() const
	{
		return static_cast<FRHIUnorderedAccessView*>(FRDGResource::GetRHI());
	}

protected:
	FRDGUnorderedAccessView(
		const TCHAR* InName,
		ERDGChildResourceType InType,
		ERDGChildResourceFlags InFlags)
		: FRDGChildResource(InName, InType, InFlags)
	{}

	/** Returns the allocated RHI UAV without access checks. */
	FRHIUnorderedAccessView* GetRHIUnchecked() const
	{
		return static_cast<FRHIUnorderedAccessView*>(FRDGResource::GetRHIUnchecked());
	}
};

/** Descriptor for render graph tracked SRV. */
class FRDGTextureSRVDesc final : public FRHITextureSRVCreateInfo
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
		if (Texture->Desc.bIsArray)
		{
			Desc.NumArraySlices = Texture->Desc.Depth;
		}
		return Desc;
	}

	/** Create SRV that access one specific mip level. */
	static FRDGTextureSRVDesc CreateForMipLevel(FRDGTextureRef Texture, int32 MipLevel)
	{
		FRDGTextureSRVDesc Desc;
		Desc.Texture = Texture;
		Desc.MipLevel = MipLevel;
		Desc.NumMipLevels = 1;
		if (Texture->Desc.bIsArray)
		{
			Desc.NumArraySlices = Texture->Desc.Depth;
		}
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

	FRDGTextureSubresourceRange GetSubresourceRange() const
	{
		FRDGTextureSubresourceRange Range = GetParent()->GetSubresourceRange();
		Range.MipIndex = Desc.MipLevel;
		Range.PlaneSlice = GetResourceTransitionPlaneForMetadataAccess(Desc.MetaData);

		if (Desc.NumMipLevels != 0)
		{
			Range.NumMips = Desc.NumMipLevels;
		}

		if (Desc.NumArraySlices != 0)
		{
			Range.NumArraySlices = Desc.NumArraySlices;
		}

		if (Desc.MetaData != ERDGTextureMetaDataAccess::None)
		{
			Range.NumPlaneSlices = 1;
		}

		return Range;
	}

private:
	FRDGTextureSRV(
		const TCHAR* InName,
		const FRDGTextureSRVDesc& InDesc,
		ERDGChildResourceFlags InFlags)
		: FRDGShaderResourceView(InName, ERDGChildResourceType::TextureSRV, InFlags)
		, Desc(InDesc)
	{}

	friend class FRDGBuilder;
};

/** Descriptor for render graph tracked UAV. */
class FRDGTextureUAVDesc
{
public:
	FRDGTextureUAVDesc() = default;

	FRDGTextureUAVDesc(
		FRDGTextureRef InTexture,
		uint8 InMipLevel = 0)
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

	FRDGTextureSubresourceRange GetSubresourceRange() const
	{
		FRDGTextureSubresourceRange Range = GetParent()->GetSubresourceRange();
		Range.MipIndex = Desc.MipLevel;
		Range.NumMips = 1;
		Range.PlaneSlice = GetResourceTransitionPlaneForMetadataAccess(Desc.MetaData);

		if (Desc.MetaData != ERDGTextureMetaDataAccess::None)
		{
			Range.NumPlaneSlices = 1;
		}

		return Range;
	}

private:
	FRDGTextureUAV(
		const TCHAR* InName,
		const FRDGTextureUAVDesc& InDesc,
		ERDGChildResourceFlags InFlags)
		: FRDGUnorderedAccessView(InName, ERDGChildResourceType::TextureUAV, InFlags)
		, Desc(InDesc)
	{}

	friend class FRDGBuilder;
};

/** Descriptor for render graph tracked Buffer. */
struct FRDGBufferDesc
{
	// Type of buffers to the RHI
	// TODO(RDG): refactor RHI to only have one FRHIBuffer.
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
		Desc.Usage = EBufferUsageFlags(BUF_Static | BUF_DrawIndirect | BUF_UnorderedAccess | BUF_ShaderResource);
		Desc.BytesPerElement = sizeof(IndirectParameterStruct);
		Desc.NumElements = NumElements;
		return Desc;
	}

	static FRDGBufferDesc CreateIndirectDesc(uint32 NumElements = 1)
	{
		FRDGBufferDesc Desc;
		Desc.UnderlyingType = EUnderlyingType::VertexBuffer;
		Desc.Usage = EBufferUsageFlags(BUF_Static | BUF_DrawIndirect | BUF_UnorderedAccess | BUF_ShaderResource);
		Desc.BytesPerElement = 4;
		Desc.NumElements = NumElements;
		return Desc;
	}

	static FRDGBufferDesc CreateStructuredDesc(uint32 BytesPerElement, uint32 NumElements)
	{
		FRDGBufferDesc Desc;
		Desc.UnderlyingType = EUnderlyingType::StructuredBuffer;
		Desc.Usage = EBufferUsageFlags(BUF_Static | BUF_UnorderedAccess | BUF_ShaderResource);
		Desc.BytesPerElement = BytesPerElement;
		Desc.NumElements = NumElements;
		return Desc;
	}

	static FRDGBufferDesc CreateBufferDesc(uint32 BytesPerElement, uint32 NumElements)
	{
		FRDGBufferDesc Desc;
		Desc.UnderlyingType = EUnderlyingType::VertexBuffer;
		Desc.Usage = EBufferUsageFlags(BUF_Static | BUF_UnorderedAccess | BUF_ShaderResource);
		Desc.BytesPerElement = BytesPerElement;
		Desc.NumElements = NumElements;
		return Desc;
	}

	static FRDGBufferDesc CreateByteAddressDesc(uint32 NumBytes)
	{
		check(NumBytes % 4 == 0);
		FRDGBufferDesc Desc;
		Desc.UnderlyingType = EUnderlyingType::StructuredBuffer;
		Desc.Usage = EBufferUsageFlags(BUF_UnorderedAccess | BUF_ShaderResource | BUF_ByteAddressBuffer);
		Desc.BytesPerElement = 4;
		Desc.NumElements = NumBytes / 4;
		return Desc;
	}

	static FRDGBufferDesc CreateUploadDesc(uint32 BytesPerElement, uint32 NumElements)
	{
		FRDGBufferDesc Desc;
		Desc.UnderlyingType = EUnderlyingType::VertexBuffer;
		Desc.Usage = EBufferUsageFlags(BUF_Static | BUF_ShaderResource);
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

/** Defines how the map's pairs are hashed. */
template<typename KeyType, typename ValueType>
struct TMapRDGBufferSRVFuncs : BaseKeyFuncs<TPair<KeyType, ValueType>, KeyType, /* bInAllowDuplicateKeys = */ false>
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

/** Defines how the map's pairs are hashed. */
template<typename KeyType, typename ValueType>
struct TMapRDGBufferUAVFuncs : BaseKeyFuncs<TPair<KeyType, ValueType>, KeyType, /* bInAllowDuplicateKeys = */ false>
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

struct FPooledRDGBuffer
{
	FVertexBufferRHIRef VertexBuffer;
	FIndexBufferRHIRef IndexBuffer;
	FStructuredBufferRHIRef StructuredBuffer;
	TMap<FRDGBufferUAVDesc, FUnorderedAccessViewRHIRef, FDefaultSetAllocator, TMapRDGBufferUAVFuncs<FRDGBufferUAVDesc, FUnorderedAccessViewRHIRef>> UAVs;
	TMap<FRDGBufferSRVDesc, FShaderResourceViewRHIRef, FDefaultSetAllocator, TMapRDGBufferSRVFuncs<FRDGBufferSRVDesc, FShaderResourceViewRHIRef>> SRVs;

	/** Descriptor. */
	FRDGBufferDesc Desc;


	// Refcounting
	inline uint32 AddRef()
	{
		return ++RefCount;
	}

	inline uint32 Release()
	{
		const uint32 LocalRefCount = --RefCount;
		if (LocalRefCount == 0)
		{
			delete this;
		}
		return LocalRefCount;
	}

	inline uint32 GetRefCount()
	{
		return RefCount;
	}

private:
	const TCHAR* Name = nullptr;
	uint32 RefCount = 0;
	uint32 LastUsedFrame = 0;

	FRDGSubresourceState State;

	friend class FRenderGraphResourcePool;
	friend class FRDGBuilder;
};

/** Render graph tracked buffers. */
class RENDERCORE_API FRDGBuffer final
	: public FRDGParentResource
{
public:
	/** Descriptor of the graph. */
	const FRDGBufferDesc Desc;

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
	FRDGBuffer(
		const TCHAR* InName,
		const FRDGBufferDesc& InDesc,
		ERDGParentResourceFlags InFlags,
		bool bIsExternal = false);

	void Init(const TRefCountPtr<FPooledRDGBuffer>& InPooledBuffer);

	TRefCountPtr<FPooledRDGBuffer> PooledBuffer;

	FRDGSubresourceState StatePending;
	FRDGSubresourceState State;

#if RDG_ENABLE_DEBUG
	class FBufferDebugData
	{
	private:
		/** Tracks state changes in order of execution. */
		TArray<TPair<const FRDGPass*, FRDGSubresourceState>, SceneRenderingAllocator> States;

		friend class FRDGBarrierValidation;
	} BufferDebugData;
#endif

	friend class FRDGBuilder;
	friend class FRDGBarrierValidation;
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
	FRDGBufferSRV(
		const TCHAR* InName,
		const FRDGBufferSRVDesc& InDesc,
		ERDGChildResourceFlags InFlags)
		: FRDGShaderResourceView(InName, ERDGChildResourceType::BufferSRV, InFlags)
		, Desc(InDesc)
	{}

	friend class FRDGBuilder;
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
	FRDGBufferUAV(
		const TCHAR* InName,
		const FRDGBufferUAVDesc& InDesc,
		ERDGChildResourceFlags InFlags)
		: FRDGUnorderedAccessView(InName, ERDGChildResourceType::BufferUAV, InFlags)
		, Desc(InDesc)
	{}

	friend class FRDGBuilder;
};

inline FRDGBufferSRVDesc::FRDGBufferSRVDesc(FRDGBufferRef InBuffer)
	: Buffer(InBuffer)
{
	if (Buffer->Desc.Usage & BUF_DrawIndirect)
	{
		BytesPerElement = 4;
		Format = PF_R32_UINT;
	}
	else
	{
		checkf(Buffer->Desc.UnderlyingType != FRDGBufferDesc::EUnderlyingType::VertexBuffer, TEXT("VertexBuffer %s requires a type when creating a SRV."), Buffer->Name);
	}
}

inline FRDGBufferUAVDesc::FRDGBufferUAVDesc(FRDGBufferRef InBuffer)
	: Buffer(InBuffer)
{
	if (Buffer->Desc.Usage & BUF_DrawIndirect)
	{
		Format = PF_R32_UINT;
	}
	else
	{
		checkf(Buffer->Desc.UnderlyingType != FRDGBufferDesc::EUnderlyingType::VertexBuffer, TEXT("VertexBuffer %s requires a type when creating a UAV."), Buffer->Name);
	}
}