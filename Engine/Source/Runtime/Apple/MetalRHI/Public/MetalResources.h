// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalResources.h: Metal resource RHI definitions..
=============================================================================*/

#pragma once

#include "BoundShaderStateCache.h"
#include "MetalShaderResources.h"
#include "ShaderCodeArchive.h"

THIRD_PARTY_INCLUDES_START
#include "mtlpp.hpp"
THIRD_PARTY_INCLUDES_END

/** Parallel execution is available on Mac but not iOS for the moment - it needs to be tested because it isn't cost-free */
#define METAL_SUPPORTS_PARALLEL_RHI_EXECUTE 1

class FMetalContext;
@class FMetalShaderPipeline;

extern NSString* DecodeMetalSourceCode(uint32 CodeSize, TArray<uint8> const& CompressedSource);

enum EMetalIndexType
{
	EMetalIndexType_None   = 0,
	EMetalIndexType_UInt16 = 1,
	EMetalIndexType_UInt32 = 2,
	EMetalIndexType_Num	   = 3
};


struct FMetalRenderPipelineHash
{
	friend uint32 GetTypeHash(FMetalRenderPipelineHash const& Hash)
	{
		return HashCombine(GetTypeHash(Hash.RasterBits), GetTypeHash(Hash.TargetBits));
	}
	
	friend bool operator==(FMetalRenderPipelineHash const& Left, FMetalRenderPipelineHash const& Right)
	{
		return Left.RasterBits == Right.RasterBits && Left.TargetBits == Right.TargetBits;
	}
	
	uint64 RasterBits;
	uint64 TargetBits;
};

class FMetalSubBufferHeap;
class FMetalSubBufferLinear;
class FMetalSubBufferMagazine;

class FMetalBuffer : public mtlpp::Buffer
{
public:
	FMetalBuffer(ns::Ownership retain = ns::Ownership::Retain) : mtlpp::Buffer(retain), Heap(nullptr), Linear(nullptr), Magazine(nullptr), bPooled(false) { }
	FMetalBuffer(ns::Protocol<id<MTLBuffer>>::type handle, ns::Ownership retain = ns::Ownership::Retain);
	
	FMetalBuffer(mtlpp::Buffer&& rhs, FMetalSubBufferHeap* heap);
	FMetalBuffer(mtlpp::Buffer&& rhs, FMetalSubBufferLinear* heap);
	FMetalBuffer(mtlpp::Buffer&& rhs, FMetalSubBufferMagazine* magazine);
	FMetalBuffer(mtlpp::Buffer&& rhs, bool bInPooled);
	
	FMetalBuffer(const FMetalBuffer& rhs);
	FMetalBuffer(FMetalBuffer&& rhs);
	virtual ~FMetalBuffer();
	
	FMetalBuffer& operator=(const FMetalBuffer& rhs);
	FMetalBuffer& operator=(FMetalBuffer&& rhs);
	
	inline bool operator==(FMetalBuffer const& rhs) const
	{
		return mtlpp::Buffer::operator==(rhs);
	}
	
	inline bool IsPooled() const { return bPooled; }
	inline bool IsSingleUse() const { return bSingleUse; }
	inline void MarkSingleUse() { bSingleUse = true; }
    void SetOwner(class FMetalRHIBuffer* Owner, bool bIsSwap);
	void Release();
	
	friend uint32 GetTypeHash(FMetalBuffer const& Hash)
	{
		return HashCombine(GetTypeHash(Hash.GetPtr()), GetTypeHash((uint64)Hash.GetOffset()));
	}
	
private:
	FMetalSubBufferHeap* Heap;
	FMetalSubBufferLinear* Linear;
	FMetalSubBufferMagazine* Magazine;
	bool bPooled;
	bool bSingleUse;
};

class FMetalTexture : public mtlpp::Texture
{
public:
	FMetalTexture(ns::Ownership retain = ns::Ownership::Retain) : mtlpp::Texture(retain) { }
	FMetalTexture(ns::Protocol<id<MTLTexture>>::type handle, ns::Ownership retain = ns::Ownership::Retain)
	: mtlpp::Texture(handle, nullptr, retain) {}
	
	FMetalTexture(mtlpp::Texture&& rhs)
	: mtlpp::Texture((mtlpp::Texture&&)rhs)
	{
		
	}
	
	FMetalTexture(const FMetalTexture& rhs)
	: mtlpp::Texture(rhs)
	{
		
	}
	
