// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"

class FRDGPass;
class FRDGBuilder;
class FRDGEventName;
class FRDGResource;
class FRDGTrackedResource;
class FRDGTexture;
class FRDGTextureSRV;
class FRDGTextureUAV;
class FRDGBuffer;
class FRDGBufferSRV;
class FRDGBufferUAV;

/** Defines the RDG resource references for user code not forgetting the const every time. */
using FRDGResourceRef = FRDGResource*;
using FRDGTrackedResourceRef = FRDGTrackedResource*;
using FRDGTextureRef = FRDGTexture*;
using FRDGTextureSRVRef = FRDGTextureSRV*;
using FRDGTextureUAVRef = FRDGTextureUAV*;
using FRDGBufferRef = FRDGBuffer*;
using FRDGBufferSRVRef = FRDGBufferSRV*;
using FRDGBufferUAVRef = FRDGBufferUAV*;

/** Used for tracking resource state during execution. */
struct FRDGResourceState
{
	// The hardware pipeline the resource state is compatible with.
	enum class EPipeline : uint8
	{
		Graphics,
		Compute,
		MAX
	};

	// The access permissions the resource state is compatible with.
	enum class EAccess : uint8
	{
		Read,
		Write,
		Unknown,
		MAX = Unknown
	};

	FRDGResourceState() = default;

	FRDGResourceState(const FRDGPass* InPass, EPipeline InPipeline, EAccess InAccess)
		: Pass(InPass)
		, Pipeline(InPipeline)
		, Access(InAccess)
	{}

	bool operator==(const FRDGResourceState& Other) const
	{
		return Pass == Other.Pass && Pipeline == Other.Pipeline && Access == Other.Access;
	}

	bool operator!=(const FRDGResourceState& Other) const
	{
		return !(*this == Other);
	}

	// The last pass the resource was used.
	const FRDGPass* Pass = nullptr;

	// The last used pass hardware pipeline.
	EPipeline Pipeline = EPipeline::Graphics;

	// The last used access on the pass.
	EAccess Access = EAccess::Unknown;
};

/** Render graph specific flags for resources. */
enum class ERDGResourceFlags : uint8
{
	None = 0,

	// Tag the resource to survive through frame, that is important for multi GPU alternate frame rendering.
	MultiFrame = 1 << 1,
};

ENUM_CLASS_FLAGS(ERDGResourceFlags);

/** Generic graph resource. */
class RENDERCORE_API FRDGResource
{
public:
	FRDGResource(const FRDGResource&) = delete;

	// Name of the resource for debugging purpose.
	const TCHAR* const Name = nullptr;

	//////////////////////////////////////////////////////////////////////////
	//! The following methods may only be called during pass execution.

	/** Marks this resource as actually used by a resource. This is to track what dependencies on pass was actually unnecessary. */
	void MarkResourceAsUsed()
	{
		ValidateRHIAccess();

		#if RDG_ENABLE_DEBUG
		{
			bIsActuallyUsedByPass = true;
		}
		#endif
	}

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

	/** Verify that the RHI resource can be accessed at a pass execution. */
	void ValidateRHIAccess() const
	{
#if RDG_ENABLE_DEBUG
		{
			checkf(bAllowRHIAccess,
				TEXT("Accessing the RHI resource of %s at this time is not allowed. If you hit this check in pass, ")
				TEXT("that is due to this resource not being referenced in the parameters of your pass."),
				Name);
		}
#endif
	}

	FRHIResource* GetRHIUnchecked() const
	{
		return ResourceRHI;
	}

private:
	FRHIResource* ResourceRHI = nullptr;

#if RDG_ENABLE_DEBUG
	/** Boolean to track at runtime whether a resource is actually used by the lambda of a pass or not, to detect unnecessary resource dependencies on passes. */
	bool bIsActuallyUsedByPass = false;

	/** Boolean to track at pass execution whether the underlying RHI resource is allowed to be accessed. */
	bool bAllowRHIAccess = false;
#endif

	friend class FRDGBuilder;
	friend class FRDGUserValidation;
};

