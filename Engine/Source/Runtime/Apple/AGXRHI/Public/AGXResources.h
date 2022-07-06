// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXResources.h: AGX resource RHI definitions..
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

class FAGXContext;
@class FAGXShaderPipeline;

extern NSString* AGXDecodeMetalSourceCode(uint32 CodeSize, TArray<uint8> const& CompressedSource);

struct FAGXRenderPipelineHash
{
	friend uint32 GetTypeHash(FAGXRenderPipelineHash const& Hash)
	{
		return HashCombine(GetTypeHash(Hash.RasterBits), GetTypeHash(Hash.TargetBits));
	}
	
	friend bool operator==(FAGXRenderPipelineHash const& Left, FAGXRenderPipelineHash const& Right)
	{
		return Left.RasterBits == Right.RasterBits && Left.TargetBits == Right.TargetBits;
	}
	
	uint64 RasterBits;
	uint64 TargetBits;
};

class FAGXSubBufferHeap;
class FAGXSubBufferLinear;
class FAGXSubBufferMagazine;

class FAGXBuffer : public mtlpp::Buffer
{
public:
	FAGXBuffer(ns::Ownership retain = ns::Ownership::Retain) : mtlpp::Buffer(retain), Heap(nullptr), Linear(nullptr), Magazine(nullptr), bPooled(false) { }
	FAGXBuffer(ns::Protocol<id<MTLBuffer>>::type handle, ns::Ownership retain = ns::Ownership::Retain);
	
	FAGXBuffer(mtlpp::Buffer&& rhs, FAGXSubBufferHeap* heap);
	FAGXBuffer(mtlpp::Buffer&& rhs, FAGXSubBufferLinear* heap);
	FAGXBuffer(mtlpp::Buffer&& rhs, FAGXSubBufferMagazine* magazine);
	FAGXBuffer(mtlpp::Buffer&& rhs, bool bInPooled);
	
	FAGXBuffer(const FAGXBuffer& rhs);
	FAGXBuffer(FAGXBuffer&& rhs);
	virtual ~FAGXBuffer();
	
	FAGXBuffer& operator=(const FAGXBuffer& rhs);
	FAGXBuffer& operator=(FAGXBuffer&& rhs);
	
	inline bool operator==(FAGXBuffer const& rhs) const
	{
		return mtlpp::Buffer::operator==(rhs);
	}
	
	inline bool IsPooled() const { return bPooled; }
	inline bool IsSingleUse() const { return bSingleUse; }
	inline void MarkSingleUse() { bSingleUse = true; }
    void SetOwner(class FAGXRHIBuffer* Owner, bool bIsSwap);
	void Release();
	
	friend uint32 GetTypeHash(FAGXBuffer const& Hash)
	{
		return HashCombine(GetTypeHash(Hash.GetPtr()), GetTypeHash((uint64)Hash.GetOffset()));
	}
	
private:
	FAGXSubBufferHeap* Heap;
	FAGXSubBufferLinear* Linear;
	FAGXSubBufferMagazine* Magazine;
	bool bPooled;
	bool bSingleUse;
};

class FAGXTexture : public mtlpp::Texture
{
public:
	FAGXTexture(ns::Ownership retain = ns::Ownership::Retain)
		: mtlpp::Texture(retain)
	{}

	FAGXTexture(ns::Protocol<id<MTLTexture>>::type handle, ns::Ownership retain = ns::Ownership::Retain)
		: mtlpp::Texture(handle, nullptr, retain)
	{}
	
	FAGXTexture(mtlpp::Texture&& rhs)
		: mtlpp::Texture((mtlpp::Texture&&)rhs)
	{}
	
	FAGXTexture(const FAGXTexture& rhs)
		: mtlpp::Texture(rhs)
	{}
	
	FAGXTexture(FAGXTexture&& rhs)
		: mtlpp::Texture((mtlpp::Texture&&)rhs)
	{}
	
