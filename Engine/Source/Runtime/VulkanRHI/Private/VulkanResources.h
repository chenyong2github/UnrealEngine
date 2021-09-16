// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanResources.h: Vulkan resource RHI definitions.
=============================================================================*/

#pragma once

#include "VulkanConfiguration.h"
#include "VulkanState.h"
#include "VulkanUtil.h"
#include "BoundShaderStateCache.h"
#include "VulkanShaderResources.h"
#include "VulkanState.h"
#include "VulkanMemory.h"
#include "Misc/ScopeRWLock.h"

class FVulkanDevice;
class FVulkanQueue;
class FVulkanCmdBuffer;
class FVulkanBuffer;
class FVulkanBufferCPU;
struct FVulkanTextureBase;
class FVulkanTexture2D;
struct FVulkanBufferView;
class FVulkanResourceMultiBuffer;
class FVulkanLayout;
class FVulkanOcclusionQuery;
class FVulkanShaderResourceView;
class FVulkanCommandBufferManager;

namespace VulkanRHI
{
	class FDeviceMemoryAllocation;
	class FOldResourceAllocation;
	struct FPendingBufferLock;
	class FVulkanViewBase;
}

enum
{
	NUM_OCCLUSION_QUERIES_PER_POOL = 4096,

	NUM_TIMESTAMP_QUERIES_PER_POOL = 1024,
};

struct FSamplerYcbcrConversionInitializer
{
	VkFormat Format;
	uint64 ExternalFormat;
	VkComponentMapping Components;
	VkSamplerYcbcrModelConversion Model;
	VkSamplerYcbcrRange Range;
	VkChromaLocation XOffset;
	VkChromaLocation YOffset;
};

// Mirror GPixelFormats with format information for buffers
extern VkFormat GVulkanBufferFormat[PF_MAX];

/** This represents a vertex declaration that hasn't been combined with a specific shader to create a bound shader. */
class FVulkanVertexDeclaration : public FRHIVertexDeclaration
{
public:
	FVertexDeclarationElementList Elements;

	FVulkanVertexDeclaration(const FVertexDeclarationElementList& InElements);

	virtual bool GetInitializer(FVertexDeclarationElementList& Out) final override
	{
		Out = Elements;
		return true;
	}

	static void EmptyCache();
};

struct FGfxPipelineDesc;

class FVulkanShader : public IRefCountedObject
{
public:
	FVulkanShader(FVulkanDevice* InDevice, EShaderFrequency InFrequency, VkShaderStageFlagBits InStageFlag)
		: ShaderKey(0)
		, StageFlag(InStageFlag)
		, Frequency(InFrequency)
		, SpirvSize(0)
		, Device(InDevice)
	{
	}

	virtual ~FVulkanShader();

	void PurgeShaderModules();

	void Setup(TArrayView<const uint8> InShaderHeaderAndCode, uint64 InShaderKey);

	VkShaderModule GetOrCreateHandle(const FVulkanLayout* Layout, uint32 LayoutHash)
	{
		VkShaderModule* Found = ShaderModules.Find(LayoutHash);
		if (Found)
		{
			return *Found;
		}

		return CreateHandle(Layout, LayoutHash);
	}
	
	VkShaderModule GetOrCreateHandle(const FGfxPipelineDesc& Desc, const FVulkanLayout* Layout, uint32 LayoutHash)
	{
		if (NeedsSpirvInputAttachmentPatching(Desc))
		{
			LayoutHash = HashCombine(LayoutHash, 1);
		}
		
		VkShaderModule* Found = ShaderModules.Find(LayoutHash);
		if (Found)
		{
			return *Found;
		}

		return CreateHandle(Desc, Layout, LayoutHash);
	}

	inline const FString& GetDebugName() const
	{
		return CodeHeader.DebugName;
	}

	// Name should be pointing to "main_"
	void GetEntryPoint(ANSICHAR* Name, int32 NameLength)
	{
		FCStringAnsi::Snprintf(Name, NameLength, "main_%0.8x_%0.8x", SpirvSize, CodeHeader.SpirvCRC);
	}

	FORCEINLINE const FVulkanShaderHeader& GetCodeHeader() const
	{
		return CodeHeader;
	}

	inline uint64 GetShaderKey() const
	{
		return ShaderKey;
	}

protected:
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	FString							DebugEntryPoint;
#endif
	uint64							ShaderKey;

	/** External bindings for this shader. */
	FVulkanShaderHeader				CodeHeader;
	TMap<uint32, VkShaderModule>	ShaderModules;
	const VkShaderStageFlagBits		StageFlag;
	EShaderFrequency				Frequency;

	TArray<FUniformBufferStaticSlot> StaticSlots;

	TArray<uint32>					Spirv;
	// this is size of unmodified spriv code
	uint32							SpirvSize;

	FVulkanDevice*					Device;

	VkShaderModule CreateHandle(const FVulkanLayout* Layout, uint32 LayoutHash);
	VkShaderModule CreateHandle(const FGfxPipelineDesc& Desc, const FVulkanLayout* Layout, uint32 LayoutHash);
	
	bool NeedsSpirvInputAttachmentPatching(const FGfxPipelineDesc& Desc) const;

	friend class FVulkanCommandListContext;
	friend class FVulkanPipelineStateCacheManager;
	friend class FVulkanComputeShaderState;
	friend class FVulkanComputePipeline;
	friend class FVulkanShaderFactory;
};

/** This represents a vertex shader that hasn't been combined with a specific declaration to create a bound shader. */
template<typename BaseResourceType, EShaderFrequency ShaderType, VkShaderStageFlagBits StageFlagBits>
class TVulkanBaseShader : public BaseResourceType, public FVulkanShader
{
private:
	TVulkanBaseShader(FVulkanDevice* InDevice) :
		FVulkanShader(InDevice, ShaderType, StageFlagBits)
	{
	}
	friend class FVulkanShaderFactory;
public:
	enum { StaticFrequency = ShaderType };

	// IRefCountedObject interface.
	virtual uint32 AddRef() const override final
	{
		return FRHIResource::AddRef();
	}
	virtual uint32 Release() const override final
	{
		return FRHIResource::Release();
	}
	virtual uint32 GetRefCount() const override final
	{
		return FRHIResource::GetRefCount();
	}
};

typedef TVulkanBaseShader<FRHIVertexShader, SF_Vertex, VK_SHADER_STAGE_VERTEX_BIT>					FVulkanVertexShader;
typedef TVulkanBaseShader<FRHIPixelShader, SF_Pixel, VK_SHADER_STAGE_FRAGMENT_BIT>					FVulkanPixelShader;
typedef TVulkanBaseShader<FRHIHullShader, SF_Hull, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT>		FVulkanHullShader;
typedef TVulkanBaseShader<FRHIDomainShader, SF_Domain, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT>	FVulkanDomainShader;
typedef TVulkanBaseShader<FRHIComputeShader, SF_Compute, VK_SHADER_STAGE_COMPUTE_BIT>				FVulkanComputeShader;
typedef TVulkanBaseShader<FRHIGeometryShader, SF_Geometry, VK_SHADER_STAGE_GEOMETRY_BIT>			FVulkanGeometryShader;

class FVulkanShaderFactory
{
public:
	~FVulkanShaderFactory();

	template <typename ShaderType>
	ShaderType* CreateShader(TArrayView<const uint8> Code, FVulkanDevice* Device);

	template <typename ShaderType>
	ShaderType* LookupShader(uint64 ShaderKey) const
	{
		if (ShaderKey)
		{
			FRWScopeLock ScopedLock(Lock, SLT_ReadOnly);
			FVulkanShader* const * FoundShaderPtr = ShaderMap[ShaderType::StaticFrequency].Find(ShaderKey);
			if (FoundShaderPtr)
			{
				return static_cast<ShaderType*>(*FoundShaderPtr);
			}
		}
		return nullptr;
	}

	void LookupShaders(const uint64 InShaderKeys[ShaderStage::NumStages], FVulkanShader* OutShaders[ShaderStage::NumStages]) const;

	void OnDeleteShader(const FVulkanShader& Shader);

private:
	mutable FRWLock Lock;
	TMap<uint64, FVulkanShader*> ShaderMap[SF_NumFrequencies];
};

class FVulkanBoundShaderState : public FRHIBoundShaderState
{
public:
	FVulkanBoundShaderState(
		FRHIVertexDeclaration* InVertexDeclarationRHI,
		FRHIVertexShader* InVertexShaderRHI,
		FRHIPixelShader* InPixelShaderRHI,
		FRHIHullShader* InHullShaderRHI,
		FRHIDomainShader* InDomainShaderRHI,
		FRHIGeometryShader* InGeometryShaderRHI
	);