/** A render graph resource with an allocation lifetime tracked by the graph. */
class RENDERCORE_API FRDGTrackedResource
	: public FRDGResource
{
public:
	/** Flags specific to this resource for render graph. */
	const ERDGResourceFlags Flags;

protected:
	FRDGTrackedResource(const TCHAR* InName, const ERDGResourceFlags InFlags)
		: FRDGResource(InName)
		, Flags(InFlags)
	{}

private:
	/** Number of references in passes and deferred queries. */
	int32 ReferenceCount = 0;

#if RDG_ENABLE_DEBUG
	void MarkAsProducedBy(const FRDGPass* Pass)
	{
		if (!bHasBeenProduced)
		{
			bHasBeenProduced = true;
			FirstProducer = Pass;
		}
	}

	void MarkAsExternal()
	{
		checkf(!FirstProducer, TEXT("Resource %s with producer pass marked as external."), Name);
		bHasBeenProduced = true;
		bIsExternal = true;
	}

	bool HasBeenProduced() const
	{
		return bHasBeenProduced;
	}

	bool IsExternal() const
	{
		return bIsExternal;
	}

	/** Pointer towards the pass that is the first to produce it, for even more convenient error message. */
	const FRDGPass* FirstProducer = nullptr;

	/** Count the number of times it has been used by a pass. */
	int32 PassAccessCount = 0;

	/** Boolean to track at wiring time if a resource has ever been produced by a pass, to error out early if accessing a resource that has not been produced. */
	bool bHasBeenProduced = false;

	/** Tracks whether this resource is an externally imported resource. */
	bool bIsExternal = false;
#endif

	FRDGResourceState State;

	friend class FRDGBuilder;
	friend class FRDGUserValidation;
	friend class FRDGBarrierBatcher;
};

/** Descriptor of a graph tracked texture. */
using FRDGTextureDesc = FPooledRenderTargetDesc;

/** Render graph tracked Texture. */
class RENDERCORE_API FRDGTexture final
	: public FRDGTrackedResource
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
		check(PooledRenderTarget);
		return PooledRenderTarget;
	}

	/** Returns the allocated RHI texture. */
	FRHITexture* GetRHI() const
	{
		return static_cast<FRHITexture*>(FRDGResource::GetRHI());
	}

	//////////////////////////////////////////////////////////////////////////

private:
	FRDGTexture(
		const TCHAR* InName,
		const FPooledRenderTargetDesc& InDesc,
		ERDGResourceFlags InFlags)
		: FRDGTrackedResource(InName, InFlags)
		, Desc(InDesc)
	{}

	/** Returns the allocated RHI texture without access checks. */
	FRHITexture* GetRHIUnchecked() const
	{
		return static_cast<FRHITexture*>(FRDGResource::GetRHIUnchecked());
	}

	/** This is not a TRefCountPtr<> because FRDGTexture is allocated on the FMemStack
	 *  FGraphBuilder::AllocatedTextures is actually keeping the reference.
	 */
	IPooledRenderTarget* PooledRenderTarget = nullptr;

	friend class FRDGBuilder;
	friend class FRDGUserValidation;
	friend class FRDGBarrierBatcher;
};

/** Render graph tracked SRV. */
class FRDGShaderResourceView
	: public FRDGResource
{
public:
	/** Returns the allocated RHI SRV. */
	FRHIShaderResourceView* GetRHI() const
	{
		return static_cast<FRHIShaderResourceView*>(FRDGResource::GetRHI());
	}

protected:
	FRDGShaderResourceView(const TCHAR* Name)
		: FRDGResource(Name)
	{}

	/** Returns the allocated RHI SRV without access checks. */
	FRHIShaderResourceView* GetRHIUnchecked() const
	{
		return static_cast<FRHIShaderResourceView*>(FRDGResource::GetRHIUnchecked());
	}
};

/** Render graph tracked UAV. */
class FRDGUnorderedAccessView
	: public FRDGResource
{
public:
	/** Returns the allocated RHI UAV. */
	FRHIUnorderedAccessView* GetRHI() const
	{
		return static_cast<FRHIUnorderedAccessView*>(FRDGResource::GetRHI());
	}

protected:
	FRDGUnorderedAccessView(const TCHAR* Name)
		: FRDGResource(Name)
	{}

	/** Returns the allocated RHI UAV without access checks. */
	FRHIUnorderedAccessView* GetRHIUnchecked() const
	{
		return static_cast<FRHIUnorderedAccessView*>(FRDGResource::GetRHIUnchecked());
	}
};

/** Descriptor for render graph tracked SRV. */
class RENDERCORE_API FRDGTextureSRVDesc final
{
public:
	FRDGTextureSRVDesc() = default;

	FRDGTextureSRVDesc(FRDGTextureRef InTexture, uint8 InMipLevel)
		: Texture(InTexture)
		, MipLevel(InMipLevel)
	{}

	FRDGTextureRef Texture = nullptr;
	uint8 MipLevel = 0;
};

/** Render graph tracked SRV. */
class RENDERCORE_API FRDGTextureSRV final
	: public FRDGShaderResourceView
{
public:
	/** Descriptor of the graph tracked SRV. */
	const FRDGTextureSRVDesc Desc;

private:
	FRDGTextureSRV(
		const TCHAR* InName,
		const FRDGTextureSRVDesc& InDesc)
		: FRDGShaderResourceView(InName)
		, Desc(InDesc)
	{}

	friend class FRDGBuilder;
};