	FMetalTexture(FMetalTexture&& rhs)
	: mtlpp::Texture((mtlpp::Texture&&)rhs)
	{
		
	}
	
	FMetalTexture& operator=(const FMetalTexture& rhs)
	{
		if (this != &rhs)
		{
			mtlpp::Texture::operator=(rhs);
		}
		return *this;
	}
	
	FMetalTexture& operator=(FMetalTexture&& rhs)
	{
		mtlpp::Texture::operator=((mtlpp::Texture&&)rhs);
		return *this;
	}
	
	inline bool operator==(FMetalTexture const& rhs) const
	{
		return mtlpp::Texture::operator==(rhs);
	}
	
	friend uint32 GetTypeHash(FMetalTexture const& Hash)
	{
		return GetTypeHash(Hash.GetPtr());
	}
};

/** Texture/RT wrapper. */
class METALRHI_API FMetalSurface
{
public:

	/** 
	 * Constructor that will create Texture and Color/DepthBuffers as needed
	 */
	FMetalSurface(ERHIResourceType ResourceType, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 NumSamples, bool bArray, uint32 ArraySize, uint32 NumMips, ETextureCreateFlags Flags, FResourceBulkDataInterface* BulkData);

	FMetalSurface(FMetalSurface& Source, NSRange MipRange);
	
	FMetalSurface(FMetalSurface& Source, NSRange MipRange, EPixelFormat Format, bool bSRGBForceDisable);
	
	/**
	 * Destructor
	 */
	~FMetalSurface();

	/** Prepare for texture-view support - need only call this once on the source texture which is to be viewed. */
	void PrepareTextureView();
	
	/** @returns A newly allocated buffer object large enough for the surface within the texture specified. */
	id <MTLBuffer> AllocSurface(uint32 MipIndex, uint32 ArrayIndex, EResourceLockMode LockMode, uint32& DestStride, bool SingleLayer = false);

	/** Apply the data in Buffer to the surface specified.
	 * Will also handle destroying SourceBuffer appropriately.
	 */
	void UpdateSurfaceAndDestroySourceBuffer(id <MTLBuffer> SourceBuffer, uint32 MipIndex, uint32 ArrayIndex);
	
	/**
	 * Locks one of the texture's mip-maps.
	 * @param ArrayIndex Index of the texture array/face in the form Index*6+Face
	 * @return A pointer to the specified texture data.
	 */
	void* Lock(uint32 MipIndex, uint32 ArrayIndex, EResourceLockMode LockMode, uint32& DestStride, bool SingleLayer = false);
	
	/** Unlocks a previously locked mip-map.
	 * @param ArrayIndex Index of the texture array/face in the form Index*6+Face
	 */
	void Unlock(uint32 MipIndex, uint32 ArrayIndex, bool bTryAsync);
	
	/**
	 * Locks one of the texture's mip-maps.
	 * @param ArrayIndex Index of the texture array/face in the form Index*6+Face
	 * @return A pointer to the specified texture data.
	 */
	void* AsyncLock(class FRHICommandListImmediate& RHICmdList, uint32 MipIndex, uint32 ArrayIndex, EResourceLockMode LockMode, uint32& DestStride, bool bNeedsDefaultRHIFlush);
	
	/** Unlocks a previously locked mip-map.
	 * @param ArrayIndex Index of the texture array/face in the form Index*6+Face
	 */
	void AsyncUnlock(id <MTLBuffer> SourceData, uint32 MipIndex, uint32 ArrayIndex);

	/**
	 * Returns how much memory a single mip uses, and optionally returns the stride
	 */
	uint32 GetMipSize(uint32 MipIndex, uint32* Stride, bool bSingleLayer);

	/**
	 * Returns how much memory is used by the surface
	 */
	uint32 GetMemorySize();

	/** Returns the number of faces for the texture */
	uint32 GetNumFaces();
	
	/** Gets the drawable texture if this is a back-buffer surface. */
	FMetalTexture GetDrawableTexture();
	ns::AutoReleased<FMetalTexture> GetCurrentTexture();

	FMetalTexture Reallocate(FMetalTexture Texture, mtlpp::TextureUsage UsageModifier);
	void ReplaceTexture(FMetalContext& Context, FMetalTexture OldTexture, FMetalTexture NewTexture);
	void MakeAliasable(void);
	void MakeUnAliasable(void);
	