	virtual ~FVulkanBoundShaderState();

	FORCEINLINE FVulkanVertexShader*   GetVertexShader() const { return (FVulkanVertexShader*)CacheLink.GetVertexShader(); }
	FORCEINLINE FVulkanPixelShader*    GetPixelShader() const { return (FVulkanPixelShader*)CacheLink.GetPixelShader(); }
	FORCEINLINE FVulkanHullShader*     GetHullShader() const { return (FVulkanHullShader*)CacheLink.GetHullShader(); }
	FORCEINLINE FVulkanDomainShader*   GetDomainShader() const { return (FVulkanDomainShader*)CacheLink.GetDomainShader(); }
	FORCEINLINE FVulkanGeometryShader* GetGeometryShader() const { return (FVulkanGeometryShader*)CacheLink.GetGeometryShader(); }

	const FVulkanShader* GetShader(ShaderStage::EStage Stage) const
	{
		switch (Stage)
		{
		case ShaderStage::Vertex:		return GetVertexShader();
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		case ShaderStage::Hull:			return GetHullShader();
		case ShaderStage::Domain:		return GetDomainShader();
#endif
		case ShaderStage::Pixel:		return GetPixelShader();
#if VULKAN_SUPPORTS_GEOMETRY_SHADERS
		case ShaderStage::Geometry:	return GetGeometryShader();
#endif
		default: break;
		}
		checkf(0, TEXT("Invalid Shader Frequency %d"), (int32)Stage);
		return nullptr;
	}

private:
	FCachedBoundShaderStateLink_Threadsafe CacheLink;
};

struct FVulkanCpuReadbackBuffer
{
	VkBuffer Buffer;
	uint32 MipOffsets[MAX_TEXTURE_MIP_COUNT];
	uint32 MipSize[MAX_TEXTURE_MIP_COUNT];
};

/** Texture/RT wrapper. */
class FVulkanSurface : public FVulkanEvictable
{
	virtual void Evict(FVulkanDevice& Device);
	virtual void Move(FVulkanDevice& Device, FVulkanCommandListContext& Context, VulkanRHI::FVulkanAllocation& NewAllocation);
	virtual bool CanEvict();
	virtual bool CanMove();
public:
	struct FImageCreateInfo
	{
		VkImageCreateInfo ImageCreateInfo;
		//only used when HasImageFormatListKHR is supported. Otherise VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT is used.
		VkImageFormatListCreateInfoKHR ImageFormatListCreateInfo;
#if VULKAN_SUPPORTS_EXTERNAL_MEMORY
		//used when TexCreate_External is given
		VkExternalMemoryImageCreateInfoKHR ExternalMemImageCreateInfo;
#endif // VULKAN_SUPPORTS_EXTERNAL_MEMORY
		VkFormat FormatsUsed[2];
	};

	// Seperate method for creating VkImageCreateInfo
	static void GenerateImageCreateInfo(
		FImageCreateInfo& OutImageCreateInfo,
		FVulkanDevice& InDevice,
		VkImageViewType ResourceType,
		EPixelFormat InFormat,
		uint32 SizeX, uint32 SizeY, uint32 SizeZ,
		uint32 ArraySize,
		uint32 NumMips,
		uint32 NumSamples,
		ETextureCreateFlags UEFlags,
		VkFormat* OutStorageFormat = nullptr,
		VkFormat* OutViewFormat = nullptr,
		bool bForceLinearTexture = false);

	FVulkanSurface(FVulkanDevice& Device, FVulkanEvictable* Owner, VkImageViewType ResourceType, EPixelFormat Format,
					uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 ArraySize,
					uint32 NumMips, uint32 NumSamples, ETextureCreateFlags UEFlags, ERHIAccess InResourceState, const FRHIResourceCreateInfo& CreateInfo);

	// Constructor for externally owned Image
	FVulkanSurface(FVulkanDevice& Device, VkImageViewType ResourceType, EPixelFormat Format,
					uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 ArraySize, uint32 NumMips, uint32 NumSamples,
					VkImage InImage, ETextureCreateFlags UEFlags, const FRHIResourceCreateInfo& CreateInfo);

	virtual ~FVulkanSurface();

	void Destroy();
	void InvalidateMappedMemory();
	void* GetMappedPointer();

	void MoveSurface(FVulkanDevice& InDevice, FVulkanCommandListContext& Context, VulkanRHI::FVulkanAllocation& NewAllocation);
	void OnFullDefrag(FVulkanDevice& InDevice, FVulkanCommandListContext& Context, uint32 NewOffset);
	void EvictSurface(FVulkanDevice& InDevice);


#if 0
	/**
	 * Locks one of the texture's mip-maps.
	 * @param ArrayIndex Index of the texture array/face in the form Index*6+Face
	 * @return A pointer to the specified texture data.
	 */
	void* Lock(uint32 MipIndex, uint32 ArrayIndex, EResourceLockMode LockMode, uint32& DestStride);

	/** Unlocks a previously locked mip-map.
	 * @param ArrayIndex Index of the texture array/face in the form Index*6+Face
	 */
	void Unlock(uint32 MipIndex, uint32 ArrayIndex);
#endif

	/**
	 * Returns how much memory is used by the surface
	 */
	uint32 GetMemorySize() const
	{
		return MemoryRequirements.size;
	}

	/**
	 * Returns one of the texture's mip-maps stride.
	 */
	void GetMipStride(uint32 MipIndex, uint32& Stride);

	/*
	 * Returns the memory offset to the texture's mip-map.
	 */
	void GetMipOffset(uint32 MipIndex, uint32& Offset);

	/**
	* Returns how much memory a single mip uses.
	*/
	void GetMipSize(uint32 MipIndex, uint32& MipBytes);

	inline VkImageViewType GetViewType() const { return ViewType; }

	inline VkImageTiling GetTiling() const { return Tiling; }

	inline uint32 GetNumMips() const { return NumMips; }

	inline uint32 GetNumSamples() const { return NumSamples; }

	inline uint32 GetNumberOfArrayLevels() const
	{
		switch (ViewType)
		{
		case VK_IMAGE_VIEW_TYPE_1D:
		case VK_IMAGE_VIEW_TYPE_2D:
		case VK_IMAGE_VIEW_TYPE_3D:
			return 1;
		case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
			return ArraySize;
		case VK_IMAGE_VIEW_TYPE_CUBE:
			return 6;
		case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
			return 6 * ArraySize;
		default:
			ErrorInvalidViewType();
			return 1;
		}
	}
	VULKANRHI_API void ErrorInvalidViewType() const;

	// Full includes Depth+Stencil
	inline VkImageAspectFlags GetFullAspectMask() const
	{
		return FullAspectMask;
	}

	// Only Depth or Stencil
	inline VkImageAspectFlags GetPartialAspectMask() const
	{
		return PartialAspectMask;
	}

	inline bool IsDepthOrStencilAspect() const
	{
		return (FullAspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0;
	}

	inline bool IsImageOwner() const
	{
		return bIsImageOwner;
	}

	VULKANRHI_API VkDeviceMemory GetAllocationHandle() const;
	VULKANRHI_API uint64 GetAllocationOffset() const;


	FVulkanDevice* Device;

	VkImage Image;
	
	// Removes SRGB if requested, used to upload data
	VkFormat StorageFormat;
	// Format for SRVs, render targets
	VkFormat ViewFormat;
	uint32 Width, Height, Depth, ArraySize;
	// UE format
	EPixelFormat PixelFormat;
	ETextureCreateFlags UEFlags;
	VkMemoryPropertyFlags MemProps;
	VkMemoryRequirements MemoryRequirements;

	static void InternalLockWrite(FVulkanCommandListContext& Context, FVulkanSurface* Surface, const VkBufferImageCopy& Region, VulkanRHI::FStagingBuffer* StagingBuffer);

	const FVulkanCpuReadbackBuffer* GetCpuReadbackBuffer() const { return CpuReadbackBuffer; }
private:

	void SetInitialImageState(FVulkanCommandListContext& Context, VkImageLayout InitialLayout, bool bClear, const FClearValueBinding& ClearValueBinding);
	friend struct FRHICommandSetInitialImageState;

	void InternalMoveSurface(FVulkanDevice& InDevice, FVulkanCommandListContext& Context, VulkanRHI::FVulkanAllocation& DestAllocation, bool bSwapAllocation);

private:
	VkImageTiling Tiling;
	VkImageViewType	ViewType;

	bool bIsImageOwner;
	VulkanRHI::FVulkanAllocation Allocation;

	uint32 NumMips;
	uint32 NumSamples;

	VkImageAspectFlags FullAspectMask;
	VkImageAspectFlags PartialAspectMask;

	FVulkanCpuReadbackBuffer* CpuReadbackBuffer;
	FVulkanTextureBase* OwningTexture = 0;

	friend struct FVulkanTextureBase;
};


struct FVulkanTextureView
{
	FVulkanTextureView()
		: View(VK_NULL_HANDLE)
		, Image(VK_NULL_HANDLE)
		, ViewId(0)
	{
	}