	FAGXTexture& operator=(const FAGXTexture& rhs)
	{
		if (this != &rhs)
		{
			mtlpp::Texture::operator=(rhs);
		}
		return *this;
	}
	
	FAGXTexture& operator=(FAGXTexture&& rhs)
	{
		mtlpp::Texture::operator=((mtlpp::Texture&&)rhs);
		return *this;
	}
	
	inline bool operator==(FAGXTexture const& rhs) const
	{
		return mtlpp::Texture::operator==(rhs);
	}
	
	friend uint32 GetTypeHash(FAGXTexture const& Hash)
	{
		return GetTypeHash(Hash.GetPtr());
	}
};

// AGX RHI texture resource
class AGXRHI_API FAGXSurface : public FRHITexture
{
public:

	/** 
	 * Constructor that will create Texture and Color/DepthBuffers as needed
	 */
	FAGXSurface(const struct FAGXTextureCreateDesc& CreateDesc);
	
	/**
	 * Destructor
	 */
	virtual ~FAGXSurface();

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
	void GetDrawableTexture();
	id<MTLTexture> GetCurrentTexture();

	FAGXTexture Reallocate(FAGXTexture Texture, MTLTextureUsage UsageModifier);
	void MakeAliasable(void);

	uint8 const FormatKey;
	//texture used for store actions and binding to shader params
	FAGXTexture Texture;
	//if surface is MSAA, texture used to bind for RT
	FAGXTexture MSAATexture;

	//texture used for a resolve target.  Same as texture on iOS.  
	//Dummy target on Mac where RHISupportsSeparateMSAAAndResolveTextures is true.	In this case we don't always want a resolve texture but we
	//have to have one until renderpasses are implemented at a high level.
	// Mac / RHISupportsSeparateMSAAAndResolveTextures == true
	// iOS A9+ where depth resolve is available
	// iOS < A9 where depth resolve is unavailable.
	FAGXTexture MSAAResolveTexture;

	int16 volatile Written;
	int16 GPUReadback = 0;
	enum EAGXGPUReadbackFlags : int16
	{
		ReadbackRequestedShift 			= 0,
		ReadbackFenceCompleteShift  	= 1,
		ReadbackRequested 				= 1 << ReadbackRequestedShift,
		ReadbackFenceComplete 			= 1 << ReadbackFenceCompleteShift,
		ReadbackRequestedAndComplete 	= ReadbackRequested | ReadbackFenceComplete
	};

	// how much memory is allocated for this texture
	uint64 TotalTextureSize;
	
	// For back-buffers, the owning viewport.
	class FAGXViewport* Viewport;

	virtual void* GetTextureBaseRHI() override final
	{
		return this;
	}

	virtual void* GetNativeResource() const override final
	{
		return Texture;
	}
	
private:
	// The movie playback IOSurface/CVTexture wrapper to avoid page-off
	CFTypeRef ImageSurfaceRef;

	// Count of outstanding async. texture uploads
	static volatile int64 ActiveUploads;
};

@interface FAGXBufferData : FApplePlatformObject<NSObject>
{
@public
	uint8* Data;
	uint32 Len;	
}
-(instancetype)initWithSize:(uint32)Size;
-(instancetype)initWithBytes:(void const*)Data length:(uint32)Size;
@end

enum class EAGXBufferUsage
{
	None = 0,
	GPUOnly = 1 << 0,
	LinearTex = 1 << 1,
};
ENUM_CLASS_FLAGS(EAGXBufferUsage);

class FAGXLinearTextureDescriptor
{
public:
	FAGXLinearTextureDescriptor() = default;

	FAGXLinearTextureDescriptor(uint32 InStartOffsetBytes, uint32 InNumElements, uint32 InBytesPerElement)
		: StartOffsetBytes(InStartOffsetBytes)
		, NumElements     (InNumElements)
		, BytesPerElement (InBytesPerElement)
	{}