	ERHIResourceType Type;
	EPixelFormat PixelFormat;
	uint8 FormatKey;
	//texture used for store actions and binding to shader params
	FMetalTexture Texture;
	//if surface is MSAA, texture used to bind for RT
	FMetalTexture MSAATexture;

	//texture used for a resolve target.  Same as texture on iOS.  
	//Dummy target on Mac where RHISupportsSeparateMSAAAndResolveTextures is true.	In this case we don't always want a resolve texture but we
	//have to have one until renderpasses are implemented at a high level.
	// Mac / RHISupportsSeparateMSAAAndResolveTextures == true
	// iOS A9+ where depth resolve is available
	// iOS < A9 where depth resolve is unavailable.
	FMetalTexture MSAAResolveTexture;
	uint32 SizeX, SizeY, SizeZ;
	bool bIsCubemap;
	int16 volatile Written;
	int16 GPUReadback = 0;
	enum EMetalGPUReadbackFlags : int16
	{
		ReadbackRequestedShift 			= 0,
		ReadbackFenceCompleteShift  	= 1,
		ReadbackRequested 				= 1 << ReadbackRequestedShift,
		ReadbackFenceComplete 			= 1 << ReadbackFenceCompleteShift,
		ReadbackRequestedAndComplete 	= ReadbackRequested | ReadbackFenceComplete
	};
	
	ETextureCreateFlags Flags;

	uint32 BufferLocks;

	// how much memory is allocated for this texture
	uint64 TotalTextureSize;
	
	// For back-buffers, the owning viewport.
	class FMetalViewport* Viewport;
	
	TSet<class FMetalShaderResourceView*> SRVs;

private:
	void Init(FMetalSurface& Source, NSRange MipRange);
	
	void Init(FMetalSurface& Source, NSRange MipRange, EPixelFormat Format, bool bSRGBForceDisable);
	
private:
	// The movie playback IOSurface/CVTexture wrapper to avoid page-off
	CFTypeRef ImageSurfaceRef;
	
	// Texture view surfaces don't own their resources, only reference
	bool bTextureView;
	
	// Count of outstanding async. texture uploads
	static volatile int64 ActiveUploads;
};

class FMetalTexture2D : public FRHITexture2D
{
public:
	/** The surface info */
	FMetalSurface Surface;

	// Constructor, just calls base and Surface constructor
	FMetalTexture2D(EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue)
		: FRHITexture2D(SizeX, SizeY, NumMips, NumSamples, Format, Flags, InClearValue)
		, Surface(RRT_Texture2D, Format, SizeX, SizeY, 1, NumSamples, /*bArray=*/ false, 1, NumMips, Flags, BulkData)
	{
	}
	
	virtual ~FMetalTexture2D()
	{
	}
	
	virtual void* GetTextureBaseRHI() override final
	{
		return &Surface;
	}
	
	virtual void* GetNativeResource() const override final
	{
		return Surface.Texture;
	}
};

class FMetalTexture2DArray : public FRHITexture2DArray
{
public:
	/** The surface info */
	FMetalSurface Surface;

	// Constructor, just calls base and Surface constructor
	FMetalTexture2DArray(EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, ETextureCreateFlags Flags, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue)
		: FRHITexture2DArray(SizeX, SizeY, ArraySize, NumMips, 1, Format, Flags, InClearValue)
		, Surface(RRT_Texture2DArray, Format, SizeX, SizeY, 1, /*NumSamples=*/1, /*bArray=*/ true, ArraySize, NumMips, Flags, BulkData)
	{
	}
	
	virtual ~FMetalTexture2DArray()
	{
	}
	
	virtual void* GetTextureBaseRHI() override final
	{
		return &Surface;
	}
};

class FMetalTexture3D : public FRHITexture3D
{
public:
	/** The surface info */
	FMetalSurface Surface;

	// Constructor, just calls base and Surface constructor
	FMetalTexture3D(EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 NumMips, ETextureCreateFlags Flags, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue)
		: FRHITexture3D(SizeX, SizeY, SizeZ, NumMips, Format, Flags, InClearValue)
		, Surface(RRT_Texture3D, Format, SizeX, SizeY, SizeZ, /*NumSamples=*/1, /*bArray=*/ false, 1, NumMips, Flags, BulkData)
	{
	}
	
	virtual ~FMetalTexture3D()
	{
	}
	