	void Create(FVulkanDevice& Device, VkImage InImage, VkImageViewType ViewType, VkImageAspectFlags AspectFlags, EPixelFormat UEFormat, VkFormat Format, uint32 FirstMip, uint32 NumMips, uint32 ArraySliceIndex, uint32 NumArraySlices, bool bUseIdentitySwizzle = false);
	void Create(FVulkanDevice& Device, VkImage InImage, VkImageViewType ViewType, VkImageAspectFlags AspectFlags, EPixelFormat UEFormat, VkFormat Format, uint32 FirstMip, uint32 NumMips, uint32 ArraySliceIndex, uint32 NumArraySlices, FSamplerYcbcrConversionInitializer& ConversionInitializer, bool bUseIdentitySwizzle = false);
	void Destroy(FVulkanDevice& Device);

	VkImageView View;
	VkImage Image;
	uint32 ViewId;

private:
	static VkImageView StaticCreate(FVulkanDevice& Device, VkImage InImage, VkImageViewType ViewType, VkImageAspectFlags AspectFlags, EPixelFormat UEFormat, VkFormat Format, uint32 FirstMip, uint32 NumMips, uint32 ArraySliceIndex, uint32 NumArraySlices, bool bUseIdentitySwizzle, const FSamplerYcbcrConversionInitializer* ConversionInitializer);
};


struct FVulkanTextureBase : public FVulkanEvictable, public IRefCountedObject
{
	inline static FVulkanTextureBase* Cast(FRHITexture* Texture)
	{
		check(Texture);
		return (FVulkanTextureBase*)Texture->GetTextureBaseRHI();
	}

	FVulkanTextureBase(FVulkanDevice& Device, VkImageViewType ResourceType, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags UEFlags, ERHIAccess InResourceState, const FRHIResourceCreateInfo& CreateInfo);
	FVulkanTextureBase(FVulkanDevice& Device, VkImageViewType ResourceType, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, VkImage InImage, VkDeviceMemory InMem, ETextureCreateFlags UEFlags, const FRHIResourceCreateInfo& CreateInfo = FRHIResourceCreateInfo());
	FVulkanTextureBase(FVulkanDevice& Device, VkImageViewType ResourceType, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, VkImage InImage, VkDeviceMemory InMem, FSamplerYcbcrConversionInitializer& ConversionInitializer, ETextureCreateFlags UEFlags, const FRHIResourceCreateInfo& CreateInfo = FRHIResourceCreateInfo());

	// Aliasing constructor.
	FVulkanTextureBase(FTextureRHIRef& SrcTextureRHI, const FVulkanTextureBase* SrcTexture, VkImageViewType ResourceType, uint32 SizeX, uint32 SizeY, uint32 sizeZ);

	virtual ~FVulkanTextureBase();

	void AliasTextureResources(FTextureRHIRef& SrcTexture);

	FVulkanSurface Surface;

	// View with all mips/layers
	FVulkanTextureView DefaultView;
	// View with all mips/layers, but if it's a Depth/Stencil, only the Depth view
	FVulkanTextureView* PartialView;

	FTextureRHIRef AliasedTexture;

	virtual void OnLayoutTransition(FVulkanCommandListContext& Context, VkImageLayout NewLayout) {}

	template<typename T>
	void DumpMemory(T Callback)
	{
		Callback(TEXT("FVulkanTextureBase"), GetResourceFName(), this, GetRHIResource(), Surface.Width, Surface.Height, Surface.Depth, Surface.StorageFormat);
	}

	void Evict(FVulkanDevice& Device); ///evict to system memory
	void Move(FVulkanDevice& Device, FVulkanCommandListContext& Context, VulkanRHI::FVulkanAllocation& NewAllocation); //move to a full new allocation
	void OnFullDefrag(FVulkanDevice& Device, FVulkanCommandListContext& Context, uint32 NewOffset); //called when compacting an allocation. Old image can still be used as a copy source.
	FVulkanTextureBase* GetTextureBase() { return this; }

	void AttachView(VulkanRHI::FVulkanViewBase* View);
	void DetachView(VulkanRHI::FVulkanViewBase* View);

	virtual FRHITexture* GetRHITexture() = 0;
private:
	void InvalidateViews(FVulkanDevice& Device);
	VulkanRHI::FVulkanViewBase* FirstView = nullptr;

	void DestroyViews();
	virtual FName GetResourceFName() = 0;
	virtual FRHIResource* GetRHIResource(){ return 0; }

};

class FVulkanTexture2D : public FRHITexture2D, public FVulkanTextureBase
{
	FName GetResourceFName(){ return GetName(); }
	virtual FRHIResource* GetRHIResource() { return (FRHITexture2D*)this; }
public:
	FVulkanTexture2D(FVulkanDevice& Device, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags UEFlags, ERHIAccess InResourceState, const FRHIResourceCreateInfo& CreateInfo);
	FVulkanTexture2D(FVulkanDevice& Device, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, VkImage Image, ETextureCreateFlags UEFlags, const FRHIResourceCreateInfo& CreateInfo);
	FVulkanTexture2D(FVulkanDevice& Device, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, VkImage Image, struct FSamplerYcbcrConversionInitializer& ConversionInitializer, ETextureCreateFlags UEFlags, const FRHIResourceCreateInfo& CreateInfo);

	// Aliasing constructor
	FVulkanTexture2D(FTextureRHIRef& SrcTextureRHI, const FVulkanTexture2D* SrcTexture);

	virtual ~FVulkanTexture2D();
	virtual FRHITexture* GetRHITexture()
	{
		return this;
	};

	// IRefCountedObject interface.
	virtual uint32 AddRef() const override final
	{
		return FRHIResource::AddRef();
	}
	virtual uint32 Release() const override final
	{
		return FRHIResource::Release();
	}
	virtual uint32 GetRefCount() const override final
	{
		return FRHIResource::GetRefCount();
	}

	virtual void* GetTextureBaseRHI() override final
	{
		FVulkanTextureBase* Base = static_cast<FVulkanTextureBase*>(this);
		return Base;
	}

	virtual void* GetNativeResource() const
	{
		return (void*)Surface.Image;
	}
};

class FVulkanTexture2DArray : public FRHITexture2DArray, public FVulkanTextureBase
{
	FName GetResourceFName() { return GetName(); }
public:
	// Constructor, just calls base and Surface constructor
	FVulkanTexture2DArray(FVulkanDevice& Device, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ERHIAccess InResourceState, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue);
	FVulkanTexture2DArray(FVulkanDevice& Device, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, VkImage Image, ETextureCreateFlags Flags, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue);

	// Aliasing constructor
	FVulkanTexture2DArray(FTextureRHIRef& SrcTextureRHI, const FVulkanTexture2DArray* SrcTexture);

	virtual FRHITexture* GetRHITexture()
	{
		return this;
	};



	// IRefCountedObject interface.
	virtual uint32 AddRef() const override final
	{
		return FRHIResource::AddRef();
	}
	virtual uint32 Release() const override final
	{
		return FRHIResource::Release();
	}
	virtual uint32 GetRefCount() const override final
	{
		return FRHIResource::GetRefCount();
	}

	virtual void* GetTextureBaseRHI() override final
	{
		return (FVulkanTextureBase*)this;
	}

	virtual void* GetNativeResource() const
	{
		return (void*)Surface.Image;
	}
};

class FVulkanTexture3D : public FRHITexture3D, public FVulkanTextureBase
{
	FName GetResourceFName() { return GetName(); }
public:
	// Constructor, just calls base and Surface constructor
	FVulkanTexture3D(FVulkanDevice& Device, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue);
	FVulkanTexture3D(FVulkanDevice& Device, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 NumMips, VkImage Image, ETextureCreateFlags Flags, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue);
	virtual ~FVulkanTexture3D();