	friend uint32 GetTypeHash(FAGXLinearTextureDescriptor const& Key)
	{
		uint32 Hash = GetTypeHash((uint64)Key.StartOffsetBytes);
		Hash = HashCombine(Hash, GetTypeHash((uint64)Key.NumElements));
		Hash = HashCombine(Hash, GetTypeHash((uint64)Key.BytesPerElement));
		return Hash;
	}

	bool operator==(FAGXLinearTextureDescriptor const& Other) const
	{
		return    StartOffsetBytes == Other.StartOffsetBytes
		       && NumElements      == Other.NumElements
		       && BytesPerElement  == Other.BytesPerElement;
	}

	uint32 StartOffsetBytes = 0;
	uint32 NumElements      = UINT_MAX;
	uint32 BytesPerElement  = 0;
};

class FAGXRHIBuffer
{
public:
	// Matches other RHIs
	static constexpr const uint32 MaxNumBufferedFrames = 4;
	
	using LinearTextureMapKey = TTuple<EPixelFormat, FAGXLinearTextureDescriptor>;
	using LinearTextureMap = TMap<LinearTextureMapKey, FAGXTexture>;
	
	struct FAGXBufferAndViews
	{
		FAGXBuffer Buffer;
		LinearTextureMap Views;
	};
	
	FAGXRHIBuffer(uint32 InSize, EBufferUsageFlags InUsage, EAGXBufferUsage InAgxUsage, ERHIResourceType InType);
	virtual ~FAGXRHIBuffer();
	
	/**
	 * Initialize the buffer contents from the render-thread.
	 */
	void Init(class FRHICommandListBase& RHICmdList, uint32 Size, EBufferUsageFlags InUsage, FRHIResourceCreateInfo& CreateInfo, FRHIResource* Resource);
	
	/**
	 * Get a linear texture for given format.
	 */
	void CreateLinearTexture(EPixelFormat InFormat, FRHIResource* InParent, const FAGXLinearTextureDescriptor* InLinearTextureDescriptor = nullptr);
	
	/**
	 * Get a linear texture for given format.
	 */
	ns::AutoReleased<FAGXTexture> GetLinearTexture(EPixelFormat InFormat, const FAGXLinearTextureDescriptor* InLinearTextureDescriptor = nullptr);
	
	/**
	 * Prepare a CPU accessible buffer for uploading to GPU memory
	 */
	void* Lock(bool bIsOnRHIThread, EResourceLockMode LockMode, uint32 Offset, uint32 Size=0);
	
	/**
	 * Prepare a CPU accessible buffer for uploading to GPU memory
	 */
	void Unlock();
	
	void Swap(FAGXRHIBuffer& Other);
	
	const FAGXBufferAndViews& GetCurrentBacking()
	{
		check(NumberOfBuffers > 0);
		return BufferPool[CurrentIndex];
	}
	
	const FAGXBuffer& GetCurrentBuffer()
	{
		return BufferPool[CurrentIndex].Buffer;
	}
	
	FAGXBuffer GetCurrentBufferOrNil()
	{
		if(NumberOfBuffers > 0)
		{
			return GetCurrentBuffer();
		}
		return nil;
	}
	