	virtual void* GetTextureBaseRHI() override final
	{
		return &Surface;
	}
};

class FMetalTextureCube : public FRHITextureCube
{
public:
	/** The surface info */
	FMetalSurface Surface;

	// Constructor, just calls base and Surface constructor
	FMetalTextureCube(EPixelFormat Format, uint32 Size, bool bArray, uint32 ArraySize, uint32 NumMips, ETextureCreateFlags Flags, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue)
		: FRHITextureCube(Size, NumMips, Format, Flags, InClearValue)
		, Surface(RRT_TextureCube, Format, Size, Size, 6, /*NumSamples=*/1, bArray, ArraySize, NumMips, Flags, BulkData)
	{
	}
	
	virtual ~FMetalTextureCube()
	{
	}
	
	virtual void* GetTextureBaseRHI() override final
	{
		return &Surface;
	}

	virtual void* GetNativeResource() const override final
	{
		return Surface.Texture;
	}
};

@interface FMetalBufferData : FApplePlatformObject<NSObject>
{
@public
	uint8* Data;
	uint32 Len;	
}
-(instancetype)initWithSize:(uint32)Size;
-(instancetype)initWithBytes:(void const*)Data length:(uint32)Size;
@end

enum EMetalBufferUsage
{
	EMetalBufferUsage_GPUOnly = 0x80000000,
	EMetalBufferUsage_LinearTex = 0x40000000,
};

class FMetalLinearTextureDescriptor
{
public:
	FMetalLinearTextureDescriptor()
		: StartOffsetBytes(0)
		, NumElements(UINT_MAX)
		, BytesPerElement(0)
	{
		// void
	}

	FMetalLinearTextureDescriptor(uint32 InStartOffsetBytes, uint32 InNumElements, uint32 InBytesPerElement)
		: StartOffsetBytes(InStartOffsetBytes)
		, NumElements(InNumElements)
		, BytesPerElement(InBytesPerElement)
	{
		// void
	}
	
	FMetalLinearTextureDescriptor(const FMetalLinearTextureDescriptor& Other)
		: StartOffsetBytes(Other.StartOffsetBytes)
		, NumElements(Other.NumElements)
		, BytesPerElement(Other.BytesPerElement)
	{
		// void
	}
	
	~FMetalLinearTextureDescriptor()
	{
		// void
	}

	friend uint32 GetTypeHash(FMetalLinearTextureDescriptor const& Key)
	{
		uint32 Hash = GetTypeHash((uint64)Key.StartOffsetBytes);
		Hash = HashCombine(Hash, GetTypeHash((uint64)Key.NumElements));
		Hash = HashCombine(Hash, GetTypeHash((uint64)Key.BytesPerElement));
		return Hash;
	}

	bool operator==(FMetalLinearTextureDescriptor const& Other) const
	{
		return    StartOffsetBytes == Other.StartOffsetBytes
		       && NumElements      == Other.NumElements
		       && BytesPerElement  == Other.BytesPerElement;
	}

	uint32 StartOffsetBytes;
	uint32 NumElements;
	uint32 BytesPerElement;
};

class FMetalRHIBuffer
{
public:
	// Matches other RHIs
	static constexpr const uint32 MetalMaxNumBufferedFrames = 4;
	
	using LinearTextureMapKey = TTuple<EPixelFormat, FMetalLinearTextureDescriptor>;
	using LinearTextureMap = TMap<LinearTextureMapKey, FMetalTexture>;
	
	struct FMetalBufferAndViews
	{
		FMetalBuffer Buffer;
		LinearTextureMap Views;
	};
	
	FMetalRHIBuffer(uint32 InSize, uint32 InUsage, ERHIResourceType InType);
	virtual ~FMetalRHIBuffer();
	