	virtual FRHITexture* GetRHITexture()
	{
		return this;
	}


	// IRefCountedObject interface.
	virtual uint32 AddRef() const override final
	{
		return FRHIResource::AddRef();
	}
	virtual uint32 Release() const override final
	{
		return FRHIResource::Release();
	}
	virtual uint32 GetRefCount() const override final
	{
		return FRHIResource::GetRefCount();
	}

	virtual void* GetTextureBaseRHI() override final
	{
		return (FVulkanTextureBase*)this;
	}

	virtual void* GetNativeResource() const
	{
		return (void*)Surface.Image;
	}
};

class FVulkanTextureCube : public FRHITextureCube, public FVulkanTextureBase
{
	FName GetResourceFName() { return GetName(); }
public:
	FVulkanTextureCube(FVulkanDevice& Device, EPixelFormat Format, uint32 Size, bool bArray, uint32 ArraySize, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue);
	FVulkanTextureCube(FVulkanDevice& Device, EPixelFormat Format, uint32 Size, bool bArray, uint32 ArraySize, uint32 NumMips, VkImage Image, ETextureCreateFlags Flags, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue);

	// Aliasing constructor
	FVulkanTextureCube(FTextureRHIRef& SrcTextureRHI, const FVulkanTextureCube* SrcTexture);

	virtual ~FVulkanTextureCube();

	virtual FRHITexture* GetRHITexture()
	{
		return this;
	};


	// IRefCountedObject interface.
	virtual uint32 AddRef() const override final
	{
		return FRHIResource::AddRef();
	}
	virtual uint32 Release() const override final
	{
		return FRHIResource::Release();
	}
	virtual uint32 GetRefCount() const override final
	{
		return FRHIResource::GetRefCount();
	}

	virtual void* GetTextureBaseRHI() override final
	{
		return (FVulkanTextureBase*)this;
	}

	virtual void* GetNativeResource() const
	{
		return (void*)Surface.Image;
	}
};

class FVulkanTextureReference : public FRHITextureReference, public FVulkanTextureBase
{
	FName GetResourceFName() { return GetName(); }
public:
	explicit FVulkanTextureReference(FVulkanDevice& Device, FLastRenderTimeContainer* InLastRenderTime)
	:	FRHITextureReference(InLastRenderTime)
	,	FVulkanTextureBase(Device, VK_IMAGE_VIEW_TYPE_MAX_ENUM, PF_Unknown, 0, 0, 0, 1, 1, 1, VK_NULL_HANDLE, VK_NULL_HANDLE, TexCreate_None)
	{}

	virtual FRHITexture* GetRHITexture()
	{
		return this;
	};


	// IRefCountedObject interface.
	virtual uint32 AddRef() const override final
	{
		return FRHIResource::AddRef();
	}

	virtual uint32 Release() const override final
	{
		return FRHIResource::Release();
	}

	virtual uint32 GetRefCount() const override final
	{
		return FRHIResource::GetRefCount();
	}

	virtual void* GetTextureBaseRHI() override final
	{
		return GetReferencedTexture() ? GetReferencedTexture()->GetTextureBaseRHI() : nullptr;
	}

	virtual void* GetNativeResource() const
	{
		return (void*)Surface.Image;
	}

	void SetReferencedTexture(FRHITexture* InTexture);
};

class FVulkanQueryPool : public VulkanRHI::FDeviceChild
{
public:
	FVulkanQueryPool(FVulkanDevice* InDevice, FVulkanCommandBufferManager* CommandBufferManager, uint32 InMaxQueries, VkQueryType InQueryType, bool bInShouldAddReset = true);
	virtual ~FVulkanQueryPool();

	inline uint32 GetMaxQueries() const
	{
		return MaxQueries;
	}

	inline VkQueryPool GetHandle() const
	{
		return QueryPool;
	}

	inline uint64 GetResultValue(uint32 Index) const
	{
		return QueryOutput[Index];
	}

protected:
	VkQueryPool QueryPool;
	VkEvent ResetEvent;
	const uint32 MaxQueries;
	const VkQueryType QueryType;
	TArray<uint64> QueryOutput;
};

class FVulkanOcclusionQueryPool : public FVulkanQueryPool
{
public:
	FVulkanOcclusionQueryPool(FVulkanDevice* InDevice, FVulkanCommandBufferManager* CommandBufferManager, uint32 InMaxQueries)
		: FVulkanQueryPool(InDevice, CommandBufferManager, InMaxQueries, VK_QUERY_TYPE_OCCLUSION)
	{
		AcquiredIndices.AddZeroed(Align(InMaxQueries, 64) / 64);
		AllocatedQueries.AddZeroed(InMaxQueries);
	}

	inline uint32 AcquireIndex(FVulkanOcclusionQuery* Query)
	{
		check(NumUsedQueries < MaxQueries);
		const uint32 Index = NumUsedQueries;
		const uint32 Word = Index / 64;
		const uint32 Bit = Index % 64;
		const uint64 Mask = (uint64)1 << (uint64)Bit;
		const uint64& WordValue = AcquiredIndices[Word];
		AcquiredIndices[Word] = WordValue | Mask;
		++NumUsedQueries;
		ensure(AllocatedQueries[Index] == nullptr);
		AllocatedQueries[Index] = Query;
		return Index;
	}

	inline void ReleaseIndex(uint32 Index)
	{
		check(Index < NumUsedQueries);
		const uint32 Word = Index / 64;
		const uint32 Bit = Index % 64;
		const uint64 Mask = (uint64)1 << (uint64)Bit;
		const uint64& WordValue = AcquiredIndices[Word];
		ensure((WordValue & Mask) == Mask);
		AcquiredIndices[Word] = WordValue & (~Mask);
		AllocatedQueries[Index] = nullptr;
	}

	inline void EndBatch(FVulkanCmdBuffer* InCmdBuffer)
	{
		ensure(State == EState::RHIT_PostBeginBatch);
		State = EState::RHIT_PostEndBatch;
		SetFence(InCmdBuffer);
	}

	bool CanBeReused();

	inline bool TryGetResults(bool bWait)
	{
		if (State == RT_PostGetResults)
		{
			return true;
		}

		if (State == RHIT_PostEndBatch)
		{
			return InternalTryGetResults(bWait);
		}

		return false;
	}

	void Reset(FVulkanCmdBuffer* InCmdBuffer, uint32 InFrameNumber);

	bool IsStalePool() const;

	void FlushAllocatedQueries();

	enum EState
	{
		Undefined,
		RHIT_PostBeginBatch,
		RHIT_PostEndBatch,
		RT_PostGetResults,
	};
	EState State = Undefined;
	
	// frame number when pool was placed into free list
	uint32 FreedFrameNumber = UINT32_MAX;
protected:
	uint32 NumUsedQueries = 0;
	TArray<FVulkanOcclusionQuery*> AllocatedQueries;
	TArray<uint64> AcquiredIndices;
	bool InternalTryGetResults(bool bWait);
	void SetFence(FVulkanCmdBuffer* InCmdBuffer);

	FVulkanCmdBuffer* CmdBuffer = nullptr;
	uint64 FenceCounter = UINT64_MAX;
	uint32 FrameNumber = UINT32_MAX;
};

class FVulkanTimingQueryPool : public FVulkanQueryPool
{
public:
	FVulkanTimingQueryPool(FVulkanDevice* InDevice, FVulkanCommandBufferManager* CommandBufferManager, uint32 InBufferSize)
		: FVulkanQueryPool(InDevice, CommandBufferManager, InBufferSize * 2, VK_QUERY_TYPE_TIMESTAMP, false)
		, BufferSize(InBufferSize)
	{
		TimestampListHandles.AddZeroed(InBufferSize * 2);
	}

	uint32 CurrentTimestamp = 0;
	uint32 NumIssuedTimestamps = 0;
	const uint32 BufferSize;

	struct FCmdBufferFence
	{
		FVulkanCmdBuffer* CmdBuffer;
		uint64 FenceCounter;
		uint64 FrameCount = UINT64_MAX;
	};
	TArray<FCmdBufferFence> TimestampListHandles;

	VulkanRHI::FStagingBuffer* ResultsBuffer = nullptr;
};

class FVulkanRenderQuery : public FRHIRenderQuery
{
public:
	FVulkanRenderQuery(ERenderQueryType InType)
		: QueryType(InType)
	{
	}

	virtual ~FVulkanRenderQuery() {}

	const ERenderQueryType QueryType;
	uint64 Result = 0;