	EAGXBufferUsage GetAgxUsage() const
	{
		return AgxUsage;
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
	FAGXBuffer TransferBuffer;
	
	TArray<FAGXBufferAndViews> BufferPool;
	
	/** Buffer for small buffers < 4Kb to avoid heap fragmentation. */
	FAGXBufferData* Data;
	
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
	EBufferUsageFlags Usage;
	
	// Agx buffer usage.
	EAGXBufferUsage AgxUsage;
	
	// Storage mode
	MTLStorageMode StorageMode;
	
	// Resource type
	ERHIResourceType Type;
	
	static_assert((1 << 16) > RLM_Num, "Lock mode does not fit in bitfield");
	static_assert((1 << 8) > MaxNumBufferedFrames, "Buffer count does not fit in bitfield");
	
private:
	FAGXBufferAndViews& GetCurrentBackingInternal()
	{
		return BufferPool[CurrentIndex];
	}
	
	FAGXBuffer& GetCurrentBufferInternal()
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

class FAGXResourceMultiBuffer : public FRHIBuffer, public FAGXRHIBuffer
{
public:
	FAGXResourceMultiBuffer(uint32 InSize, EBufferUsageFlags InUsage, EAGXBufferUsage InAgxUsage, uint32 InStride, ERHIResourceType ResourceType);
	virtual ~FAGXResourceMultiBuffer();

	void Swap(FAGXResourceMultiBuffer& Other);

	// 16- or 32-bit; used for index buffers only.
	MTLIndexType IndexType;
};

typedef FAGXResourceMultiBuffer FAGXIndexBuffer;
typedef FAGXResourceMultiBuffer FAGXVertexBuffer;
typedef FAGXResourceMultiBuffer FAGXStructuredBuffer;

class FAGXResourceViewBase
{
protected:
	// Constructor for buffers
	FAGXResourceViewBase(
		  FRHIBuffer* InBuffer
		, uint32 InStartOffsetBytes
		, uint32 InNumElements
		, EPixelFormat InFormat
	);

	// Constructor for textures
	FAGXResourceViewBase(
		  FRHITexture* InTexture
		, EPixelFormat InFormat
		, uint8 InMipLevel
		, uint8 InNumMipLevels
		, ERHITextureSRVOverrideSRGBType InSRGBOverride
		, uint32 InFirstArraySlice
		, uint32 InNumArraySlices
		, bool bInUAV
	);

public:
	~FAGXResourceViewBase();

	inline FAGXResourceMultiBuffer* GetSourceBuffer () const { check(!bTexture); return SourceBuffer; }

	inline FAGXSurface*             GetSourceTexture() const { check(bTexture); return SourceTexture; }
	inline id<MTLTexture>           GetTextureView  () const { check(bTexture); return TextureView;   }

private:
	// Needed for RHIUpdateShaderResourceView
	friend class FAGXDynamicRHI;

	union
	{
		FAGXResourceMultiBuffer* SourceBuffer;
		FAGXSurface* SourceTexture;
	};

	TUniquePtr<FAGXLinearTextureDescriptor> LinearTextureDesc = nullptr;
	id<MTLTexture> TextureView = nil;

public:
	uint8 const bTexture : 1;
	uint8       bSRGBForceDisable : 1;
	uint8       MipLevel : 4;
	uint8       Reserved : 2;
	uint8       NumMips;
	uint8       Format;
	uint8       Stride;
	uint32      Offset;

	ns::AutoReleased<FAGXTexture> GetLinearTexture();
};

class FAGXShaderResourceView final : public FRHIShaderResourceView, public FAGXResourceViewBase
{
public:
	explicit FAGXShaderResourceView(const FShaderResourceViewInitializer& Initializer)
		: FRHIShaderResourceView(Initializer.AsBufferSRV().Buffer)
		, FAGXResourceViewBase(
			  Initializer.AsBufferSRV().Buffer
			, Initializer.AsBufferSRV().StartOffsetBytes
			, Initializer.AsBufferSRV().NumElements
			, Initializer.AsBufferSRV().Format
		)
	{}

	explicit FAGXShaderResourceView(FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo)
		: FRHIShaderResourceView(Texture)
		, FAGXResourceViewBase(
			  Texture
			, CreateInfo.Format
			, CreateInfo.MipLevel
			, CreateInfo.NumMipLevels
			, CreateInfo.SRGBOverride
			, CreateInfo.FirstArraySlice
			, CreateInfo.NumArraySlices
			, false // bInUAV
		)
	{}

	virtual ~FAGXShaderResourceView()
	{}
};

class FAGXUnorderedAccessView final : public FRHIUnorderedAccessView, public FAGXResourceViewBase
{
public:
	explicit FAGXUnorderedAccessView(FRHIBuffer* Buffer, EPixelFormat Format)
		: FRHIUnorderedAccessView(Buffer)
		, FAGXResourceViewBase(Buffer, 0, UINT_MAX, Format)
	{}

	explicit FAGXUnorderedAccessView(FRHIBuffer* Buffer, bool bUseUAVCounter, bool bAppendBuffer)
		: FRHIUnorderedAccessView(Buffer)
		, FAGXResourceViewBase(Buffer, 0, UINT_MAX, PF_Unknown)
	{
		checkf(!bUseUAVCounter, TEXT("UAV counters not implemented."));
		checkf(!bAppendBuffer, TEXT("UAV append buffers not implemented."));
	}

	explicit FAGXUnorderedAccessView(FRHITexture* Texture, uint32 MipLevel, uint16 FirstArraySlice, uint16 NumArraySlices)
		: FRHIUnorderedAccessView(Texture)
		, FAGXResourceViewBase(
			  Texture
			, PF_Unknown
			, MipLevel
			, 1 // NumMipLevels
			, ERHITextureSRVOverrideSRGBType::SRGBO_ForceDisable
			, FirstArraySlice
			, NumArraySlices
			, true // bInUAV
		)
	{}

	virtual ~FAGXUnorderedAccessView()
	{}
};

class FAGXGPUFence final : public FRHIGPUFence
{
public:
	FAGXGPUFence(FName InName)
		: FRHIGPUFence(InName)
	{
	}

	~FAGXGPUFence()
	{
	}

	virtual void Clear() override final;

	void WriteInternal(mtlpp::CommandBuffer& CmdBuffer);

	virtual bool Poll() const override final;

private:
	mtlpp::CommandBufferFence Fence;
};

class FAGXShaderLibrary;
class FAGXGraphicsPipelineState;
class FAGXComputePipelineState;
class FAGXVertexDeclaration;
class FAGXVertexShader;
class FAGXGeometryShader;
class FAGXPixelShader;
class FAGXComputeShader;
class FAGXRHIStagingBuffer;
class FAGXRHIRenderQuery;
class FAGXSuballocatedUniformBuffer;

template<class T>
struct TAGXResourceTraits
{
};
template<>
struct TAGXResourceTraits<FRHIShaderLibrary>
{
	typedef FAGXShaderLibrary TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIVertexDeclaration>
{
	typedef FAGXVertexDeclaration TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIVertexShader>
{
	typedef FAGXVertexShader TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIGeometryShader>
{
	typedef FAGXGeometryShader TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIPixelShader>
{
	typedef FAGXPixelShader TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIComputeShader>
{
	typedef FAGXComputeShader TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIRenderQuery>
{
	typedef FAGXRHIRenderQuery TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIUniformBuffer>
{
	typedef FAGXSuballocatedUniformBuffer TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIBuffer>
{
	typedef FAGXResourceMultiBuffer TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIShaderResourceView>
{
	typedef FAGXShaderResourceView TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIUnorderedAccessView>
{
	typedef FAGXUnorderedAccessView TConcreteType;
};

template<>
struct TAGXResourceTraits<FRHISamplerState>
{
	typedef FAGXSamplerState TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIRasterizerState>
{
	typedef FAGXRasterizerState TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIDepthStencilState>
{
	typedef FAGXDepthStencilState TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIBlendState>
{
	typedef FAGXBlendState TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIGraphicsPipelineState>
{
	typedef FAGXGraphicsPipelineState TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIComputePipelineState>
{
	typedef FAGXComputePipelineState TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIGPUFence>
{
	typedef FAGXGPUFence TConcreteType;
};
template<>
struct TAGXResourceTraits<FRHIStagingBuffer>
{
	typedef FAGXRHIStagingBuffer TConcreteType;
};