	/**
	 * Initialize the buffer contents from the render-thread.
	 */
	void Init_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, FRHIResource* Resource);
	
	/**
	 * Get a linear texture for given format.
	 */
	void CreateLinearTexture(EPixelFormat InFormat, FRHIResource* InParent, const FMetalLinearTextureDescriptor* InLinearTextureDescriptor = nullptr);
	
	/**
	 * Get a linear texture for given format.
	 */
	ns::AutoReleased<FMetalTexture> GetLinearTexture(EPixelFormat InFormat, const FMetalLinearTextureDescriptor* InLinearTextureDescriptor = nullptr);
	
	/**
	 * Prepare a CPU accessible buffer for uploading to GPU memory
	 */
	void* Lock(bool bIsOnRHIThread, EResourceLockMode LockMode, uint32 Offset, uint32 Size=0);
	
	/**
	 * Prepare a CPU accessible buffer for uploading to GPU memory
	 */
	void Unlock();
	
	void Swap(FMetalRHIBuffer& Other);
	
	const FMetalBufferAndViews& GetCurrentBacking()
	{
		check(NumberOfBuffers > 0);
		return BufferPool[CurrentIndex];
	}
	
	const FMetalBuffer& GetCurrentBuffer()
	{
		return BufferPool[CurrentIndex].Buffer;
	}
	
	FMetalBuffer GetCurrentBufferOrNil()
	{
		if(NumberOfBuffers > 0)
		{
			return GetCurrentBuffer();
		}
		return nil;
	}
	
	void AdvanceBackingIndex()
	{
		CurrentIndex = (CurrentIndex + 1) % NumberOfBuffers;
	}
	
	/**
	 * Whether to allocate the resource from private memory.
	 */
	bool UsePrivateMemory() const;
	
	// A temporary shared/CPU accessible buffer for upload/download
	FMetalBuffer TransferBuffer;
	
	TArray<FMetalBufferAndViews> BufferPool;
	
	/** Buffer for small buffers < 4Kb to avoid heap fragmentation. */
	FMetalBufferData* Data;
	
	// Frame we last locked (for debugging, mainly)
	uint32 LastLockFrame;
	
	// The active buffer.
	uint32 CurrentIndex		: 8;
	// How many buffers are actually allocated
	uint32 NumberOfBuffers	: 8;
	// Current lock mode. RLM_Num indicates this buffer is not locked.
	uint32 CurrentLockMode	: 16;
	
	// offset into the buffer (for lock usage)
	uint32 LockOffset;
	
	// Sizeof outstanding lock.
	uint32 LockSize;
	
	// Initial buffer size.
	uint32 Size;
	
	// Buffer usage.
	uint32 Usage;
	
	// Storage mode
	mtlpp::StorageMode Mode;
	
	// Resource type
	ERHIResourceType Type;
	
	static_assert((1 << 16) > RLM_Num, "Lock mode does not fit in bitfield");
	static_assert((1 << 8) > MetalMaxNumBufferedFrames, "Buffer count does not fit in bitfield");
	
private:
	FMetalBufferAndViews& GetCurrentBackingInternal()
	{
		return BufferPool[CurrentIndex];
	}
	
	FMetalBuffer& GetCurrentBufferInternal()
	{
		return BufferPool[CurrentIndex].Buffer;
	}
	
	/**
	 * Allocate the CPU accessible buffer for data transfer.
	 */
	void AllocTransferBuffer(bool bOnRHIThread, uint32 InSize, EResourceLockMode LockMode);
	
	/**
	 * Allocate a linear texture for given format.
	 */
	void AllocLinearTextures(const LinearTextureMapKey& InLinearTextureMapKey);
};

/** Index buffer resource class that stores stride information. */
class FMetalIndexBuffer : public FRHIIndexBuffer, public FMetalRHIBuffer
{
public:
	
	/** Constructor */
	FMetalIndexBuffer(uint32 InStride, uint32 InSize, uint32 InUsage);
	virtual ~FMetalIndexBuffer();
	
	void Swap(FMetalIndexBuffer& Other);
	
	// 16- or 32-bit
	mtlpp::IndexType IndexType;
};

/** Vertex buffer resource class that stores usage type. */
class FMetalVertexBuffer : public FRHIVertexBuffer, public FMetalRHIBuffer
{
public:

	/** Constructor */
	FMetalVertexBuffer(uint32 InSize, uint32 InUsage);
	virtual ~FMetalVertexBuffer();

	void Swap(FMetalVertexBuffer& Other);
};

class FMetalStructuredBuffer : public FRHIStructuredBuffer, public FMetalRHIBuffer
{
public:
	// Constructor
	FMetalStructuredBuffer(uint32 Stride, uint32 Size, FResourceArrayInterface* ResourceArray, uint32 InUsage);

	// Destructor
	~FMetalStructuredBuffer();
};


class FMetalShaderResourceView : public FRHIShaderResourceView
{
public:

	// The vertex buffer this SRV comes from (can be null)
	TRefCountPtr<FMetalVertexBuffer> SourceVertexBuffer;
	
	// The index buffer this SRV comes from (can be null)
	TRefCountPtr<FMetalIndexBuffer> SourceIndexBuffer;

	// The texture that this SRV come from
	TRefCountPtr<FRHITexture> SourceTexture;
	
	// The source structured buffer (can be null)
	TRefCountPtr<FMetalStructuredBuffer> SourceStructuredBuffer;
	
	FMetalSurface* TextureView;
	uint32 Offset;
	uint8 MipLevel          : 4;
	uint8 bSRGBForceDisable : 1;
	uint8 Reserved          : 3;
	uint8 NumMips;
	uint8 Format;
	uint8 Stride;

	void InitLinearTextureDescriptor(const FMetalLinearTextureDescriptor& InLinearTextureDescriptor);

	FMetalShaderResourceView();
	~FMetalShaderResourceView();
	
	ns::AutoReleased<FMetalTexture> GetLinearTexture(bool const bUAV);

private:
	FMetalLinearTextureDescriptor* LinearTextureDesc;
};



class FMetalUnorderedAccessView : public FRHIUnorderedAccessView
{
public:
	
	// the potential resources to refer to with the UAV object
	TRefCountPtr<FMetalShaderResourceView> SourceView;
};

class FMetalGPUFence final : public FRHIGPUFence
{
public:
	FMetalGPUFence(FName InName)
		: FRHIGPUFence(InName)
	{
	}

	~FMetalGPUFence()
	{
	}

	virtual void Clear() override final;

	void WriteInternal(mtlpp::CommandBuffer& CmdBuffer);

	virtual bool Poll() const override final;

private:
	mtlpp::CommandBufferFence Fence;
};

class FMetalShaderLibrary;
class FMetalGraphicsPipelineState;
class FMetalComputePipelineState;
class FMetalVertexDeclaration;
class FMetalVertexShader;
class FMetalHullShader;
class FMetalDomainShader;
class FMetalGeometryShader;
class FMetalPixelShader;
class FMetalComputeShader;
class FMetalRHIStagingBuffer;
class FMetalRHIRenderQuery;
class FMetalSuballocatedUniformBuffer;

template<class T>
struct TMetalResourceTraits
{
};
template<>
struct TMetalResourceTraits<FRHIShaderLibrary>
{
	typedef FMetalShaderLibrary TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIVertexDeclaration>
{
	typedef FMetalVertexDeclaration TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIVertexShader>
{
	typedef FMetalVertexShader TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIGeometryShader>
{
	typedef FMetalGeometryShader TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIHullShader>
{
	typedef FMetalHullShader TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIDomainShader>
{
	typedef FMetalDomainShader TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIPixelShader>
{
	typedef FMetalPixelShader TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIComputeShader>
{
	typedef FMetalComputeShader TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHITexture3D>
{
	typedef FMetalTexture3D TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHITexture2D>
{
	typedef FMetalTexture2D TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHITexture2DArray>
{
	typedef FMetalTexture2DArray TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHITextureCube>
{
	typedef FMetalTextureCube TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIRenderQuery>
{
	typedef FMetalRHIRenderQuery TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIUniformBuffer>
{
	typedef FMetalSuballocatedUniformBuffer TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIIndexBuffer>
{
	typedef FMetalIndexBuffer TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIStructuredBuffer>
{
	typedef FMetalStructuredBuffer TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIVertexBuffer>
{
	typedef FMetalVertexBuffer TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIShaderResourceView>
{
	typedef FMetalShaderResourceView TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIUnorderedAccessView>
{
	typedef FMetalUnorderedAccessView TConcreteType;
};

template<>
struct TMetalResourceTraits<FRHISamplerState>
{
	typedef FMetalSamplerState TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIRasterizerState>
{
	typedef FMetalRasterizerState TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIDepthStencilState>
{
	typedef FMetalDepthStencilState TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIBlendState>
{
	typedef FMetalBlendState TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIGraphicsPipelineState>
{
	typedef FMetalGraphicsPipelineState TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIComputePipelineState>
{
	typedef FMetalComputePipelineState TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIGPUFence>
{
	typedef FMetalGPUFence TConcreteType;
};
template<>
struct TMetalResourceTraits<FRHIStagingBuffer>
{
	typedef FMetalRHIStagingBuffer TConcreteType;
};