/** Descriptor for render graph tracked UAV. */
class RENDERCORE_API FRDGTextureUAVDesc
{
public:
	FRDGTextureUAVDesc() = default;

	FRDGTextureUAVDesc(
		FRDGTextureRef InTexture,
		uint8 InMipLevel = 0)
		: Texture(InTexture)
		, MipLevel(InMipLevel)
	{}

	FRDGTextureRef Texture = nullptr;
	uint8 MipLevel = 0;
};

/** Render graph tracked texture UAV. */
class RENDERCORE_API FRDGTextureUAV final
	: public FRDGUnorderedAccessView
{
public:
	/** Descriptor of the graph tracked UAV. */
	const FRDGTextureUAVDesc Desc;

private:
	FRDGTextureUAV(
		const TCHAR* InName,
		const FRDGTextureUAVDesc& InDesc)
		: FRDGUnorderedAccessView(InName)
		, Desc(InDesc)
	{}

	friend class FRDGBuilder;
};

/** Descriptor for render graph tracked Buffer. */
struct RENDERCORE_API FRDGBufferDesc
{
	// Type of buffers to the RHI
	// TODO(RDG): refactor RHI to only have one FRHIBuffer.
	enum class EUnderlyingType
	{
		VertexBuffer,
		IndexBuffer, // not implemented yet.
		StructuredBuffer,
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

struct RENDERCORE_API FRDGBufferSRVDesc
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

struct RENDERCORE_API FRDGBufferUAVDesc
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

/** Render graph tracked Buffer. Only as opose to vertex, index and structured buffer at RHI level, because there is already
 * plans to refactor them. */
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

	RENDERCORE_API uint32 Release()
	{
		RefCount--;

		if (RefCount == 0)
		{
			VertexBuffer.SafeRelease();
			IndexBuffer.SafeRelease();
			StructuredBuffer.SafeRelease();
			UAVs.Empty();
			SRVs.Empty();
		}

		return RefCount;
	}

	inline uint32 GetRefCount()
	{
		return RefCount;
	}

private:
	const TCHAR* Name = nullptr;
	uint32 RefCount = 0;
	uint32 LastUsedFrame = 0;

	friend class FRenderGraphResourcePool;
};

/** Render graph tracked buffers. */
class RENDERCORE_API FRDGBuffer final
	: public FRDGTrackedResource
{
public:
	/** Descriptor of the graph. */
	const FRDGBufferDesc Desc;

	//////////////////////////////////////////////////////////////////////////
	//! The following methods may only be called during pass execution.

	/** Returns the buffer to use for indirect RHI calls. */
	FRHIVertexBuffer* GetIndirectRHICallBuffer() const
	{
		ValidateRHIAccess();
		check(PooledBuffer);
		checkf(Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer, TEXT("Indirect buffers needs to be underlying vertex buffer."));
		checkf(Desc.Usage & BUF_DrawIndirect, TEXT("The buffer %s was not flagged for indirect draw call"), Name);
		check(PooledBuffer->VertexBuffer.IsValid());
		return PooledBuffer->VertexBuffer;
	}

	//////////////////////////////////////////////////////////////////////////

private:
	FRDGBuffer(
		const TCHAR* InName,
		const FRDGBufferDesc& InDesc,
		ERDGResourceFlags InFlags)
		: FRDGTrackedResource(InName, InFlags)
		, Desc(InDesc)
	{}

	/** This is not a TRefCountPtr<> because FRDGBuffer is allocated on the FMemStack
	 *  FGraphBuilder::AllocatedBuffers is actually keeping the reference.
	 */
	FPooledRDGBuffer* PooledBuffer = nullptr;
	
	friend class FRDGBuilder;
};

/** Render graph tracked buffer SRV. */
class RENDERCORE_API FRDGBufferSRV final
	: public FRDGShaderResourceView
{
public:
	/** Descriptor of the graph tracked SRV. */
	const FRDGBufferSRVDesc Desc;

private:
	FRDGBufferSRV(const TCHAR* InName, const FRDGBufferSRVDesc& InDesc)
		: FRDGShaderResourceView(InName)
		, Desc(InDesc)
	{}

	friend class FRDGBuilder;
};

/** Render graph tracked buffer UAV. */
class RENDERCORE_API FRDGBufferUAV final
	: public FRDGUnorderedAccessView
{
public:
	/** Descriptor of the graph tracked UAV. */
	const FRDGBufferUAVDesc Desc;

private:
	FRDGBufferUAV(const TCHAR* InName, const FRDGBufferUAVDesc& InDesc)
		: FRDGUnorderedAccessView(InName)
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