	uint32 IndexInPool = UINT32_MAX;
};

class FVulkanOcclusionQuery : public FVulkanRenderQuery
{
public:
	FVulkanOcclusionQuery();
	virtual ~FVulkanOcclusionQuery();

	enum class EState
	{
		Undefined,
		RHI_PostBegin,
		RHI_PostEnd,
		RT_GotResults,
		FlushedFromPoolHadResults,
	};

	FVulkanOcclusionQueryPool* Pool = nullptr;

	void ReleaseFromPool();

	EState State = EState::Undefined;
};

class FVulkanTimingQuery : public FVulkanRenderQuery
{
public:
	FVulkanTimingQuery();
	virtual ~FVulkanTimingQuery();

	FVulkanTimingQueryPool* Pool = nullptr;
};

struct FVulkanBufferView : public FRHIResource, public VulkanRHI::FDeviceChild
{
	FVulkanBufferView(FVulkanDevice* InDevice)
		: VulkanRHI::FDeviceChild(InDevice)
		, View(VK_NULL_HANDLE)
		, ViewId(0)
		, Flags(0)
		, Offset(0)
		, Size(0)
	{
	}

	virtual ~FVulkanBufferView()
	{
		Destroy();
	}

	void Create(FVulkanBuffer& Buffer, EPixelFormat Format, uint32 InOffset, uint32 InSize);
	void Create(FVulkanResourceMultiBuffer* Buffer, EPixelFormat Format, uint32 InOffset, uint32 InSize);
	void Create(VkFormat Format, FVulkanResourceMultiBuffer* Buffer, uint32 InOffset, uint32 InSize);
	void Destroy();

	VkBufferView View;
	uint32 ViewId;
	VkFlags Flags;
	uint32 Offset;
	uint32 Size;
};

class FVulkanBuffer : public FRHIResource
{
public:
	FVulkanBuffer(FVulkanDevice& Device, uint32 InSize, VkFlags InUsage, VkMemoryPropertyFlags InMemPropertyFlags, bool bAllowMultiLock, const char* File, int32 Line);
	virtual ~FVulkanBuffer();

	inline VkBuffer GetBufferHandle() const { return Buf; }

	inline uint32 GetSize() const { return Size; }

	void* Lock(uint32 InSize, uint32 InOffset = 0);

	void Unlock();

	inline VkFlags GetFlags() const { return Usage; }

private:
	FVulkanDevice& Device;
	VkBuffer Buf;
	VulkanRHI::FDeviceMemoryAllocation* Allocation;
	uint32 Size;
	VkFlags Usage;

	void* BufferPtr;	
	VkMappedMemoryRange MappedRange;

	bool bAllowMultiLock;
	int32 LockStack;
};

struct FVulkanRingBuffer : public FVulkanEvictable, public VulkanRHI::FDeviceChild
{
	virtual void Evict(FVulkanDevice& Device);
	virtual void Move(FVulkanDevice& Device, FVulkanCommandListContext& Context, VulkanRHI::FVulkanAllocation& NewAllocation);

public:
	FVulkanRingBuffer(FVulkanDevice* InDevice, uint64 TotalSize, VkFlags Usage, VkMemoryPropertyFlags MemPropertyFlags);
	virtual ~FVulkanRingBuffer();

	// Allocate some space in the ring buffer
	inline uint64 AllocateMemory(uint64 Size, uint32 Alignment, FVulkanCmdBuffer* InCmdBuffer)
	{
		Alignment = FMath::Max(Alignment, MinAlignment);
		uint64 AllocationOffset = Align<uint64>(BufferOffset, Alignment);
		if (AllocationOffset + Size <= BufferSize)
		{
			BufferOffset = AllocationOffset + Size;
			return AllocationOffset;
		}

		return WrapAroundAllocateMemory(Size, Alignment, InCmdBuffer);
	}

	inline uint32 GetBufferOffset() const
	{
		return Allocation.Offset;
	}

	inline VkBuffer GetHandle() const
	{
		return Allocation.GetBufferHandle();
	}

	inline void* GetMappedPointer()
	{
		return Allocation.GetMappedPointer(Device);
	}

	VulkanRHI::FVulkanAllocation& GetAllocation()
	{
		return Allocation;
	}

	const VulkanRHI::FVulkanAllocation& GetAllocation() const
	{
		return Allocation;
	}


protected:
	uint64 BufferSize;
	uint64 BufferOffset;
	uint32 MinAlignment;
	VulkanRHI::FVulkanAllocation Allocation;

	// Fence for wrapping around
	FVulkanCmdBuffer* FenceCmdBuffer = nullptr;
	uint64 FenceCounter = 0;

	uint64 WrapAroundAllocateMemory(uint64 Size, uint32 Alignment, FVulkanCmdBuffer* InCmdBuffer);
};

struct FVulkanUniformBufferUploader : public VulkanRHI::FDeviceChild
{
public:
	struct FUniformBufferPatchInfo
	{
		const class FVulkanUniformBuffer* SourceBuffer; // Todo replace it with a true buffer handle instead of pointer.
		uint16 SourceOffsetInFloats;
		uint16 SizeInFloats;
		uint8* RESTRICT DestBufferAddress;
	};

	FVulkanUniformBufferUploader(FVulkanDevice* InDevice);
	~FVulkanUniformBufferUploader();

	uint8* GetCPUMappedPointer()
	{
		return (uint8*)CPUBuffer->GetMappedPointer();
	}

	uint64 AllocateMemory(uint64 Size, uint32 Alignment, FVulkanCmdBuffer* InCmdBuffer)
	{
		return CPUBuffer->AllocateMemory(Size, Alignment, InCmdBuffer);
	}

	const VulkanRHI::FVulkanAllocation& GetCPUBufferAllocation() const
	{
		return CPUBuffer->GetAllocation();
	}

	VkBuffer GetCPUBufferHandle() const
	{
		return CPUBuffer->GetHandle();
	}

	inline uint32 GetCPUBufferOffset() const
	{
		return CPUBuffer->GetBufferOffset();
	}

	inline TArray<FUniformBufferPatchInfo>& GetUniformBufferPatchInfo()
	{
		return BufferPatchInfos;
	}

	void ApplyUniformBufferPatching(bool bNeedAbort);
	int32 UniformBufferPatchingFrameNumber;
	bool bEnableUniformBufferPatching;
	uint64 BeginPatchSubmitCounter;

protected:
	FVulkanRingBuffer* CPUBuffer;
	TArray<FUniformBufferPatchInfo> BufferPatchInfos;
	friend class FVulkanCommandListContext;
};

class FVulkanResourceMultiBuffer : public FVulkanEvictable, public VulkanRHI::FDeviceChild
{
	virtual void Evict(FVulkanDevice& Device);
	virtual void Move(FVulkanDevice& Device, FVulkanCommandListContext& Context, VulkanRHI::FVulkanAllocation& NewAllocation);

public:
	FVulkanResourceMultiBuffer(FVulkanDevice* InDevice, VkBufferUsageFlags InBufferUsageFlags, uint32 InSize, uint32 InUEUsage, FRHIResourceCreateInfo& CreateInfo, class FRHICommandListImmediate* InRHICmdList = nullptr);
	virtual ~FVulkanResourceMultiBuffer();

	inline const VulkanRHI::FVulkanAllocation& GetCurrentAllocation() const
	{
		return Current.Alloc;
	}

	inline VkBuffer GetHandle() const
	{
		return Current.Handle;
	}

	inline bool IsDynamic() const
	{
		return NumBuffers > 1;
	}

	inline int32 GetDynamicIndex() const
	{
		return DynamicBufferIndex;
	}

	inline bool IsVolatile() const
	{
		return NumBuffers == 0;
	}

	inline uint32 GetVolatileLockCounter() const
	{
		check(IsVolatile());
		return VolatileLockInfo.LockCounter;
	}
	inline uint32 GetVolatileLockSize() const
	{
		check(IsVolatile());
		return VolatileLockInfo.Size;
	}

	inline int32 GetNumBuffers() const
	{
		return NumBuffers;
	}

	// Offset used for Binding a VkBuffer
	inline uint32 GetOffset() const
	{
		return Current.Offset;
	}

	// Remaining size from the current offset
	inline uint64 GetCurrentSize() const
	{
		return Current.Alloc.Size - (Current.Offset - Current.Alloc.Offset);
	}


	inline VkBufferUsageFlags GetBufferUsageFlags() const
	{
		return BufferUsageFlags;
	}

	inline uint32 GetUEUsage() const
	{
		return BufferUsageFlags;
	}


	void* Lock(bool bFromRenderingThread, EResourceLockMode LockMode, uint32 Size, uint32 Offset);
	void Unlock(bool bFromRenderingThread);

	void Swap(FVulkanResourceMultiBuffer& Other);


	template<typename T>
	void DumpMemory(T Callback)
	{
		Callback(TEXT("FVulkanResourceMultiBuffer"), FName(), this, 0, GetCurrentSize() * GetNumBuffers(), 1, 1, VK_FORMAT_UNDEFINED);
	}


protected:
	uint32 UEUsage;
	VkBufferUsageFlags BufferUsageFlags;
	uint32 NumBuffers;
	uint32 DynamicBufferIndex;

	enum
	{
		NUM_BUFFERS = 3,
	};

	VulkanRHI::FVulkanAllocation Buffers[NUM_BUFFERS];
	struct
	{
		VulkanRHI::FVulkanAllocation Alloc;
		VkBuffer Handle = VK_NULL_HANDLE;
		uint64 Offset = 0;
		uint64 Size = 0;
	} Current;
	VulkanRHI::FTempFrameAllocationBuffer::FTempAllocInfo VolatileLockInfo;

	static void InternalUnlock(FVulkanCommandListContext& Context, VulkanRHI::FPendingBufferLock& PendingLock, FVulkanResourceMultiBuffer* MultiBuffer, int32 InDynamicBufferIndex);

	friend class FVulkanCommandListContext;
	friend struct FRHICommandMultiBufferUnlock;
};

class FVulkanIndexBuffer : public FRHIIndexBuffer, public FVulkanResourceMultiBuffer
{
public:
	FVulkanIndexBuffer(FVulkanDevice* InDevice, uint32 InStride, uint32 InSize, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, class FRHICommandListImmediate* InRHICmdList);

	inline VkIndexType GetIndexType() const
	{
		return IndexType;
	}

	void Swap(FVulkanIndexBuffer& Other);

private:
	VkIndexType IndexType;
};

class FVulkanVertexBuffer : public FRHIVertexBuffer, public FVulkanResourceMultiBuffer
{
public:
	FVulkanVertexBuffer(FVulkanDevice* InDevice, uint32 InSize, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, class FRHICommandListImmediate* InRHICmdList);

	void Swap(FVulkanVertexBuffer& Other);
};

class FVulkanUniformBuffer : public FRHIUniformBuffer
{
public:
	FVulkanUniformBuffer(const FRHIUniformBufferLayout& InLayout, const void* Contents, EUniformBufferUsage InUsage, EUniformBufferValidation Validation);

	const TArray<TRefCountPtr<FRHIResource>>& GetResourceTable() const { return ResourceTable; }

	void UpdateResourceTable(const FRHIUniformBufferLayout& InLayout, const void* Contents, int32 ResourceNum);
	void UpdateResourceTable(FRHIResource** Resources, int32 ResourceNum);

protected:
	TArray<TRefCountPtr<FRHIResource>> ResourceTable;
};

class FVulkanEmulatedUniformBuffer : public FVulkanUniformBuffer
{
public:
	FVulkanEmulatedUniformBuffer(const FRHIUniformBufferLayout& InLayout, const void* Contents, EUniformBufferUsage InUsage, EUniformBufferValidation Validation);

	TArray<uint8> ConstantData;

	void UpdateConstantData(const void* Contents, int32 ContentsSize);

	virtual int32 GetPatchingFrameNumber() const override { return PatchingFrameNumber; };
	virtual void SetPatchingFrameNumber(int32 FrameNumber) override { PatchingFrameNumber = FrameNumber; };

protected:
	uint32 PatchingFrameNumber;
};

class FVulkanRealUniformBuffer : public FVulkanUniformBuffer
{
public:
	FVulkanDevice* Device;
	FVulkanRealUniformBuffer(FVulkanDevice& Device, const FRHIUniformBufferLayout& InLayout, const void* Contents, EUniformBufferUsage InUsage, EUniformBufferValidation Validation);
	virtual ~FVulkanRealUniformBuffer();


	inline uint32 GetOffset() const
	{
		return Allocation.Offset;
	}

	inline void UpdateAllocation(VulkanRHI::FVulkanAllocation& NewAlloc)
	{
		NewAlloc.Swap(Allocation);
	}
	VulkanRHI::FVulkanAllocation Allocation;
};

class FVulkanStructuredBuffer : public FRHIStructuredBuffer, public FVulkanResourceMultiBuffer
{
public:
	FVulkanStructuredBuffer(FVulkanDevice* InDevice, uint32 Stride, uint32 Size, FRHIResourceCreateInfo& CreateInfo, uint32 InUsage);

	~FVulkanStructuredBuffer();

};



class FVulkanUnorderedAccessView : public FRHIUnorderedAccessView, public VulkanRHI::FVulkanViewBase
{
public:

	FVulkanUnorderedAccessView(FVulkanDevice* Device, FVulkanStructuredBuffer* StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer);
	FVulkanUnorderedAccessView(FVulkanDevice* Device, FRHITexture* TextureRHI, uint32 MipLevel);
	FVulkanUnorderedAccessView(FVulkanDevice* Device, FVulkanVertexBuffer* VertexBuffer, EPixelFormat Format);
	FVulkanUnorderedAccessView(FVulkanDevice* Device, FVulkanIndexBuffer* IndexBuffer, EPixelFormat Format);


	~FVulkanUnorderedAccessView();

	void Invalidate();

	void UpdateView();

protected:
	// the potential resources to refer to with the UAV object
	TRefCountPtr<FVulkanStructuredBuffer> SourceStructuredBuffer;
	// The texture that this UAV come from
	TRefCountPtr<FRHITexture> SourceTexture;
	FVulkanTextureView TextureView;
	uint32 MipLevel;

	// The vertex buffer this UAV comes from (can be null)
	TRefCountPtr<FVulkanVertexBuffer> SourceVertexBuffer;
	TRefCountPtr<FVulkanIndexBuffer> SourceIndexBuffer;
	TRefCountPtr<FVulkanBufferView> BufferView;
	EPixelFormat BufferViewFormat;

	// Used to check on volatile buffers if a new BufferView is required
	uint32 VolatileLockCounter;
	friend class FVulkanPendingGfxState;
	friend class FVulkanPendingComputeState;
	friend class FVulkanDynamicRHI;
	friend class FVulkanCommandListContext;
};


class FVulkanShaderResourceView : public FRHIShaderResourceView, public VulkanRHI::FVulkanViewBase
{
public:
	FVulkanShaderResourceView(FVulkanDevice* Device, FRHIResource* InRHIBuffer, FVulkanResourceMultiBuffer* InSourceBuffer, uint32 InSize, EPixelFormat InFormat, uint32 InOffset = 0);
	FVulkanShaderResourceView(FVulkanDevice* Device, FRHITexture* InSourceTexture, const FRHITextureSRVCreateInfo& InCreateInfo);
	FVulkanShaderResourceView(FVulkanDevice* Device, FVulkanStructuredBuffer* InStructuredBuffer, uint32 InOffset = 0);

	void Clear();

	void Rename(FRHIResource* InRHIBuffer, FVulkanResourceMultiBuffer* InSourceBuffer, uint32 InSize, EPixelFormat InFormat);

	void Invalidate();
	void UpdateView();

	inline FVulkanBufferView* GetBufferView()
	{
		return BufferViews[BufferIndex];
	}

	EPixelFormat BufferViewFormat;
	ERHITextureSRVOverrideSRGBType SRGBOverride = SRGBO_Default;

	// The texture that this SRV come from
	TRefCountPtr<FRHITexture> SourceTexture;
	FVulkanTextureView TextureView;
	FVulkanStructuredBuffer* SourceStructuredBuffer;
	uint32 MipLevel = 0;
	uint32 NumMips = MAX_uint32;
	uint32 FirstArraySlice = 0;
	uint32 NumArraySlices = 0;

	~FVulkanShaderResourceView();

	TArray<TRefCountPtr<FVulkanBufferView>> BufferViews;
	uint32 BufferIndex = 0;
	uint32 Size;
	uint32 Offset = 0;
	// The buffer this SRV comes from (can be null)
	FVulkanResourceMultiBuffer* SourceBuffer;
	// To keep a reference
	TRefCountPtr<FRHIResource> SourceRHIBuffer;

protected:
	// Used to check on volatile buffers if a new BufferView is required
	VkBuffer VolatileBufferHandle = VK_NULL_HANDLE;
	uint32 VolatileLockCounter = MAX_uint32;

	FVulkanShaderResourceView* NextView = 0;
	friend struct FVulkanTextureBase;
};

class FVulkanVertexInputStateInfo
{
public:
	FVulkanVertexInputStateInfo();
	~FVulkanVertexInputStateInfo();

	void Generate(FVulkanVertexDeclaration* VertexDeclaration, uint32 VertexHeaderInOutAttributeMask);

	inline uint32 GetHash() const
	{
		check(Info.sType == VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
		return Hash;
	}

	inline const VkPipelineVertexInputStateCreateInfo& GetInfo() const
	{
		return Info;
	}

	bool operator ==(const FVulkanVertexInputStateInfo& Other);

protected:
	VkPipelineVertexInputStateCreateInfo Info;
	uint32 Hash;

	uint32 BindingsNum;
	uint32 BindingsMask;

	//#todo-rco: Remove these TMaps
	TMap<uint32, uint32> BindingToStream;
	TMap<uint32, uint32> StreamToBinding;
	VkVertexInputBindingDescription Bindings[MaxVertexElementCount];

	uint32 AttributesNum;
	VkVertexInputAttributeDescription Attributes[MaxVertexElementCount];

	friend class FVulkanPendingGfxState;
	friend class FVulkanPipelineStateCacheManager;
};

// This class holds the staging area for packed global uniform buffers for a given shader
class FPackedUniformBuffers
{
public:
	// One buffer is a chunk of bytes
	typedef TArray<uint8> FPackedBuffer;

	void LazyInitSrcUniformPatchingResources()
	{
		PackedBufferSegmentIndices.AddDefaulted(PackedUniformBuffers.Num());
		PackedBufferSegmentSources.AddDefaulted(PackedUniformBuffers.Num());
		PackedBufferSegmentFrameIndex.AddDefaulted(PackedUniformBuffers.Num());
		CopyInfoRemapping.AddZeroed(EmulatedUBsCopyInfo.Num());
		for (int32 RangeIndex = 0; RangeIndex < EmulatedUBsCopyRanges.Num(); ++RangeIndex)
		{
			uint32 Range = EmulatedUBsCopyRanges[RangeIndex];
			uint16 Start = (Range >> 16) & 0xffff;
			uint16 Count = Range & 0xffff;
			for (int32 Index = Start; Index < Start + Count; ++Index)
			{
				const CrossCompiler::FUniformBufferCopyInfo& CopyInfo = EmulatedUBsCopyInfo[Index];
				int32 PackedUniformBufferIndex = (int32)CopyInfo.DestUBIndex;
				check(CopyInfo.SourceUBIndex == RangeIndex);
				CopyInfoRemapping[Index] = PackedBufferSegmentIndices[PackedUniformBufferIndex].Num();
				PackedBufferSegmentIndices[PackedUniformBufferIndex].Push(Index);

				// Initialize as 0
				PackedBufferSegmentFrameIndex[PackedUniformBufferIndex].Push(-1);
				PackedBufferSegmentSources[PackedUniformBufferIndex].Push(NULL);
			}
		}
		bSrcUniformPatchingResourceInitialized = true;
	}

	void Init(const FVulkanShaderHeader& InCodeHeader, uint64& OutPackedUniformBufferStagingMask)
	{
		PackedUniformBuffers.AddDefaulted(InCodeHeader.PackedUBs.Num());
		for (int32 Index = 0; Index < InCodeHeader.PackedUBs.Num(); ++Index)
		{
			PackedUniformBuffers[Index].AddUninitialized(InCodeHeader.PackedUBs[Index].SizeInBytes);
		}

		OutPackedUniformBufferStagingMask = ((uint64)1 << (uint64)InCodeHeader.PackedUBs.Num()) - 1;
		EmulatedUBsCopyInfo = InCodeHeader.EmulatedUBsCopyInfo;
		EmulatedUBsCopyRanges = InCodeHeader.EmulatedUBCopyRanges;
		bSrcUniformPatchingResourceInitialized = false;
	}

	inline void SetPackedGlobalParameter(uint32 BufferIndex, uint32 ByteOffset, uint32 NumBytes, const void* RESTRICT NewValue, uint64& InOutPackedUniformBufferStagingDirty)
	{
		FPackedBuffer& StagingBuffer = PackedUniformBuffers[BufferIndex];
		check(ByteOffset + NumBytes <= (uint32)StagingBuffer.Num());
		check((NumBytes & 3) == 0 && (ByteOffset & 3) == 0);
		uint32* RESTRICT RawDst = (uint32*)(StagingBuffer.GetData() + ByteOffset);
		uint32* RESTRICT RawSrc = (uint32*)NewValue;
		uint32* RESTRICT RawSrcEnd = RawSrc + (NumBytes >> 2);

		bool bChanged = false;
		while (RawSrc != RawSrcEnd)
		{
			bChanged |= CopyAndReturnNotEqual(*RawDst++, *RawSrc++);
		}

		InOutPackedUniformBufferStagingDirty = InOutPackedUniformBufferStagingDirty | ((uint64)(bChanged ? 1 : 0) << (uint64)BufferIndex);
	}

	// Copies a 'real' constant buffer into the packed globals uniform buffer (only the used ranges)
	inline void SetEmulatedUniformBufferIntoPacked(uint32 BindPoint, const TArray<uint8>& ConstantData, const FVulkanUniformBuffer* SrcBuffer, uint64& NEWPackedUniformBufferStagingDirty)
	{
		// Emulated UBs. Assumes UniformBuffersCopyInfo table is sorted by CopyInfo.SourceUBIndex
		if (BindPoint < (uint32)EmulatedUBsCopyRanges.Num())
		{
			uint32 Range = EmulatedUBsCopyRanges[BindPoint];
			uint16 Start = (Range >> 16) & 0xffff;
			uint16 Count = Range & 0xffff;
			const uint8* RESTRICT SourceData = ConstantData.GetData();
			for (int32 Index = Start; Index < Start + Count; ++Index)
			{
				const CrossCompiler::FUniformBufferCopyInfo& CopyInfo = EmulatedUBsCopyInfo[Index];
				check(CopyInfo.SourceUBIndex == BindPoint);
				FPackedBuffer& StagingBuffer = PackedUniformBuffers[(int32)CopyInfo.DestUBIndex];
				//check(ByteOffset + NumBytes <= (uint32)StagingBuffer.Num());
				bool bChanged = false;
				uint32* RESTRICT RawDst = (uint32*)(StagingBuffer.GetData() + CopyInfo.DestOffsetInFloats * 4);
				uint32* RESTRICT RawSrc = (uint32*)(SourceData + CopyInfo.SourceOffsetInFloats * 4);
				uint32* RESTRICT RawSrcEnd = RawSrc + CopyInfo.SizeInFloats;
				do
				{
					bChanged |= CopyAndReturnNotEqual(*RawDst++, *RawSrc++);
				}
				while (RawSrc != RawSrcEnd);
				NEWPackedUniformBufferStagingDirty = NEWPackedUniformBufferStagingDirty | ((uint64)(bChanged ? 1 : 0) << (uint64)CopyInfo.DestUBIndex);

				// For Non-LateLatching Flaged buffer, GetPatchingFrameNumber() == -1
				int32 PatchingFrameNumber = SrcBuffer->GetPatchingFrameNumber();
				if (PatchingFrameNumber > 0)
				{
					if (!bSrcUniformPatchingResourceInitialized)
					{
						LazyInitSrcUniformPatchingResources();
					}
					MaskPackedBufferCopyInfoSegmentSource(SrcBuffer, PatchingFrameNumber, Index, CopyInfo.DestUBIndex);
				}
				else if (bSrcUniformPatchingResourceInitialized)
				{
					MaskPackedBufferCopyInfoSegmentSource(NULL, -1, Index, CopyInfo.DestUBIndex);
				}
			}
		}
	}

	inline const FPackedBuffer& GetBuffer(int32 Index) const
	{
		return PackedUniformBuffers[Index];
	}

	inline void RecordUniformBufferPatch(TArray<FVulkanUniformBufferUploader::FUniformBufferPatchInfo>& PostBindingPatches, int32 FrameNumber, int32 PackedBufferIndex, uint8* RESTRICT OffsetedCPUAddress) const
	{
		if (bSrcUniformPatchingResourceInitialized)
		{
			for (int i = 0; i < PackedBufferSegmentIndices[PackedBufferIndex].Num(); i++)
			{
				if (PackedBufferSegmentFrameIndex[PackedBufferIndex][i] == FrameNumber)
				{
					// ensure(PackedBufferSegmentSources[PackedBufferIndex][i] != NULL);
					const CrossCompiler::FUniformBufferCopyInfo& CopyInfo = EmulatedUBsCopyInfo[PackedBufferSegmentIndices[PackedBufferIndex][i]];
					FVulkanUniformBufferUploader::FUniformBufferPatchInfo PatchInfo;
					PatchInfo.DestBufferAddress = OffsetedCPUAddress + CopyInfo.DestOffsetInFloats * sizeof(float);
					PatchInfo.SizeInFloats = CopyInfo.SizeInFloats;
					PatchInfo.SourceOffsetInFloats = CopyInfo.SourceOffsetInFloats;
					PatchInfo.SourceBuffer = PackedBufferSegmentSources[PackedBufferIndex][i];
					PostBindingPatches.Push(PatchInfo);
				}
			}
		}
	}

	inline void MaskPackedBufferCopyInfoSegmentSource(const FVulkanUniformBuffer* SrcBuffer, const int32 PatchingFrameNumber, int EmulatedUBsCopyInfoIndex, int PackedBufferIndex)
	{
		int PackedSegIndex = CopyInfoRemapping[EmulatedUBsCopyInfoIndex];
		PackedBufferSegmentFrameIndex[PackedBufferIndex][PackedSegIndex] = PatchingFrameNumber;
		PackedBufferSegmentSources[PackedBufferIndex][PackedSegIndex] = SrcBuffer;

		// Validation
		// ensureMsgf(PackedSegIndex == PackedBufferSegmentIndices[PackedBufferIndex][PackedSegIndex], TEXT("Mismatch: PackedBufferSegmentIndices %d EmulatedUBsCopyInfoIndex %d"), PackedBufferSegmentIndices[PackedBufferIndex][PackedSegIndex], PackedSegIndex);
	}

protected:
	TArray<FPackedBuffer>									PackedUniformBuffers;

	// Copies to Shader Code Header (shaders may be deleted when we use this object again)
	TArray<CrossCompiler::FUniformBufferCopyInfo>			EmulatedUBsCopyInfo;
	TArray<uint32>											EmulatedUBsCopyRanges;

	// Pre-built static structure, Only created in lazy-initialization
	// Purpose: Translate EmulatedUBsCopyInfoIndex into PackedSegIndex, so given an EmulatedUBsCopyInfoIndex, we know where it goes in the PackedUniformBuffers
	// It has the same dimension with EmulatedUBsCopyInfo array
	// Example: if we have an CopyInfoIndex for EmulatedUBsCopyInfo[*], we can found it is corresponding segment postion 
	// SegIndex = CopyInfoRemapping[CopyInfoIndex] in the packedUniformBuffer
	// So  PackedBufferSegmentIndices[PackedBufferIndex][SegIndex] is pointing back to CopyInfoIndex
	//     PackedBufferSegmentSources[PackedBufferIndex][SegIndex] is pointing to the src uniform buffer
	//     PackedBufferSegmentFrameIndex[PackedBufferIndex][SegIndex] to the frameIndex last time latched
	// Note: here PackedBufferIndex is the PackedUniformBuffers ( CopyInfo.DestUBIndex )
	TArray<int>	CopyInfoRemapping;

	// Pre-built static structure, Only created in lazy-initialization from shader compiler meta data
	// Purpose: A representation of packedUnformBuffer segments, the returned value is used to index EmulatedUBsCopyInfo
	// Example: for a given PackedUniformBufferIndex, we can iterate through all packed segments by 
	// iterating PackedBufferSegmentIndices[PackedUniformBufferIndex][*], then
	// retIndex = PackedBufferSegmentIndices[PackedUniformBufferIndex][*], retIndex can be used to read  EmulatedUBsCopyInfo[retIndex]
	TArray<TArray<int>>	PackedBufferSegmentIndices;

	// Dynamic: works like a dirty mask for updateDescriptorSet to detect if we need post binding patching
	// Same dimension with PackedBufferSegmentIndices, they are PackedBuffer point of view data structure as well
	TArray<TArray<const FVulkanUniformBuffer*>>					PackedBufferSegmentSources;
	TArray<TArray<int32>>										PackedBufferSegmentFrameIndex;
	bool bSrcUniformPatchingResourceInitialized = false;

};

class FVulkanStagingBuffer : public FRHIStagingBuffer
{
	friend class FVulkanCommandListContext;
public:
	FVulkanStagingBuffer()
		: FRHIStagingBuffer()
	{
		check(!bIsLocked);
	}

	virtual ~FVulkanStagingBuffer();

	virtual void* Lock(uint32 Offset, uint32 NumBytes) final override;
	virtual void Unlock() final override;

private:
	VulkanRHI::FStagingBuffer* StagingBuffer = nullptr;
	uint32 QueuedOffset = 0;
	uint32 QueuedNumBytes = 0;
	// The staging buffer was allocated from this device.
	FVulkanDevice* Device;
};

class FVulkanGPUFence : public FRHIGPUFence
{
public:
	FVulkanGPUFence(FName InName)
		: FRHIGPUFence(InName)
	{
	}

	virtual void Clear() final override;
	virtual bool Poll() const final override;

	FVulkanCmdBuffer* GetCmdBuffer() const { return CmdBuffer; }

protected:
	FVulkanCmdBuffer*	CmdBuffer = nullptr;
	uint64				FenceSignaledCounter = 0;

	friend class FVulkanCommandListContext;
};

template<class T>
struct TVulkanResourceTraits
{
};
template<>
struct TVulkanResourceTraits<FRHIVertexDeclaration>
{
	typedef FVulkanVertexDeclaration TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIVertexShader>
{
	typedef FVulkanVertexShader TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIGeometryShader>
{
	typedef FVulkanGeometryShader TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIHullShader>
{
	typedef FVulkanHullShader TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIDomainShader>
{
	typedef FVulkanDomainShader TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIPixelShader>
{
	typedef FVulkanPixelShader TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIComputeShader>
{
	typedef FVulkanComputeShader TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHITexture3D>
{
	typedef FVulkanTexture3D TConcreteType;
};
//template<>
//struct TVulkanResourceTraits<FRHITexture>
//{
//	typedef FVulkanTexture TConcreteType;
//};
template<>
struct TVulkanResourceTraits<FRHITexture2D>
{
	typedef FVulkanTexture2D TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHITexture2DArray>
{
	typedef FVulkanTexture2DArray TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHITextureCube>
{
	typedef FVulkanTextureCube TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIRenderQuery>
{
	typedef FVulkanRenderQuery TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIUniformBuffer>
{
	typedef FVulkanUniformBuffer TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIIndexBuffer>
{
	typedef FVulkanIndexBuffer TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIStructuredBuffer>
{
	typedef FVulkanStructuredBuffer TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIVertexBuffer>
{
	typedef FVulkanVertexBuffer TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIShaderResourceView>
{
	typedef FVulkanShaderResourceView TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIUnorderedAccessView>
{
	typedef FVulkanUnorderedAccessView TConcreteType;
};

template<>
struct TVulkanResourceTraits<FRHISamplerState>
{
	typedef FVulkanSamplerState TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIRasterizerState>
{
	typedef FVulkanRasterizerState TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIDepthStencilState>
{
	typedef FVulkanDepthStencilState TConcreteType;
};
template<>
struct TVulkanResourceTraits<FRHIBlendState>
{
	typedef FVulkanBlendState TConcreteType;
};

template<>
struct TVulkanResourceTraits<FRHIBoundShaderState>
{
	typedef FVulkanBoundShaderState TConcreteType;
};

template<>
struct TVulkanResourceTraits<FRHIStagingBuffer>
{
	typedef FVulkanStagingBuffer TConcreteType;
};

template<>
struct TVulkanResourceTraits<FRHIGPUFence>
{
	typedef FVulkanGPUFence TConcreteType;
};

template<typename TRHIType>
static FORCEINLINE typename TVulkanResourceTraits<TRHIType>::TConcreteType* ResourceCast(TRHIType* Resource)
{
	return static_cast<typename TVulkanResourceTraits<TRHIType>::TConcreteType*>(Resource);
}

template<typename TRHIType>
static FORCEINLINE typename TVulkanResourceTraits<TRHIType>::TConcreteType* ResourceCast(const TRHIType* Resource)
{
	return static_cast<const typename TVulkanResourceTraits<TRHIType>::TConcreteType*>(Resource);
}
