// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanTexture.cpp: Vulkan texture RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanMemory.h"
#include "VulkanContext.h"
#include "VulkanPendingState.h"
#include "Containers/ResourceArray.h"
#include "VulkanLLM.h"
#include "VulkanBarriers.h"

int32 GVulkanSubmitOnTextureUnlock = 1;
static FAutoConsoleVariableRef CVarVulkanSubmitOnTextureUnlock(
	TEXT("r.Vulkan.SubmitOnTextureUnlock"),
	GVulkanSubmitOnTextureUnlock,
	TEXT("Whether to submit upload cmd buffer on each texture unlock.\n")
	TEXT(" 0: Do not submit\n")
	TEXT(" 1: Submit (default)"),
	ECVF_Default
);

int32 GVulkanDepthStencilForceStorageBit = 0;
static FAutoConsoleVariableRef CVarVulkanDepthStencilForceStorageBit(
	TEXT("r.Vulkan.DepthStencilForceStorageBit"),
	GVulkanDepthStencilForceStorageBit,
	TEXT("Whether to force Image Usage Storage on Depth (can disable framebuffer compression).\n")
	TEXT(" 0: Not enabled\n")
	TEXT(" 1: Enables override for IMAGE_USAGE_STORAGE"),
	ECVF_Default
);

extern int32 GVulkanLogDefrag;

static FCriticalSection GTextureMapLock;

struct FTextureLock
{
	FRHIResource* Texture;
	uint32 MipIndex;
	uint32 LayerIndex;

	FTextureLock(FRHIResource* InTexture, uint32 InMipIndex, uint32 InLayerIndex = 0)
		: Texture(InTexture)
		, MipIndex(InMipIndex)
		, LayerIndex(InLayerIndex)
	{
	}
};

#if VULKAN_USE_LLM
inline ELLMTagVulkan GetMemoryTagForTextureFlags(ETextureCreateFlags UEFlags)
{
	bool bRenderTarget = ((TexCreate_RenderTargetable | TexCreate_ResolveTargetable | TexCreate_DepthStencilTargetable) & UEFlags) != 0u;
	return bRenderTarget ? ELLMTagVulkan::VulkanRenderTargets : ELLMTagVulkan::VulkanTextures;
}
#endif // VULKAN_USE_LLM

inline bool operator == (const FTextureLock& A, const FTextureLock& B)
{
	return A.Texture == B.Texture && A.MipIndex == B.MipIndex && A.LayerIndex == B.LayerIndex;
}

inline uint32 GetTypeHash(const FTextureLock& Lock)
{
	return GetTypeHash(Lock.Texture) ^ (Lock.MipIndex << 16) ^ (Lock.LayerIndex << 8);
}

static TMap<FTextureLock, VulkanRHI::FStagingBuffer*> GPendingLockedBuffers;

static const VkImageTiling GVulkanViewTypeTilingMode[VK_IMAGE_VIEW_TYPE_RANGE_SIZE] =
{
	VK_IMAGE_TILING_LINEAR,		// VK_IMAGE_VIEW_TYPE_1D
	VK_IMAGE_TILING_OPTIMAL,	// VK_IMAGE_VIEW_TYPE_2D
	VK_IMAGE_TILING_OPTIMAL,	// VK_IMAGE_VIEW_TYPE_3D
	VK_IMAGE_TILING_OPTIMAL,	// VK_IMAGE_VIEW_TYPE_CUBE
	VK_IMAGE_TILING_LINEAR,		// VK_IMAGE_VIEW_TYPE_1D_ARRAY
	VK_IMAGE_TILING_OPTIMAL,	// VK_IMAGE_VIEW_TYPE_2D_ARRAY
	VK_IMAGE_TILING_OPTIMAL,	// VK_IMAGE_VIEW_TYPE_CUBE_ARRAY
};

static TStatId GetVulkanStatEnum(bool bIsCube, bool bIs3D, bool bIsRT)
{
#if STATS
	if (bIsRT == false)
	{
		// normal texture
		if (bIsCube)
		{
			return GET_STATID(STAT_TextureMemoryCube);
		}
		else if (bIs3D)
		{
			return GET_STATID(STAT_TextureMemory3D);
		}
		else
		{
			return GET_STATID(STAT_TextureMemory2D);
		}
	}
	else
	{
		// render target
		if (bIsCube)
		{
			return GET_STATID(STAT_RenderTargetMemoryCube);
		}
		else if (bIs3D)
		{
			return GET_STATID(STAT_RenderTargetMemory3D);
		}
		else
		{
			return GET_STATID(STAT_RenderTargetMemory2D);
		}
	}
#endif
	return TStatId();
}

static void UpdateVulkanTextureStats(int64 TextureSize, bool bIsCube, bool bIs3D, bool bIsRT)
{
	const int64 AlignedSize = (TextureSize > 0) ? Align(TextureSize, 1024) / 1024 : -(Align(-TextureSize, 1024) / 1024);
	if (bIsRT == false)
	{
		FPlatformAtomics::InterlockedAdd(&GCurrentTextureMemorySize, AlignedSize);
	}
	else
	{
		FPlatformAtomics::InterlockedAdd(&GCurrentRendertargetMemorySize, AlignedSize);
	}

	INC_MEMORY_STAT_BY_FName(GetVulkanStatEnum(bIsCube, bIs3D, bIsRT).GetName(), TextureSize);
}

static void VulkanTextureAllocated(uint64 Size, VkImageViewType ImageType, bool bIsRT)
{
	bool bIsCube = ImageType == VK_IMAGE_VIEW_TYPE_CUBE || ImageType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
	bool bIs3D = ImageType == VK_IMAGE_VIEW_TYPE_3D ;

	UpdateVulkanTextureStats(Size, bIsCube, bIs3D, bIsRT);
}

static void VulkanTextureDestroyed(uint64 Size, VkImageViewType ImageType, bool bIsRT)
{
	bool bIsCube = ImageType == VK_IMAGE_VIEW_TYPE_CUBE || ImageType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
	bool bIs3D = ImageType == VK_IMAGE_VIEW_TYPE_3D;

	UpdateVulkanTextureStats(-(int64)Size, bIsCube, bIs3D, bIsRT);
}

inline void FVulkanSurface::InternalLockWrite(FVulkanCommandListContext& Context, FVulkanSurface* Surface, const VkBufferImageCopy& Region, VulkanRHI::FStagingBuffer* StagingBuffer)
{
	FVulkanCmdBuffer* CmdBuffer = Context.GetCommandBufferManager()->GetUploadCmdBuffer();
	ensure(CmdBuffer->IsOutsideRenderPass());
	VkCommandBuffer StagingCommandBuffer = CmdBuffer->GetHandle();

	const VkImageSubresourceLayers& ImageSubresource = Region.imageSubresource;
	const VkImageSubresourceRange SubresourceRange = FVulkanPipelineBarrier::MakeSubresourceRange(ImageSubresource.aspectMask, ImageSubresource.mipLevel, 1, ImageSubresource.baseArrayLayer, ImageSubresource.layerCount);

	FVulkanImageLayout& TrackedTextureLayout = Context.GetLayoutManager().GetOrAddFullLayout(*Surface, VK_IMAGE_LAYOUT_UNDEFINED);

	// Transition the subresource layouts from their tracked state to the copy state
	FVulkanImageLayout TransferTextureLayout = TrackedTextureLayout;
	TransferTextureLayout.Set(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, SubresourceRange);
	{
		FVulkanPipelineBarrier Barrier;
		Barrier.AddImageLayoutTransition(Surface->Image, SubresourceRange.aspectMask, TrackedTextureLayout, TransferTextureLayout);
		Barrier.Execute(StagingCommandBuffer);
	}

	VulkanRHI::vkCmdCopyBufferToImage(StagingCommandBuffer, StagingBuffer->GetHandle(), Surface->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

	// Transition the subresource layouts from the copy state to a regular read state
	TrackedTextureLayout.Set(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, SubresourceRange);
	{
		FVulkanPipelineBarrier Barrier;
		Barrier.AddImageLayoutTransition(Surface->Image, SubresourceRange.aspectMask, TransferTextureLayout, TrackedTextureLayout);
		Barrier.Execute(StagingCommandBuffer);
	}

	Surface->Device->GetStagingManager().ReleaseBuffer(CmdBuffer, StagingBuffer);

	if (GVulkanSubmitOnTextureUnlock != 0)
	{
		Context.GetCommandBufferManager()->SubmitUploadCmdBuffer();
	}
}

void FVulkanSurface::ErrorInvalidViewType() const
{
	UE_LOG(LogVulkanRHI, Error, TEXT("Invalid ViewType %d"), (uint32)ViewType);
}


struct FRHICommandLockWriteTexture final : public FRHICommand<FRHICommandLockWriteTexture>
{
	FVulkanSurface* Surface;
	VkBufferImageCopy Region;
	VulkanRHI::FStagingBuffer* StagingBuffer;

	FRHICommandLockWriteTexture(FVulkanSurface* InSurface, const VkBufferImageCopy& InRegion, VulkanRHI::FStagingBuffer* InStagingBuffer)
		: Surface(InSurface)
		, Region(InRegion)
		, StagingBuffer(InStagingBuffer)
	{
	}

	void Execute(FRHICommandListBase& RHICmdList)
	{
		FVulkanSurface::InternalLockWrite(FVulkanCommandListContext::GetVulkanContext(RHICmdList.GetContext()), Surface, Region, StagingBuffer);
	}
};

void FVulkanSurface::GenerateImageCreateInfo(
	FImageCreateInfo& OutImageCreateInfo,
	FVulkanDevice& InDevice,
	VkImageViewType ResourceType,
	EPixelFormat InFormat,
	uint32 SizeX, uint32 SizeY, uint32 SizeZ,
	uint32 ArraySize,
	uint32 NumMips,
	uint32 NumSamples,
	ETextureCreateFlags UEFlags,
	VkFormat* OutStorageFormat,
	VkFormat* OutViewFormat,
	bool bForceLinearTexture)
{
	const VkPhysicalDeviceProperties& DeviceProperties = InDevice.GetDeviceProperties();
	const FPixelFormatInfo& FormatInfo = GPixelFormats[InFormat];
	VkFormat TextureFormat = (VkFormat)FormatInfo.PlatformFormat;

	if(UEFlags & TexCreate_CPUReadback)
	{
		bForceLinearTexture = true;
	}

	checkf(TextureFormat != VK_FORMAT_UNDEFINED, TEXT("PixelFormat %d, is not supported for images"), (int32)InFormat);
	VkImageCreateInfo& ImageCreateInfo = OutImageCreateInfo.ImageCreateInfo;
	ZeroVulkanStruct(ImageCreateInfo, VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);

	switch(ResourceType)
	{
	case VK_IMAGE_VIEW_TYPE_1D:
		ImageCreateInfo.imageType = VK_IMAGE_TYPE_1D;
		check(SizeX <= DeviceProperties.limits.maxImageDimension1D);
		break;
	case VK_IMAGE_VIEW_TYPE_CUBE:
	case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
		check(SizeX == SizeY);
		check(SizeX <= DeviceProperties.limits.maxImageDimensionCube);
		check(SizeY <= DeviceProperties.limits.maxImageDimensionCube);
		ImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		break;
	case VK_IMAGE_VIEW_TYPE_2D:
	case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
		check(SizeX <= DeviceProperties.limits.maxImageDimension2D);
		check(SizeY <= DeviceProperties.limits.maxImageDimension2D);
		ImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		break;
	case VK_IMAGE_VIEW_TYPE_3D:
		check(SizeY <= DeviceProperties.limits.maxImageDimension3D);
		ImageCreateInfo.imageType = VK_IMAGE_TYPE_3D;
		break;
	default:
		checkf(false, TEXT("Unhandled image type %d"), (int32)ResourceType);
		break;
	}

	VkFormat srgbFormat = UEToVkTextureFormat(InFormat, (UEFlags & TexCreate_SRGB) == TexCreate_SRGB);
	VkFormat nonSrgbFormat = UEToVkTextureFormat(InFormat, false);

	ImageCreateInfo.format = ((UEFlags & TexCreate_UAV) == 0) ? srgbFormat : nonSrgbFormat; 

	checkf(ImageCreateInfo.format != VK_FORMAT_UNDEFINED, TEXT("Pixel Format %d not defined!"), (int32)InFormat);
	if (OutViewFormat)
	{
		*OutViewFormat = srgbFormat;
	}
	if (OutStorageFormat)
	{
		*OutStorageFormat = nonSrgbFormat;
	}

	ImageCreateInfo.extent.width = SizeX;
	ImageCreateInfo.extent.height = SizeY;
	ImageCreateInfo.extent.depth = ResourceType == VK_IMAGE_VIEW_TYPE_3D ? SizeZ : 1;
	ImageCreateInfo.mipLevels = NumMips;
	uint32 LayerCount = (ResourceType == VK_IMAGE_VIEW_TYPE_CUBE || ResourceType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) ? 6 : 1;
	ImageCreateInfo.arrayLayers = ArraySize * LayerCount;
	check(ImageCreateInfo.arrayLayers <= DeviceProperties.limits.maxImageArrayLayers);

	ImageCreateInfo.flags = (ResourceType == VK_IMAGE_VIEW_TYPE_CUBE || ResourceType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;


	if((UEFlags & TexCreate_SRGB) == TexCreate_SRGB)
	{
		if(InDevice.GetOptionalExtensions().HasKHRImageFormatList)
		{
			VkImageFormatListCreateInfoKHR& ImageFormatListCreateInfo = OutImageCreateInfo.ImageFormatListCreateInfo;
			ZeroVulkanStruct(ImageFormatListCreateInfo, VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR);
			ImageFormatListCreateInfo.pNext = ImageCreateInfo.pNext;
			ImageCreateInfo.pNext = &ImageFormatListCreateInfo;
			ImageFormatListCreateInfo.viewFormatCount = 2;
			ImageFormatListCreateInfo.pViewFormats = OutImageCreateInfo.FormatsUsed;
			OutImageCreateInfo.FormatsUsed[0] = nonSrgbFormat;
			OutImageCreateInfo.FormatsUsed[1] = srgbFormat;
		}

		ImageCreateInfo.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
	}

#if VULKAN_SUPPORTS_MAINTENANCE_LAYER1
	if (InDevice.GetOptionalExtensions().HasKHRMaintenance1 && ImageCreateInfo.imageType == VK_IMAGE_TYPE_3D)
	{
		ImageCreateInfo.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT_KHR;
	}
#endif

	ImageCreateInfo.tiling = bForceLinearTexture ? VK_IMAGE_TILING_LINEAR : GVulkanViewTypeTilingMode[ResourceType];

	ImageCreateInfo.usage = 0;
	ImageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	//@TODO: should everything be created with the source bit?
	ImageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	ImageCreateInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

	if (UEFlags & TexCreate_Presentable)
	{
		ImageCreateInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;		
	}
	else if (UEFlags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable))
	{
		if ((UEFlags & TexCreate_InputAttachmentRead) == TexCreate_InputAttachmentRead)
		{
			ImageCreateInfo.usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
		}
		ImageCreateInfo.usage |= ((UEFlags & TexCreate_RenderTargetable) ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
		ImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		if (((UEFlags & TexCreate_Memoryless) == TexCreate_Memoryless) && InDevice.SupportsMemoryless())
		{
			ImageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
			// Remove the transfer and sampled bits, as they are incompatible with the transient bit.
			ImageCreateInfo.usage &= ~(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		}
	}
	else if (UEFlags & (TexCreate_DepthStencilResolveTarget))
	{
		ImageCreateInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		ImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	}
	else if (UEFlags & TexCreate_ResolveTargetable)
	{
		ImageCreateInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		ImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	}

	if (UEFlags & TexCreate_UAV)
	{
		//cannot have the storage bit on a memoryless texture
		ensure((UEFlags & TexCreate_Memoryless) == 0);
		ImageCreateInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
	}

#if VULKAN_SUPPORTS_EXTERNAL_MEMORY && PLATFORM_DESKTOP
	if (UEFlags & TexCreate_External)
	{
		VkExternalMemoryImageCreateInfoKHR& ExternalMemImageCreateInfo = OutImageCreateInfo.ExternalMemImageCreateInfo;
		ZeroVulkanStruct(ExternalMemImageCreateInfo, VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR);
#if PLATFORM_WINDOWS
		ExternalMemImageCreateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
#else
	    ExternalMemImageCreateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;
#endif
		ExternalMemImageCreateInfo.pNext = ImageCreateInfo.pNext;
    	ImageCreateInfo.pNext = &ExternalMemImageCreateInfo;
	}
#endif // VULKAN_SUPPORTS_EXTERNAL_MEMORY && PLATFORM_DESKTOP

	//#todo-rco: If using CONCURRENT, make sure to NOT do so on render targets as that kills DCC compression
	ImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ImageCreateInfo.queueFamilyIndexCount = 0;
	ImageCreateInfo.pQueueFamilyIndices = nullptr;

	if (ImageCreateInfo.tiling == VK_IMAGE_TILING_LINEAR && NumSamples > 1)
	{
		UE_LOG(LogVulkanRHI, Warning, TEXT("Not allowed to create Linear textures with %d samples, reverting to 1 sample"), NumSamples);
		NumSamples = 1;
	}

	switch (NumSamples)
	{
	case 1:
		ImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		break;
	case 2:
		ImageCreateInfo.samples = VK_SAMPLE_COUNT_2_BIT;
		break;
	case 4:
		ImageCreateInfo.samples = VK_SAMPLE_COUNT_4_BIT;
		break;
	case 8:
		ImageCreateInfo.samples = VK_SAMPLE_COUNT_8_BIT;
		break;
	case 16:
		ImageCreateInfo.samples = VK_SAMPLE_COUNT_16_BIT;
		break;
	case 32:
		ImageCreateInfo.samples = VK_SAMPLE_COUNT_32_BIT;
		break;
	case 64:
		ImageCreateInfo.samples = VK_SAMPLE_COUNT_64_BIT;
		break;
	default:
		checkf(0, TEXT("Unsupported number of samples %d"), NumSamples);
		break;
	}
		
	const VkFormatFeatureFlags FormatFlags = ImageCreateInfo.tiling == VK_IMAGE_TILING_LINEAR ? 
		InDevice.GetFormatProperties()[ImageCreateInfo.format].linearTilingFeatures : 
		InDevice.GetFormatProperties()[ImageCreateInfo.format].optimalTilingFeatures;

	if ((FormatFlags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0)
	{
		ensure((ImageCreateInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0);
		ImageCreateInfo.usage &= ~VK_IMAGE_USAGE_SAMPLED_BIT;
	}

	if ((FormatFlags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
	{
		ensure((ImageCreateInfo.usage & VK_IMAGE_USAGE_STORAGE_BIT) == 0);
		ImageCreateInfo.usage &= ~VK_IMAGE_USAGE_STORAGE_BIT;
	}

	if ((FormatFlags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0)
	{
		ensure((ImageCreateInfo.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0);
		ImageCreateInfo.usage &= ~VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	if ((FormatFlags & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0)
	{
		ensure((ImageCreateInfo.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0);
		ImageCreateInfo.usage &= ~VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}

	if ((FormatFlags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) == 0)
	{
		// this flag is used unconditionally, strip it without warnings 
		ImageCreateInfo.usage &= ~VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
		
	if ((FormatFlags & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) == 0)
	{
		// this flag is used unconditionally, strip it without warnings 
		ImageCreateInfo.usage &= ~VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}

	if ((UEFlags & TexCreate_DepthStencilTargetable) && GVulkanDepthStencilForceStorageBit)
	{
		ImageCreateInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
	}
}

struct FRHICommandSetInitialImageState final : public FRHICommand<FRHICommandSetInitialImageState>
{
	FVulkanSurface* Surface;
	VkImageLayout InitialLayout;
	bool bOnlyAddToLayoutManager;
	bool bClear;
	FClearValueBinding ClearValueBinding;

	FRHICommandSetInitialImageState(FVulkanSurface* InSurface, VkImageLayout InInitialLayout, bool bInOnlyAddToLayoutManager, bool bInClear, const FClearValueBinding& InClearValueBinding)
		: Surface(InSurface)
		, InitialLayout(InInitialLayout)
		, bOnlyAddToLayoutManager(bInOnlyAddToLayoutManager)
		, bClear(bInClear)
		, ClearValueBinding(InClearValueBinding)
	{
	}

	void Execute(FRHICommandListBase& CmdList)
	{
		FVulkanCommandListContext& Context = FVulkanCommandListContext::GetVulkanContext(CmdList.GetContext());
		
		if (bOnlyAddToLayoutManager)
		{
			Context.GetLayoutManager().GetOrAddFullLayout(*Surface, InitialLayout);
		}
		else
		{
			Surface->SetInitialImageState(Context, InitialLayout, bClear, ClearValueBinding);
		}
	}
};

struct FRHICommandOnDestroyImage final : public FRHICommand<FRHICommandOnDestroyImage>
{
	VkImage Image;
	FVulkanDevice* Device;
	bool bRenderTarget;

	FRHICommandOnDestroyImage(VkImage InImage, FVulkanDevice* InDevice, bool bInRenderTarget)
		: Image(InImage)
		, Device(InDevice)
		, bRenderTarget(bInRenderTarget)
	{
	}

	void Execute(FRHICommandListBase& RHICmdList)
	{
		Device->NotifyDeletedImage(Image, bRenderTarget);
	}
};

static VkImageLayout GetInitialLayoutFromRHIAcess(ERHIAccess RHIAccess, uint32 UEFlags)
{
	if (EnumHasAnyFlags(RHIAccess, ERHIAccess::RTV) || RHIAccess == ERHIAccess::Present)
	{
		return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	if (EnumHasAnyFlags(RHIAccess, ERHIAccess::DSVWrite))
	{
		return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}

	if (EnumHasAnyFlags(RHIAccess, ERHIAccess::DSVRead))
	{
		return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	}

	if (EnumHasAnyFlags(RHIAccess, ERHIAccess::SRVMask))
	{
		return (UEFlags & TexCreate_DepthStencilTargetable) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	if (EnumHasAnyFlags(RHIAccess, ERHIAccess::UAVMask))
	{
		return VK_IMAGE_LAYOUT_GENERAL;
	}

	switch (RHIAccess)
	{
		case ERHIAccess::Unknown:	return VK_IMAGE_LAYOUT_UNDEFINED;
		case ERHIAccess::CopySrc:	return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		case ERHIAccess::CopyDest:	return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	}

	checkf(false, TEXT("Invalid initial access %d"), RHIAccess);
	return VK_IMAGE_LAYOUT_UNDEFINED;
}

FVulkanSurface::FVulkanSurface(FVulkanDevice& InDevice, FVulkanEvictable* Owner, VkImageViewType ResourceType, EPixelFormat InFormat,
								uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 InArraySize, uint32 InNumMips,
								uint32 InNumSamples, ETextureCreateFlags InUEFlags, ERHIAccess InResourceState, const FRHIResourceCreateInfo& CreateInfo)
	: Device(&InDevice)
	, Image(VK_NULL_HANDLE)
	, StorageFormat(VK_FORMAT_UNDEFINED)
	, ViewFormat(VK_FORMAT_UNDEFINED)
	, Width(SizeX)
	, Height(SizeY)
	, Depth(SizeZ)
	, ArraySize(InArraySize)
	, PixelFormat(InFormat)
	, UEFlags(InUEFlags)
	, MemProps(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	, Tiling(VK_IMAGE_TILING_MAX_ENUM)	// Can be expanded to a per-platform definition
	, ViewType(ResourceType)
	, bIsImageOwner(true)
	, NumMips(InNumMips)
	, NumSamples(InNumSamples)
	, FullAspectMask(0)
	, PartialAspectMask(0)
	, CpuReadbackBuffer(nullptr) // for readback textures we use a staging buffer. this is because vulkan only requires implentations to support 1 mip level(which is useless), so we emulate using a buffer
{
	FImageCreateInfo ImageCreateInfo;
	FVulkanSurface::GenerateImageCreateInfo(ImageCreateInfo,
		InDevice, ResourceType,
		InFormat, Width, Height, Depth,
		ArraySize, NumMips, NumSamples, UEFlags,
		&StorageFormat, &ViewFormat);
	if(UEFlags & TexCreate_CPUReadback)
	{
		check(NumSamples == 1);	//not implemented
		check(Depth == 1);		//not implemented
		check(ArraySize == 1);	//not implemented
		CpuReadbackBuffer = new FVulkanCpuReadbackBuffer;
		uint32 Size = 0;
		for(uint32 Mip = 0; Mip < NumMips; ++Mip)
		{
			uint32 LocalSize;
			GetMipSize(Mip, LocalSize);
			CpuReadbackBuffer->MipOffsets[Mip] = Size;
			CpuReadbackBuffer->MipSize[Mip] = LocalSize;
			Size += LocalSize;
		}

		VkDevice VulkanDevice = InDevice.GetInstanceHandle();
		VkMemoryPropertyFlags BufferMemFlags = (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		
		VkBufferCreateInfo BufferCreateInfo;
		ZeroVulkanStruct(BufferCreateInfo, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
		BufferCreateInfo.size = Size;
		BufferCreateInfo.usage = BufferMemFlags;

		VERIFYVULKANRESULT(VulkanRHI::vkCreateBuffer(VulkanDevice, &BufferCreateInfo, VULKAN_CPU_ALLOCATOR, &CpuReadbackBuffer->Buffer));
		VulkanRHI::vkGetBufferMemoryRequirements(VulkanDevice, CpuReadbackBuffer->Buffer, &MemoryRequirements);
		// Set minimum alignment to 16 bytes, as some buffers are used with CPU SIMD instructions
		MemoryRequirements.alignment = FMath::Max<VkDeviceSize>(16, MemoryRequirements.alignment);
		if (!InDevice.GetMemoryManager().AllocateBufferMemory(Allocation, Owner, MemoryRequirements, BufferMemFlags, EVulkanAllocationMetaBufferStaging, false, __FILE__, __LINE__))
		{
			InDevice.GetMemoryManager().HandleOOM();
		}
		Allocation.BindBuffer(Device, CpuReadbackBuffer->Buffer);
		void* Memory = Allocation.GetMappedPointer(Device);
		FMemory::Memzero(Memory, MemoryRequirements.size);
		return;
	}

	VERIFYVULKANRESULT(VulkanRHI::vkCreateImage(InDevice.GetInstanceHandle(), &ImageCreateInfo.ImageCreateInfo, VULKAN_CPU_ALLOCATOR, &Image));

	// Fetch image size
	VulkanRHI::vkGetImageMemoryRequirements(InDevice.GetInstanceHandle(), Image, &MemoryRequirements);

	VULKAN_SET_DEBUG_NAME(InDevice, VK_OBJECT_TYPE_IMAGE, Image, TEXT("(FVulkanSurface*)0x%p"), this);

	FullAspectMask = VulkanRHI::GetAspectMaskFromUEFormat(PixelFormat, true, true);
	PartialAspectMask = VulkanRHI::GetAspectMaskFromUEFormat(PixelFormat, false, true);

	// If VK_IMAGE_TILING_OPTIMAL is specified,
	// memoryTypeBits in vkGetImageMemoryRequirements will become 1
	// which does not support VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT.
	if (ImageCreateInfo.ImageCreateInfo.tiling != VK_IMAGE_TILING_OPTIMAL)
	{
		MemProps |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	}

	const bool bRenderTarget = (UEFlags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable | TexCreate_ResolveTargetable)) != 0;
	const bool bUAV = (UEFlags & TexCreate_UAV) != 0;
	const bool bCPUReadback = (UEFlags & TexCreate_CPUReadback) != 0;
	const bool bDynamic = (UEFlags & TexCreate_Dynamic) != 0;
	const bool bExternal = (UEFlags & TexCreate_External) != 0;

	VkMemoryPropertyFlags MemoryFlags = bCPUReadback ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	bool bMemoryless = ((UEFlags & TexCreate_Memoryless) != 0) && InDevice.SupportsMemoryless();
	if (bMemoryless)
	{
		if (ensureMsgf(bRenderTarget, TEXT("Memoryless surfaces can only be used for render targets")) && ensureMsgf(!bCPUReadback, TEXT("Memoryless surfaces cannot be read back on CPU")))
		{
			MemoryFlags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
		}
		else
		{
			bMemoryless = false;
		}
	}
	if(Owner == nullptr)
	{
		Owner = this;
	}
	check(bRenderTarget || bUAV || Owner != nullptr);
	EVulkanAllocationMetaType MetaType = (bRenderTarget||bUAV) ? EVulkanAllocationMetaImageRenderTarget : EVulkanAllocationMetaImageOther;
#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
	extern int32 GVulkanEnableDedicatedImageMemory;
	// Per https://developer.nvidia.com/what%E2%80%99s-your-vulkan-memory-type
	VkDeviceSize SizeToBeConsideredForDedicated = 12 * 1024 * 1024;
	if ((bRenderTarget || MemoryRequirements.size >= SizeToBeConsideredForDedicated) && !bMemoryless && InDevice.GetOptionalExtensions().HasKHRDedicatedAllocation && GVulkanEnableDedicatedImageMemory)
	{
		if(!InDevice.GetMemoryManager().AllocateDedicatedImageMemory(Allocation, Owner, Image, MemoryRequirements, MemoryFlags, MetaType, bExternal, __FILE__, __LINE__))
		{
			checkNoEntry();
		}
	}
	else
#endif
	{
		if(!InDevice.GetMemoryManager().AllocateImageMemory(Allocation, Owner, MemoryRequirements, MemoryFlags, MetaType, bExternal, __FILE__, __LINE__))
		{
			checkNoEntry();
		}
	}
	Allocation.BindImage(Device, Image);

	// update rhi stats
	VulkanTextureAllocated(MemoryRequirements.size, ResourceType, bRenderTarget);

	Tiling = ImageCreateInfo.ImageCreateInfo.tiling;
	check(Tiling == VK_IMAGE_TILING_LINEAR || Tiling == VK_IMAGE_TILING_OPTIMAL);

	VkImageLayout InitialLayout = GetInitialLayoutFromRHIAcess(InResourceState, UEFlags);
	const bool bDoInitialClear = (ImageCreateInfo.ImageCreateInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT) && (UEFlags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable));

	if (InitialLayout != VK_IMAGE_LAYOUT_UNDEFINED || bDoInitialClear)
	{
		FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		if (!IsInRenderingThread() || (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread()))
		{
			SetInitialImageState(Device->GetImmediateContext(), InitialLayout, bDoInitialClear, CreateInfo.ClearValueBinding);
		}
		else
		{
			check(IsInRenderingThread());
			ALLOC_COMMAND_CL(RHICmdList, FRHICommandSetInitialImageState)(this, InitialLayout, false, bDoInitialClear, CreateInfo.ClearValueBinding);
		}
	}
}

void FVulkanSurface::InternalMoveSurface(FVulkanDevice& InDevice, FVulkanCommandListContext& Context, FVulkanAllocation& DestAllocation, bool bSwapAllocation)
{
	FImageCreateInfo ImageCreateInfo;
	FVulkanSurface::GenerateImageCreateInfo(ImageCreateInfo,
		InDevice, ViewType,
		PixelFormat, Width, Height, Depth,
		ArraySize, NumMips, NumSamples, UEFlags,
		&StorageFormat, &ViewFormat);

	VkImage MovedImage;
	VERIFYVULKANRESULT(VulkanRHI::vkCreateImage(InDevice.GetInstanceHandle(), &ImageCreateInfo.ImageCreateInfo, VULKAN_CPU_ALLOCATOR, &MovedImage));
	checkf(Tiling == ImageCreateInfo.ImageCreateInfo.tiling, TEXT("Move has changed image tiling:  before [%d] != after [%d]"), (int32)Tiling, (int32)ImageCreateInfo.ImageCreateInfo.tiling);

	const bool bRenderTarget = EnumHasAnyFlags(UEFlags, TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable | TexCreate_ResolveTargetable);
	const bool bCPUReadback = EnumHasAnyFlags(UEFlags, TexCreate_CPUReadback);
	const bool bMemoryless = EnumHasAnyFlags(UEFlags, TexCreate_Memoryless);
	checkf(!bCPUReadback, TEXT("Move of CPUReadback surfaces not currently supported.   UEFlags=0x%x"), (int32)UEFlags);
	checkf(!bMemoryless || !InDevice.SupportsMemoryless(), TEXT("Move of Memoryless surfaces not currently supported.   UEFlags=0x%x"), (int32)UEFlags);

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	// This shouldn't change
	VkMemoryRequirements MovedMemReqs;
	VulkanRHI::vkGetImageMemoryRequirements(InDevice.GetInstanceHandle(), MovedImage, &MovedMemReqs);
	checkf((MemoryRequirements.alignment == MovedMemReqs.alignment), TEXT("Memory requirements changed: alignment %d -> %d"), (int32)MemoryRequirements.alignment, (int32)MovedMemReqs.alignment);
	checkf((MemoryRequirements.size == MovedMemReqs.size), TEXT("Memory requirements changed: size %d -> %d"), (int32)MemoryRequirements.size, (int32)MovedMemReqs.size);
	checkf((MemoryRequirements.memoryTypeBits == MovedMemReqs.memoryTypeBits), TEXT("Memory requirements changed: memoryTypeBits %d -> %d"), (int32)MemoryRequirements.memoryTypeBits, (int32)MovedMemReqs.memoryTypeBits);
#endif // UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT

	DestAllocation.BindImage(&InDevice, MovedImage);

	// Copy Original -> Moved
	FVulkanCmdBuffer* CmdBuffer = Context.GetCommandBufferManager()->GetActiveCmdBuffer();
	VkCommandBuffer VkCmdBuffer = CmdBuffer->GetHandle();
	ensure(CmdBuffer->IsOutsideRenderPass());

	const uint32 NumberOfArrayLevels = GetNumberOfArrayLevels();
	FVulkanImageLayout* MovedLayout = &Context.GetLayoutManager().FindOrAddFullLayoutRW(MovedImage, VK_IMAGE_LAYOUT_UNDEFINED, GetNumMips(), NumberOfArrayLevels);
	FVulkanImageLayout* OriginalLayout = Context.GetLayoutManager().GetFullLayout(Image);
	// Account for map resize, should rarely happen...
	if (OriginalLayout == nullptr)
	{
		OriginalLayout = &Context.GetLayoutManager().GetOrAddFullLayout(*this, VK_IMAGE_LAYOUT_UNDEFINED);
		MovedLayout = &Context.GetLayoutManager().GetFullLayoutChecked(MovedImage);
	}

	checkf((OriginalLayout->NumMips == GetNumMips()), TEXT("NumMips reported by LayoutManager (%d) differs from surface (%d)"), OriginalLayout->NumMips, GetNumMips());
	checkf((OriginalLayout->NumLayers == NumberOfArrayLevels), TEXT("NumLayers reported by LayoutManager (%d) differs from surface (%d)"), OriginalLayout->NumLayers, NumberOfArrayLevels);
	{
		// Transition to copying layouts
		{
			FVulkanPipelineBarrier Barrier;
			Barrier.AddImageLayoutTransition(Image, FullAspectMask, *OriginalLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			Barrier.AddImageLayoutTransition(MovedImage, FullAspectMask, *MovedLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
			Barrier.Execute(VkCmdBuffer);
		}
		{
			VkImageCopy Regions[MAX_TEXTURE_MIP_COUNT];
			check(NumMips <= MAX_TEXTURE_MIP_COUNT);
			FMemory::Memzero(Regions);
			for (uint32 i = 0; i < NumMips; ++i)
			{
				VkImageCopy& Region = Regions[i];
				Region.extent.width = FMath::Max(1u, Width >> i);
				Region.extent.height = FMath::Max(1u, Height >> i);
				Region.extent.depth = FMath::Max(1u, Depth >> i);
				Region.srcSubresource.aspectMask = FullAspectMask;
				Region.dstSubresource.aspectMask = FullAspectMask;
				Region.srcSubresource.baseArrayLayer = 0;
				Region.dstSubresource.baseArrayLayer = 0;
				Region.srcSubresource.layerCount = NumberOfArrayLevels;
				Region.dstSubresource.layerCount = NumberOfArrayLevels;
				Region.srcSubresource.mipLevel = i;
				Region.dstSubresource.mipLevel = i;
			}
			VulkanRHI::vkCmdCopyImage(VkCmdBuffer,
				Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				MovedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				NumMips, &Regions[0]);
		}

		// Put the destination image in exactly the same layout the original image was
		{
			FVulkanPipelineBarrier Barrier;
			Barrier.AddImageLayoutTransition(MovedImage, FullAspectMask, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, *OriginalLayout);
			Barrier.Execute(VkCmdBuffer);
		}

		// Update the tracked layouts
		*MovedLayout = *OriginalLayout;
		OriginalLayout->Set(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, FVulkanPipelineBarrier::MakeSubresourceRange(FullAspectMask));
	}

	{
		check(Image != VK_NULL_HANDLE);
		InDevice.NotifyDeletedImage(Image, bRenderTarget);
		InDevice.GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::Image, Image);
		if (GVulkanLogDefrag)
		{
			FGenericPlatformMisc::LowLevelOutputDebugStringf(TEXT("** MOVE IMAGE %p -> %p\n"), Image, MovedImage);
		}
	}

	Image = MovedImage;

	if (bSwapAllocation)
	{
		const uint64 Size = GetMemorySize();
		VulkanTextureDestroyed(Size, ViewType, bRenderTarget);

		Allocation.Swap(DestAllocation);
	}
}

void FVulkanSurface::MoveSurface(FVulkanDevice& InDevice, FVulkanCommandListContext& Context, FVulkanAllocation& NewAllocation)
{
	const bool bRenderTarget = EnumHasAnyFlags(UEFlags, TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable | TexCreate_ResolveTargetable);
	const bool bUAV = EnumHasAnyFlags(UEFlags, TexCreate_UAV);
	checkf(bRenderTarget || bUAV, TEXT("Surface must be a RenderTarget or a UAV in order to be moved.  UEFlags=0x%x"), (int32)UEFlags);
	checkf(Tiling == VK_IMAGE_TILING_OPTIMAL, TEXT("Tiling [%d] is not supported for move, only VK_IMAGE_TILING_OPTIMAL"), (int32)Tiling);

	InternalMoveSurface(InDevice, Context, NewAllocation, true);
}


void FVulkanSurface::OnFullDefrag(FVulkanDevice& InDevice, FVulkanCommandListContext& Context, uint32 NewOffset)
{
	const bool bRenderTarget = EnumHasAnyFlags(UEFlags, TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable | TexCreate_ResolveTargetable);
	const bool bUAV = EnumHasAnyFlags(UEFlags, TexCreate_UAV);
	checkf(bRenderTarget || bUAV, TEXT("Surface must be a RenderTarget or a UAV in order to be defragged.  UEFlags=0x%x"), (int32)UEFlags);
	checkf(Tiling == VK_IMAGE_TILING_OPTIMAL, TEXT("Tiling [%d] is not supported for defrag, only VK_IMAGE_TILING_OPTIMAL"), (int32)Tiling);

	Allocation.Offset = NewOffset;
	InternalMoveSurface(InDevice, Context, Allocation, false);

	//note: this exploits that the unmoved image is still bound to the old allocation, which is freed by the caller in this case.
}


void FVulkanSurface::EvictSurface(FVulkanDevice& InDevice)
{
	check(0 == CpuReadbackBuffer);
	checkf(MemProps == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, TEXT("Can't evict surface that isn't device local.  MemoryProperties=%d"), (int32)MemProps);
	checkf(VulkanRHI::GetAspectMaskFromUEFormat(PixelFormat, true, true) == FullAspectMask, TEXT("FullAspectMask (%d) does not match with PixelFormat (%d)"), (int32)FullAspectMask, (int32)PixelFormat);
	checkf(VulkanRHI::GetAspectMaskFromUEFormat(PixelFormat, false, true) == PartialAspectMask, TEXT("PartialAspectMask (%d) does not match with PixelFormat (%d)"), (int32)PartialAspectMask, (int32)PixelFormat);

	const bool bRenderTarget = EnumHasAnyFlags(UEFlags, TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable | TexCreate_ResolveTargetable);
	const bool bUAV = EnumHasAnyFlags(UEFlags, TexCreate_UAV);
	//none of this is supported for eviction
	checkf(!bRenderTarget, TEXT("RenderTargets do not support evict."));
	checkf(!bUAV, TEXT("UAV do not support evict."));

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	FVulkanCommandListContext& Context = (FVulkanCommandListContext&)RHICmdList.GetContext().GetLowestLevelContext();

	MemProps = InDevice.GetDeviceMemoryManager().GetEvictedMemoryProperties();

	FVulkanAllocation HostAllocation;
	const EVulkanAllocationMetaType MetaType = EVulkanAllocationMetaImageOther;
	if (!InDevice.GetMemoryManager().AllocateImageMemory(HostAllocation, this, MemoryRequirements, MemProps, MetaType, false, __FILE__, __LINE__))
	{
		InDevice.GetMemoryManager().HandleOOM();
		checkNoEntry();
	}
	VulkanTextureAllocated(MemoryRequirements.size, ViewType, bRenderTarget);

	InternalMoveSurface(InDevice, Context, HostAllocation, true);

	// Since the allocations were swapped, HostAllocation now contains the original allocation to be freed
	Device->GetMemoryManager().FreeVulkanAllocation(HostAllocation);

	VULKAN_SET_DEBUG_NAME(InDevice, VK_OBJECT_TYPE_IMAGE, Image, TEXT("(FVulkanSurface*)0x%p [hostimage]"), this);
}

// This is usually used for the framebuffer image
FVulkanSurface::FVulkanSurface(FVulkanDevice& InDevice, VkImageViewType ResourceType, EPixelFormat InFormat,
								uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 InArraySize, uint32 InNumMips, uint32 InNumSamples,
								VkImage InImage, ETextureCreateFlags InUEFlags, const FRHIResourceCreateInfo& CreateInfo)
	: Device(&InDevice)
	, Image(InImage)
	, StorageFormat(VK_FORMAT_UNDEFINED)
	, ViewFormat(VK_FORMAT_UNDEFINED)
	, Width(SizeX)
	, Height(SizeY)
	, Depth(SizeZ)
	, ArraySize(InArraySize)
	, PixelFormat(InFormat)
	, UEFlags(InUEFlags)
	, MemProps(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	, Tiling(VK_IMAGE_TILING_MAX_ENUM)	// Can be expanded to a per-platform definition
	, ViewType(ResourceType)
	, bIsImageOwner(false)
	, NumMips(InNumMips)
	, NumSamples(InNumSamples)
	, FullAspectMask(0)
	, PartialAspectMask(0)
	, CpuReadbackBuffer(nullptr)
{
	StorageFormat = UEToVkTextureFormat(PixelFormat, false);

	checkf(PixelFormat == PF_Unknown || StorageFormat != VK_FORMAT_UNDEFINED, TEXT("PixelFormat %d, is not supported for images"), (int32)PixelFormat);

	ViewFormat = UEToVkTextureFormat(PixelFormat, (UEFlags & TexCreate_SRGB) == TexCreate_SRGB);
	FullAspectMask = VulkanRHI::GetAspectMaskFromUEFormat(PixelFormat, true, true);
	PartialAspectMask = VulkanRHI::GetAspectMaskFromUEFormat(PixelFormat, false, true);

	// Purely informative patching, we know that "TexCreate_Presentable" uses optimal tiling
	if ((UEFlags & TexCreate_Presentable) == TexCreate_Presentable && GetTiling() == VK_IMAGE_TILING_MAX_ENUM)
	{
		Tiling = VK_IMAGE_TILING_OPTIMAL;
	}

	if (Image != VK_NULL_HANDLE)
	{
#if VULKAN_ENABLE_WRAP_LAYER
		FImageCreateInfo ImageCreateInfo;
		FVulkanSurface::GenerateImageCreateInfo(
			ImageCreateInfo,
			InDevice, ResourceType,
			InFormat, SizeX, SizeY, SizeZ,
			ArraySize, NumMips, NumSamples, UEFlags,
			&StorageFormat, &ViewFormat);
		FWrapLayer::CreateImage(VK_SUCCESS, InDevice.GetInstanceHandle(), &ImageCreateInfo.ImageCreateInfo, &Image);
#endif
		VULKAN_SET_DEBUG_NAME(InDevice, VK_OBJECT_TYPE_IMAGE, Image, TEXT("(FVulkanSurface*)0x%p"), this);

		VkImageLayout InitialLayout;
		bool bOnlyAddToLayoutManager, bDoInitialClear;
		if (UEFlags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable))
		{
			InitialLayout = (UEFlags & TexCreate_DepthStencilTargetable) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			bOnlyAddToLayoutManager = false;
			bDoInitialClear = true;
		}
		else if (UEFlags & TexCreate_Foveation)
		{
			// If it's a foveation texture, do not clear but add to layoutmgr, and set correct foveation layout. 
			InitialLayout = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;
			bOnlyAddToLayoutManager = true;
			bDoInitialClear = false;
		}
		else
		{
			// If we haven't seen this image before, we assume it's an SRV (VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) and the call below
			// tells the layout manager about it. If we've seen it before, the call won't do anything, since the manager already knows the layout.
			InitialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			bOnlyAddToLayoutManager = true;
			bDoInitialClear = false;
		}

		FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		if (!IsInRenderingThread() || (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread()))
		{
			if (bOnlyAddToLayoutManager)
			{
				InDevice.GetImmediateContext().GetLayoutManager().GetOrAddFullLayout(*this, InitialLayout);
			}
			else
			{
				SetInitialImageState(InDevice.GetImmediateContext(), InitialLayout, true, CreateInfo.ClearValueBinding);
			}
		}
		else
		{
			check(IsInRenderingThread());
			ALLOC_COMMAND_CL(RHICmdList, FRHICommandSetInitialImageState)(this, InitialLayout, bOnlyAddToLayoutManager, bDoInitialClear, CreateInfo.ClearValueBinding);
		}
	}
}

FVulkanSurface::~FVulkanSurface()
{
	Destroy();
}

void FVulkanSurface::Destroy()
{
	// An image can be instances.
	// - Instances VkImage has "bIsImageOwner" set to "false".
	// - Owner of VkImage has "bIsImageOwner" set to "true".
	if(CpuReadbackBuffer)
	{
		Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::Buffer, CpuReadbackBuffer->Buffer);
		Device->GetMemoryManager().FreeVulkanAllocation(Allocation);
		delete CpuReadbackBuffer;

	}
	else if (bIsImageOwner)
	{
		const bool bRenderTarget = (UEFlags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable | TexCreate_ResolveTargetable)) != 0;
		FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		if (!IsInRenderingThread() || (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread()))
		{
			Device->NotifyDeletedImage(Image, bRenderTarget);
		}
		else
		{
			check(IsInRenderingThread());
			new (RHICmdList.AllocCommand<FRHICommandOnDestroyImage>()) FRHICommandOnDestroyImage(Image, Device, bRenderTarget);
		}

		bIsImageOwner = false;

		uint64 Size = 0;

		if (Image != VK_NULL_HANDLE)
		{
			Size = GetMemorySize();
			Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::Image, Image);
			Device->GetMemoryManager().FreeVulkanAllocation(Allocation);
			Image = VK_NULL_HANDLE;
		}

		VulkanTextureDestroyed(Size, ViewType, bRenderTarget);
	}
}


void FVulkanSurface::InvalidateMappedMemory()
{
	Allocation.InvalidateMappedMemory(Device);

}
void* FVulkanSurface::GetMappedPointer()
{
	return Allocation.GetMappedPointer(Device);
}



VkDeviceMemory FVulkanSurface::GetAllocationHandle() const
{
	if (Allocation.IsValid())
	{
		return Allocation.GetDeviceMemoryHandle(Device);
	}
	else
	{
		return VK_NULL_HANDLE;
	}
}

uint64 FVulkanSurface::GetAllocationOffset() const
{
	if (Allocation.IsValid())
	{
		return Allocation.Offset;
	}
	else
	{
		return 0;
	}
}


#if 0
void* FVulkanSurface::Lock(uint32 MipIndex, uint32 ArrayIndex, EResourceLockMode LockMode, uint32& DestStride)
{
	DestStride = 0;

	check((MemProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0 ? Tiling == VK_IMAGE_TILING_OPTIMAL : Tiling == VK_IMAGE_TILING_LINEAR);

	// Verify all buffers are unmapped
	auto& Data = MipMapMapping.FindOrAdd(MipIndex);
	checkf(Data == nullptr, TEXT("The buffer needs to be unmapped, before it can be mapped"));

	// Get the layout of the subresource
	VkImageSubresource ImageSubResource;
	FMemory::Memzero(ImageSubResource);

	ImageSubResource.aspectMask = GetAspectMask();
	ImageSubResource.mipLevel = MipIndex;
	ImageSubResource.arrayLayer = ArrayIndex;

	// Get buffer size
	// Pitch can be only retrieved from linear textures.
	VkSubresourceLayout SubResourceLayout;
	VulkanRHI::vkGetImageSubresourceLayout(Device->GetInstanceHandle(), Image, &ImageSubResource, &SubResourceLayout);

	// Set linear row-pitch
	GetMipStride(MipIndex, DestStride);

	if(Tiling == VK_IMAGE_TILING_LINEAR)
	{
		// Verify pitch if linear
		check(DestStride == SubResourceLayout.rowPitch);

		// Map buffer to a pointer
		Data = Allocation->Map(SubResourceLayout.size, SubResourceLayout.offset);
		return Data;
	}

	// From here on, the code is dedicated to optimal textures

	// Verify all buffers are unmapped
	TRefCountPtr<FVulkanBuffer>& LinearBuffer = MipMapBuffer.FindOrAdd(MipIndex);
	checkf(LinearBuffer == nullptr, TEXT("The buffer needs to be unmapped, before it can be mapped"));

	// Create intermediate buffer which is going to be used to perform buffer to image copy
	// The copy buffer is always one face and single mip level.
	const uint32 Layers = 1;
	const uint32 Bytes = SubResourceLayout.size * Layers;

	VkBufferUsageFlags Usage = 0;
	Usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VkMemoryPropertyFlags Flags = 0;
	Flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT;

	LinearBuffer = new FVulkanBuffer(*Device, Bytes, Usage, Flags, false, __FILE__, __LINE__);

	void* DataPtr = LinearBuffer->Lock(Bytes);
	check(DataPtr);

	return DataPtr;
}

void FVulkanSurface::Unlock(uint32 MipIndex, uint32 ArrayIndex)
{
	check((MemProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0 ? Tiling == VK_IMAGE_TILING_OPTIMAL : Tiling == VK_IMAGE_TILING_LINEAR);

	if(Tiling == VK_IMAGE_TILING_LINEAR)
	{
		void*& Data = MipMapMapping.FindOrAdd(MipIndex);
		checkf(Data != nullptr, TEXT("The buffer needs to be mapped, before it can be unmapped"));

		Allocation->Unmap();
		Data = nullptr;
		return;
	}

	TRefCountPtr<FVulkanBuffer>& LinearBuffer = MipMapBuffer.FindOrAdd(MipIndex);
	checkf(LinearBuffer != nullptr, TEXT("The buffer needs to be mapped, before it can be unmapped"));
	LinearBuffer->Unlock();

	VkImageSubresource ImageSubResource;
	FMemory::Memzero(ImageSubResource);
	ImageSubResource.aspectMask = GetAspectMask();
	ImageSubResource.mipLevel = MipIndex;
	ImageSubResource.arrayLayer = ArrayIndex;

	VkSubresourceLayout SubResourceLayout;

	VulkanRHI::vkGetImageSubresourceLayout(Device->GetInstanceHandle(), Image, &ImageSubResource, &SubResourceLayout);

	VkBufferImageCopy Region;
	FMemory::Memzero(Region);
	Region.bufferOffset = 0;
	Region.bufferRowLength = (Width >> MipIndex);
	Region.bufferImageHeight = (Height >> MipIndex);

	// The data/image is always parsed per one face.
	// Meaning that a cubemap will have atleast 6 locks/unlocks
	Region.imageSubresource.baseArrayLayer = ArrayIndex;	// Layer/face copy destination
	Region.imageSubresource.layerCount = 1;	// Indicates number of arrays in the buffer, this is also the amount of "faces/layers" to be copied
	Region.imageSubresource.aspectMask = GetAspectMask();
	Region.imageSubresource.mipLevel = MipIndex;

	Region.imageExtent.width = Region.bufferRowLength;
	Region.imageExtent.height = Region.bufferImageHeight;
	Region.imageExtent.depth = 1;

	LinearBuffer->CopyTo(*this, Region, nullptr);

	// Release buffer
	LinearBuffer = nullptr;
}
#endif

void FVulkanSurface::GetMipStride(uint32 MipIndex, uint32& Stride)
{
	// Calculate the width of the MipMap.
	const uint32 BlockSizeX = GPixelFormats[PixelFormat].BlockSizeX;
	const uint32 MipSizeX = FMath::Max(Width >> MipIndex, BlockSizeX);
	uint32 NumBlocksX = (MipSizeX + BlockSizeX - 1) / BlockSizeX;

	if (PixelFormat == PF_PVRTC2 || PixelFormat == PF_PVRTC4)
	{
		// PVRTC has minimum 2 blocks width
		NumBlocksX = FMath::Max<uint32>(NumBlocksX, 2);
	}

	const uint32 BlockBytes = GPixelFormats[PixelFormat].BlockBytes;

	Stride = NumBlocksX * BlockBytes;
}

void FVulkanSurface::GetMipOffset(uint32 MipIndex, uint32& Offset)
{
	uint32 offset = Offset = 0;
	for(uint32 i = 0; i < MipIndex; i++)
	{
		GetMipSize(i, offset);
		Offset += offset;
	}
}

void FVulkanSurface::GetMipSize(uint32 MipIndex, uint32& MipBytes)
{
	// Calculate the dimensions of mip-map level.
	const uint32 BlockSizeX = GPixelFormats[PixelFormat].BlockSizeX;
	const uint32 BlockSizeY = GPixelFormats[PixelFormat].BlockSizeY;
	const uint32 BlockBytes = GPixelFormats[PixelFormat].BlockBytes;
	const uint32 MipSizeX = FMath::Max(Width >> MipIndex, BlockSizeX);
	const uint32 MipSizeY = FMath::Max(Height >> MipIndex, BlockSizeY);
	uint32 NumBlocksX = (MipSizeX + BlockSizeX - 1) / BlockSizeX;
	uint32 NumBlocksY = (MipSizeY + BlockSizeY - 1) / BlockSizeY;

	if (PixelFormat == PF_PVRTC2 || PixelFormat == PF_PVRTC4)
	{
		// PVRTC has minimum 2 blocks width and height
		NumBlocksX = FMath::Max<uint32>(NumBlocksX, 2);
		NumBlocksY = FMath::Max<uint32>(NumBlocksY, 2);
	}

	// Size in bytes
	MipBytes = NumBlocksX * NumBlocksY * BlockBytes;
/*
#if VULKAN_HAS_DEBUGGING_ENABLED
	VkImageSubresource SubResource;
	FMemory::Memzero(SubResource);
	SubResource.aspectMask = FullAspectMask;
	SubResource.mipLevel = MipIndex;
	//SubResource.arrayLayer = 0;
	VkSubresourceLayout OutLayout;
	VulkanRHI::vkGetImageSubresourceLayout(Device->GetInstanceHandle(), Image, &SubResource, &OutLayout);
	ensure(MipBytes >= OutLayout.size);
#endif
*/
}

void FVulkanSurface::SetInitialImageState(FVulkanCommandListContext& Context, VkImageLayout InitialLayout, bool bClear, const FClearValueBinding& ClearValueBinding)
{
	// Can't use TransferQueue as Vulkan requires that queue to also have Gfx or Compute capabilities...
	//#todo-rco: This function is only used during loading currently, if used for regular RHIClear then use the ActiveCmdBuffer
	FVulkanCmdBuffer* CmdBuffer = Context.GetCommandBufferManager()->GetUploadCmdBuffer();
	ensure(CmdBuffer->IsOutsideRenderPass());

	VkImageSubresourceRange SubresourceRange = FVulkanPipelineBarrier::MakeSubresourceRange(FullAspectMask);

	VkImageLayout CurrentLayout;
	if (bClear)
	{
		VulkanSetImageLayout(CmdBuffer->GetHandle(), Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, SubresourceRange);

		if (FullAspectMask == VK_IMAGE_ASPECT_COLOR_BIT)
		{
			VkClearColorValue Color;
			FMemory::Memzero(Color);
			Color.float32[0] = ClearValueBinding.Value.Color[0];
			Color.float32[1] = ClearValueBinding.Value.Color[1];
			Color.float32[2] = ClearValueBinding.Value.Color[2];
			Color.float32[3] = ClearValueBinding.Value.Color[3];

			VulkanRHI::vkCmdClearColorImage(CmdBuffer->GetHandle(), Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &Color, 1, &SubresourceRange);
		}
		else
		{
			check(IsDepthOrStencilAspect());
			VkClearDepthStencilValue Value;
			FMemory::Memzero(Value);
			Value.depth = ClearValueBinding.Value.DSValue.Depth;
			Value.stencil = ClearValueBinding.Value.DSValue.Stencil;

			VulkanRHI::vkCmdClearDepthStencilImage(CmdBuffer->GetHandle(), Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &Value, 1, &SubresourceRange);
		}

		CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	}
	else
	{
		CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	if (InitialLayout != CurrentLayout && InitialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
	{
		VulkanSetImageLayout(CmdBuffer->GetHandle(), Image, CurrentLayout, InitialLayout, SubresourceRange);
	}

	FVulkanImageLayout& FullLayout = Context.GetLayoutManager().GetOrAddFullLayout(*this, InitialLayout);
	checkSlow(FullLayout.AreAllSubresourcesSameLayout());
	FullLayout.MainLayout = InitialLayout;
}

/*-----------------------------------------------------------------------------
	Texture allocator support.
-----------------------------------------------------------------------------*/

void FVulkanDynamicRHI::RHIGetTextureMemoryStats(FTextureMemoryStats& OutStats)
{
	check(Device);
	const uint64 TotalGPUMemory = Device->GetDeviceMemoryManager().GetTotalMemory(true);
	const uint64 TotalCPUMemory = Device->GetDeviceMemoryManager().GetTotalMemory(false);

	OutStats.DedicatedVideoMemory = TotalGPUMemory;
	OutStats.DedicatedSystemMemory = TotalCPUMemory;
	OutStats.SharedSystemMemory = -1;
	OutStats.TotalGraphicsMemory = TotalGPUMemory ? TotalGPUMemory : -1;

	OutStats.AllocatedMemorySize = int64(GCurrentTextureMemorySize) * 1024;
	OutStats.LargestContiguousAllocation = OutStats.AllocatedMemorySize;
	OutStats.TexturePoolSize = GTexturePoolSize;
	OutStats.PendingMemoryAdjustment = 0;
}

bool FVulkanDynamicRHI::RHIGetTextureMemoryVisualizeData( FColor* /*TextureData*/, int32 /*SizeX*/, int32 /*SizeY*/, int32 /*Pitch*/, int32 /*PixelSize*/ )
{
	VULKAN_SIGNAL_UNIMPLEMENTED();

	return false;
}

uint32 FVulkanDynamicRHI::RHIComputeMemorySize(FRHITexture* TextureRHI)
{
	if(!TextureRHI)
	{
		return 0;
	}

	return FVulkanTextureBase::Cast(TextureRHI)->Surface.GetMemorySize();
}

/*-----------------------------------------------------------------------------
	2D texture support.
-----------------------------------------------------------------------------*/

FTexture2DRHIRef FVulkanDynamicRHI::RHICreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)

{
	LLM_SCOPE_VULKAN(GetMemoryTagForTextureFlags(Flags));
	return new FVulkanTexture2D(*Device, (EPixelFormat)Format, SizeX, SizeY, NumMips, NumSamples, Flags, InResourceState, CreateInfo);
}

FTexture2DRHIRef FVulkanDynamicRHI::RHIAsyncCreateTexture2D(uint32 SizeX,uint32 SizeY,uint8 Format,uint32 NumMips,ETextureCreateFlags Flags, ERHIAccess InResourceState,void** InitialMipData,uint32 NumInitialMips)
{
	UE_LOG(LogVulkan, Fatal, TEXT("RHIAsyncCreateTexture2D is not supported"));
	VULKAN_SIGNAL_UNIMPLEMENTED(); // Unsupported atm
	return FTexture2DRHIRef();
}

void FVulkanDynamicRHI::RHICopySharedMips(FRHITexture2D* DestTexture2D, FRHITexture2D* SrcTexture2D)
{
	VULKAN_SIGNAL_UNIMPLEMENTED();
}

FTexture2DArrayRHIRef FVulkanDynamicRHI::RHICreateTexture2DArray(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	LLM_SCOPE_VULKAN(GetMemoryTagForTextureFlags(Flags));
	return new FVulkanTexture2DArray(*Device, (EPixelFormat)Format, SizeX, SizeY, SizeZ, NumMips, NumSamples, Flags, InResourceState, CreateInfo.BulkData, CreateInfo.ClearValueBinding);
}

FTexture3DRHIRef FVulkanDynamicRHI::RHICreateTexture3D(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	LLM_SCOPE_VULKAN(GetMemoryTagForTextureFlags(Flags));
	FVulkanTexture3D* Tex3d = new FVulkanTexture3D(*Device, (EPixelFormat)Format, SizeX, SizeY, SizeZ, NumMips, Flags, InResourceState, CreateInfo.BulkData, CreateInfo.ClearValueBinding);

	return Tex3d;
}

void FVulkanDynamicRHI::RHIGetResourceInfo(FRHITexture* Ref, FRHIResourceInfo& OutInfo)
{
	FVulkanTextureBase* Base = (FVulkanTextureBase*)Ref->GetTextureBaseRHI();
	OutInfo.VRamAllocation.AllocationSize = Base->Surface.GetMemorySize();
}

static void DoAsyncReallocateTexture2D(FVulkanCommandListContext& Context, FVulkanTexture2D* OldTexture, FVulkanTexture2D* NewTexture, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanTextures);
	//QUICK_SCOPE_CYCLE_COUNTER(STAT_FRHICommandGnmAsyncReallocateTexture2D_Execute);
	check(Context.IsImmediate());

	// figure out what mips to copy from/to
	const uint32 NumSharedMips = FMath::Min(OldTexture->GetNumMips(), NewTexture->GetNumMips());
	const uint32 SourceFirstMip = OldTexture->GetNumMips() - NumSharedMips;
	const uint32 DestFirstMip = NewTexture->GetNumMips() - NumSharedMips;

	FVulkanCmdBuffer* CmdBuffer = Context.GetCommandBufferManager()->GetUploadCmdBuffer();
	ensure(CmdBuffer->IsOutsideRenderPass());

	VkCommandBuffer StagingCommandBuffer = CmdBuffer->GetHandle();

	VkImageCopy Regions[MAX_TEXTURE_MIP_COUNT];
	check(NumSharedMips <= MAX_TEXTURE_MIP_COUNT);
	FMemory::Memzero(&Regions[0], sizeof(VkImageCopy) * NumSharedMips);
	for (uint32 Index = 0; Index < NumSharedMips; ++Index)
	{
		uint32 MipWidth = FMath::Max<uint32>(NewSizeX >> (DestFirstMip + Index), 1u);
		uint32 MipHeight = FMath::Max<uint32>(NewSizeY >> (DestFirstMip + Index), 1u);

		VkImageCopy& Region = Regions[Index];
		Region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Region.srcSubresource.mipLevel = SourceFirstMip + Index;
		Region.srcSubresource.baseArrayLayer = 0;
		Region.srcSubresource.layerCount = 1;
		//Region.srcOffset
		Region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Region.dstSubresource.mipLevel = DestFirstMip + Index;
		Region.dstSubresource.baseArrayLayer = 0;
		Region.dstSubresource.layerCount = 1;
		//Region.dstOffset
		Region.extent.width = MipWidth;
		Region.extent.height = MipHeight;
		Region.extent.depth = 1;
	}

	const VkImageSubresourceRange SourceSubResourceRange = FVulkanPipelineBarrier::MakeSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, SourceFirstMip, NumSharedMips);
	const VkImageSubresourceRange DestSubResourceRange = FVulkanPipelineBarrier::MakeSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, DestFirstMip, NumSharedMips);

	const FVulkanImageLayout& OldTextureOriginalLayout = Context.GetLayoutManager().GetOrAddFullLayout(OldTexture->Surface, VK_IMAGE_LAYOUT_UNDEFINED);
	ensure(!OldTextureOriginalLayout.AreAllSubresourcesSameLayout() || (OldTextureOriginalLayout.MainLayout != VK_IMAGE_LAYOUT_UNDEFINED));
	FVulkanImageLayout OldTextureCopyLayout = OldTextureOriginalLayout;
	OldTextureCopyLayout.Set(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, SourceSubResourceRange);

	{
		// Pre-copy barriers
		FVulkanPipelineBarrier Barrier;
		Barrier.AddImageLayoutTransition(OldTexture->Surface.Image, VK_IMAGE_ASPECT_COLOR_BIT, OldTextureOriginalLayout, OldTextureCopyLayout);
		Barrier.AddImageLayoutTransition(NewTexture->Surface.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, DestSubResourceRange);
		Barrier.Execute(CmdBuffer->GetHandle());
	}

	VulkanRHI::vkCmdCopyImage(StagingCommandBuffer, OldTexture->Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, NewTexture->Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, NumSharedMips, Regions);

	{
		// Post-copy barriers
		FVulkanPipelineBarrier Barrier;
		Barrier.AddImageLayoutTransition(OldTexture->Surface.Image, VK_IMAGE_ASPECT_COLOR_BIT, OldTextureCopyLayout, OldTextureOriginalLayout);
		Barrier.AddImageLayoutTransition(NewTexture->Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, DestSubResourceRange);
		Barrier.Execute(CmdBuffer->GetHandle());

		// Add tracking for the appropriate subresources (intentionally leave added mips in VK_IMAGE_LAYOUT_UNDEFINED)
		FVulkanImageLayout& NewTextureLayout = Context.GetLayoutManager().GetOrAddFullLayout(NewTexture->Surface, VK_IMAGE_LAYOUT_UNDEFINED);
		ensure(NewTextureLayout.AreAllSubresourcesSameLayout() && (NewTextureLayout.MainLayout == VK_IMAGE_LAYOUT_UNDEFINED));
		NewTextureLayout.Set(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, DestSubResourceRange);
	}

	// request is now complete
	RequestStatus->Decrement();

	// the next unlock for this texture can't block the GPU (it's during runtime)
	//NewTexture->Surface.bSkipBlockOnUnlock = true;
}

struct FRHICommandVulkanAsyncReallocateTexture2D final : public FRHICommand<FRHICommandVulkanAsyncReallocateTexture2D>
{
	FVulkanCommandListContext& Context;
	FVulkanTexture2D* OldTexture;
	FVulkanTexture2D* NewTexture;
	int32 NewMipCount;
	int32 NewSizeX;
	int32 NewSizeY;
	FThreadSafeCounter* RequestStatus;

	FORCEINLINE_DEBUGGABLE FRHICommandVulkanAsyncReallocateTexture2D(FVulkanCommandListContext& InContext, FVulkanTexture2D* InOldTexture, FVulkanTexture2D* InNewTexture, int32 InNewMipCount, int32 InNewSizeX, int32 InNewSizeY, FThreadSafeCounter* InRequestStatus)
		: Context(InContext)
		, OldTexture(InOldTexture)
		, NewTexture(InNewTexture)
		, NewMipCount(InNewMipCount)
		, NewSizeX(InNewSizeX)
		, NewSizeY(InNewSizeY)
		, RequestStatus(InRequestStatus)
	{
	}

	void Execute(FRHICommandListBase& RHICmdList)
	{
		ensure(&((FVulkanCommandListContext&)RHICmdList.GetContext().GetLowestLevelContext()) == &Context);
		DoAsyncReallocateTexture2D(Context, OldTexture, NewTexture, NewMipCount, NewSizeX, NewSizeY, RequestStatus);
	}
};

FTexture2DRHIRef FVulkanDynamicRHI::AsyncReallocateTexture2D_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* OldTextureRHI, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanTextures);
	if (RHICmdList.Bypass())
	{
		return FDynamicRHI::AsyncReallocateTexture2D_RenderThread(RHICmdList, OldTextureRHI, NewMipCount, NewSizeX, NewSizeY, RequestStatus);
	}

	FVulkanTexture2D* OldTexture = ResourceCast(OldTextureRHI);

	FRHIResourceCreateInfo CreateInfo;
	FVulkanTexture2D* NewTexture = new FVulkanTexture2D(*Device, OldTexture->GetFormat(), NewSizeX, NewSizeY, NewMipCount, OldTexture->GetNumSamples(), OldTexture->GetFlags(), ERHIAccess::Unknown, CreateInfo);

	FVulkanCommandListContext& Context = (FVulkanCommandListContext&)RHICmdList.GetContext().GetLowestLevelContext();
	ALLOC_COMMAND_CL(RHICmdList, FRHICommandVulkanAsyncReallocateTexture2D)(Context, OldTexture, NewTexture, NewMipCount, NewSizeX, NewSizeY, RequestStatus);

	return NewTexture;
}

FTexture2DRHIRef FVulkanDynamicRHI::RHIAsyncReallocateTexture2D(FRHITexture2D* OldTextureRHI, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanTextures);
	FVulkanTexture2D* OldTexture = ResourceCast(OldTextureRHI);

	FRHIResourceCreateInfo CreateInfo;
	FVulkanTexture2D* NewTexture = new FVulkanTexture2D(*Device, OldTexture->GetFormat(), NewSizeX, NewSizeY, NewMipCount, OldTexture->GetNumSamples(), OldTexture->GetFlags(), ERHIAccess::Unknown, CreateInfo);

	DoAsyncReallocateTexture2D(Device->GetImmediateContext(), OldTexture, NewTexture, NewMipCount, NewSizeX, NewSizeY, RequestStatus);

	return NewTexture;
}

ETextureReallocationStatus FVulkanDynamicRHI::RHIFinalizeAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
{
	return TexRealloc_Succeeded;
}

ETextureReallocationStatus FVulkanDynamicRHI::RHICancelAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
{
	return TexRealloc_Succeeded;
}

void* FVulkanDynamicRHI::RHILockTexture2D(FRHITexture2D* TextureRHI,uint32 MipIndex,EResourceLockMode LockMode,uint32& DestStride,bool bLockWithinMiptail)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanTextures);
	FVulkanTexture2D* Texture = ResourceCast(TextureRHI);
	check(Texture);

	VulkanRHI::FStagingBuffer** StagingBuffer = nullptr;
	{
		FScopeLock Lock(&GTextureMapLock);
		StagingBuffer = &GPendingLockedBuffers.FindOrAdd(FTextureLock(TextureRHI, MipIndex));
		checkf(!*StagingBuffer, TEXT("Can't lock the same texture twice!"));
	}

	// No locks for read allowed yet
	check(LockMode == RLM_WriteOnly);

	uint32 BufferSize = 0;
	DestStride = 0;
	Texture->Surface.GetMipSize(MipIndex, BufferSize);
	Texture->Surface.GetMipStride(MipIndex, DestStride);
	*StagingBuffer = Device->GetStagingManager().AcquireBuffer(BufferSize);

	void* Data = (*StagingBuffer)->GetMappedPointer();
	return Data;
}

void FVulkanDynamicRHI::InternalUnlockTexture2D(bool bFromRenderingThread, FRHITexture2D* TextureRHI,uint32 MipIndex,bool bLockWithinMiptail)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanTextures);
	FVulkanTexture2D* Texture = ResourceCast(TextureRHI);
	check(Texture);

	VkDevice LogicalDevice = Device->GetInstanceHandle();

	VulkanRHI::FStagingBuffer* StagingBuffer = nullptr;
	{
		FScopeLock Lock(&GTextureMapLock);
		bool bFound = GPendingLockedBuffers.RemoveAndCopyValue(FTextureLock(TextureRHI, MipIndex), StagingBuffer);
		checkf(bFound, TEXT("Texture was not locked!"));
	}

	EPixelFormat Format = Texture->Surface.PixelFormat;
	uint32 MipWidth = FMath::Max<uint32>(Texture->Surface.Width >> MipIndex, 0);
	uint32 MipHeight = FMath::Max<uint32>(Texture->Surface.Height >> MipIndex, 0);
	ensure(!(MipHeight == 0 && MipWidth == 0));
	MipWidth = FMath::Max<uint32>(MipWidth, 1);
	MipHeight = FMath::Max<uint32>(MipHeight, 1);
	uint32 LayerCount = Texture->Surface.GetNumberOfArrayLevels();

	VkBufferImageCopy Region;
	FMemory::Memzero(Region);
	//#todo-rco: Might need an offset here?
	//Region.bufferOffset = 0;
	//Region.bufferRowLength = 0;
	//Region.bufferImageHeight = 0;
	Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Region.imageSubresource.mipLevel = MipIndex;
	//Region.imageSubresource.baseArrayLayer = 0;
	Region.imageSubresource.layerCount = LayerCount;
	Region.imageExtent.width = MipWidth;
	Region.imageExtent.height = MipHeight;
	Region.imageExtent.depth = 1;

	FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	if (!bFromRenderingThread || (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread()))
	{
		FVulkanSurface::InternalLockWrite(Device->GetImmediateContext(), &Texture->Surface, Region, StagingBuffer);
	}
	else
	{
		check(IsInRenderingThread());
		ALLOC_COMMAND_CL(RHICmdList, FRHICommandLockWriteTexture)(&Texture->Surface, Region, StagingBuffer);
	}
}

void* FVulkanDynamicRHI::RHILockTexture2DArray(FRHITexture2DArray* TextureRHI,uint32 TextureIndex,uint32 MipIndex,EResourceLockMode LockMode,uint32& DestStride,bool bLockWithinMiptail)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanTextures);
	FVulkanTexture2DArray* Texture = ResourceCast(TextureRHI);
	check(Texture);

	VulkanRHI::FStagingBuffer** StagingBuffer = nullptr;
	{
		FScopeLock Lock(&GTextureMapLock);
		StagingBuffer = &GPendingLockedBuffers.FindOrAdd(FTextureLock(TextureRHI, MipIndex, TextureIndex));
		checkf(!*StagingBuffer, TEXT("Can't lock the same texture twice!"));
	}

	uint32 BufferSize = 0;
	DestStride = 0;
	Texture->Surface.GetMipSize(MipIndex, BufferSize);
	Texture->Surface.GetMipStride(MipIndex, DestStride);
	*StagingBuffer = Device->GetStagingManager().AcquireBuffer(BufferSize);

	void* Data = (*StagingBuffer)->GetMappedPointer();
	return Data;
}

void FVulkanDynamicRHI::RHIUnlockTexture2DArray(FRHITexture2DArray* TextureRHI,uint32 TextureIndex,uint32 MipIndex,bool bLockWithinMiptail)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanTextures);
	FVulkanTexture2DArray* Texture = ResourceCast(TextureRHI);
	check(Texture);

	VkDevice LogicalDevice = Device->GetInstanceHandle();

	VulkanRHI::FStagingBuffer* StagingBuffer = nullptr;
	{
		FScopeLock Lock(&GTextureMapLock);
		bool bFound = GPendingLockedBuffers.RemoveAndCopyValue(FTextureLock(TextureRHI, MipIndex, TextureIndex), StagingBuffer);
		checkf(bFound, TEXT("Texture was not locked!"));
	}

	EPixelFormat Format = Texture->Surface.PixelFormat;
	uint32 MipWidth = FMath::Max<uint32>(Texture->Surface.Width >> MipIndex, GPixelFormats[Format].BlockSizeX);
	uint32 MipHeight = FMath::Max<uint32>(Texture->Surface.Height >> MipIndex, GPixelFormats[Format].BlockSizeY);

	VkBufferImageCopy Region;
	FMemory::Memzero(Region);
	//#todo-rco: Might need an offset here?
	//Region.bufferOffset = 0;
	//Region.bufferRowLength = 0;
	//Region.bufferImageHeight = 0;
	Region.imageSubresource.aspectMask = Texture->Surface.GetPartialAspectMask();
	Region.imageSubresource.mipLevel = MipIndex;
	Region.imageSubresource.baseArrayLayer = TextureIndex;
	Region.imageSubresource.layerCount = 1;
	Region.imageExtent.width = MipWidth;
	Region.imageExtent.height = MipHeight;
	Region.imageExtent.depth = 1;

	FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	if (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread())
	{
		FVulkanSurface::InternalLockWrite(Device->GetImmediateContext(), &Texture->Surface, Region, StagingBuffer);
	}
	else
	{
		check(IsInRenderingThread());
		ALLOC_COMMAND_CL(RHICmdList, FRHICommandLockWriteTexture)(&Texture->Surface, Region, StagingBuffer);
	}
}

void FVulkanDynamicRHI::InternalUpdateTexture2D(bool bFromRenderingThread, FRHITexture2D* TextureRHI, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourceRowPitch, const uint8* SourceData)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanTextures);
	FVulkanTexture2D* Texture = ResourceCast(TextureRHI);

	EPixelFormat PixelFormat = Texture->GetFormat();
	const int32 BlockSizeX = GPixelFormats[PixelFormat].BlockSizeX;
	const int32 BlockSizeY = GPixelFormats[PixelFormat].BlockSizeY;
	const int32 BlockSizeZ = GPixelFormats[PixelFormat].BlockSizeZ;
	const int32 BlockBytes = GPixelFormats[PixelFormat].BlockBytes;
	VkFormat Format = UEToVkTextureFormat(PixelFormat, false);

	ensure(BlockSizeZ == 1);

	FVulkanCommandListContext& Context = Device->GetImmediateContext();
	const VkPhysicalDeviceLimits& Limits = Device->GetLimits();

	VkBufferImageCopy Region;
	FMemory::Memzero(Region);
	VulkanRHI::FStagingBuffer* StagingBuffer = nullptr;
	const uint32 NumBlocksX = (uint32)FMath::DivideAndRoundUp<int32>(UpdateRegion.Width, (uint32)BlockSizeX);
	const uint32 NumBlocksY = (uint32)FMath::DivideAndRoundUp<int32>(UpdateRegion.Height, (uint32)BlockSizeY);
	ensure(NumBlocksX * BlockBytes <= SourceRowPitch);

	const uint32 DestRowPitch = NumBlocksX * BlockBytes;
	const uint32 DestSlicePitch = DestRowPitch * NumBlocksY;

	const uint32 BufferSize = Align(DestSlicePitch, Limits.minMemoryMapAlignment);
	StagingBuffer = Device->GetStagingManager().AcquireBuffer(BufferSize);
	void* RESTRICT Memory = StagingBuffer->GetMappedPointer();

	uint8* RESTRICT DestData = (uint8*)Memory;
	uint8* RESTRICT SourceRowData = (uint8*)SourceData;
	for (uint32 Height = 0; Height < NumBlocksY; ++Height)
	{
		FMemory::Memcpy(DestData, SourceRowData, NumBlocksX * BlockBytes);
		DestData += DestRowPitch;
		SourceRowData += SourceRowPitch;
	}

	//Region.bufferOffset = 0;
	// Set these to zero to assume tightly packed buffer
	//Region.bufferRowLength = 0;
	//Region.bufferImageHeight = 0;
	Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Region.imageSubresource.mipLevel = MipIndex;
	//Region.imageSubresource.baseArrayLayer = 0;
	Region.imageSubresource.layerCount = 1;
	Region.imageOffset.x = UpdateRegion.DestX;
	Region.imageOffset.y = UpdateRegion.DestY;
	//Region.imageOffset.z = 0;
	Region.imageExtent.width = UpdateRegion.Width;
	Region.imageExtent.height = UpdateRegion.Height;
	Region.imageExtent.depth = 1;

	FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	if (!bFromRenderingThread || (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread()))
	{
		FVulkanSurface::InternalLockWrite(Device->GetImmediateContext(), &Texture->Surface, Region, StagingBuffer);
	}
	else
	{
		check(IsInRenderingThread());
		ALLOC_COMMAND_CL(RHICmdList, FRHICommandLockWriteTexture)(&Texture->Surface, Region, StagingBuffer);
	}
}

FUpdateTexture3DData FVulkanDynamicRHI::BeginUpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion)
{
	const int32 FormatSize = PixelFormatBlockBytes[Texture->GetFormat()];
	const int32 RowPitch = UpdateRegion.Width * FormatSize;
	const int32 DepthPitch = UpdateRegion.Width * UpdateRegion.Height * FormatSize;

	SIZE_T MemorySize = static_cast<SIZE_T>(DepthPitch)* UpdateRegion.Depth;
	uint8* Data = (uint8*)FMemory::Malloc(MemorySize);

	return FUpdateTexture3DData(Texture, MipIndex, UpdateRegion, RowPitch, DepthPitch, Data, MemorySize, GFrameNumberRenderThread);
}

void FVulkanDynamicRHI::EndUpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FUpdateTexture3DData& UpdateData)
{
	check(IsInRenderingThread());
	check(GFrameNumberRenderThread == UpdateData.FrameNumber);

	InternalUpdateTexture3D(true, UpdateData.Texture, UpdateData.MipIndex, UpdateData.UpdateRegion, UpdateData.RowPitch, UpdateData.DepthPitch, UpdateData.Data);
	
	FMemory::Free(UpdateData.Data);
	UpdateData.Data = nullptr;
}

void FVulkanDynamicRHI::InternalUpdateTexture3D(bool bFromRenderingThread, FRHITexture3D* TextureRHI, uint32 MipIndex, const FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanTextures);
	FVulkanTexture3D* Texture = ResourceCast(TextureRHI);

	const EPixelFormat PixelFormat = Texture->GetFormat();
	const int32 BlockSizeX = GPixelFormats[PixelFormat].BlockSizeX;
	const int32 BlockSizeY = GPixelFormats[PixelFormat].BlockSizeY;
	const int32 BlockSizeZ = GPixelFormats[PixelFormat].BlockSizeZ;
	const int32 BlockBytes = GPixelFormats[PixelFormat].BlockBytes;
	const VkFormat Format = UEToVkTextureFormat(PixelFormat, false);

	ensure(BlockSizeZ == 1);

	FVulkanCommandListContext& Context = Device->GetImmediateContext();
	const VkPhysicalDeviceLimits& Limits = Device->GetLimits();

	VkBufferImageCopy Region;
	FMemory::Memzero(Region);
	VulkanRHI::FStagingBuffer* StagingBuffer = nullptr;
	const uint32 NumBlocksX = (uint32)FMath::DivideAndRoundUp<int32>(UpdateRegion.Width, (uint32)BlockSizeX);
	const uint32 NumBlocksY = (uint32)FMath::DivideAndRoundUp<int32>(UpdateRegion.Height, (uint32)BlockSizeY);
	check(NumBlocksX * BlockBytes <= SourceRowPitch);
	check(NumBlocksX * BlockBytes * NumBlocksY <= SourceDepthPitch);

	const uint32 DestRowPitch = NumBlocksX * BlockBytes;
	const uint32 DestSlicePitch = DestRowPitch * NumBlocksY;

	const uint32 BufferSize = Align(DestSlicePitch * UpdateRegion.Depth, Limits.minMemoryMapAlignment);
	StagingBuffer = Device->GetStagingManager().AcquireBuffer(BufferSize);
	void* RESTRICT Memory = StagingBuffer->GetMappedPointer();

	ensure(UpdateRegion.SrcX == 0);
	ensure(UpdateRegion.SrcY == 0);

	uint8* RESTRICT DestData = (uint8*)Memory;
	for (uint32 Depth = 0; Depth < UpdateRegion.Depth; Depth++)
	{
		uint8* RESTRICT SourceRowData = (uint8*)SourceData + SourceDepthPitch * Depth;
		for (uint32 Height = 0; Height < NumBlocksY; ++Height)
		{
			FMemory::Memcpy(DestData, SourceRowData, NumBlocksX * BlockBytes);
			DestData += DestRowPitch;
			SourceRowData += SourceRowPitch;
		}
	}
	uint32 TextureSizeX = FMath::Max(1u, TextureRHI->GetSizeX() >> MipIndex);
	uint32 TextureSizeY = FMath::Max(1u, TextureRHI->GetSizeY() >> MipIndex);
	uint32 TextureSizeZ = FMath::Max(1u, TextureRHI->GetSizeZ() >> MipIndex);

	//Region.bufferOffset = 0;
	// Set these to zero to assume tightly packed buffer
	//Region.bufferRowLength = 0;
	//Region.bufferImageHeight = 0;
	Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Region.imageSubresource.mipLevel = MipIndex;
	//Region.imageSubresource.baseArrayLayer = 0;
	Region.imageSubresource.layerCount = 1;
	Region.imageOffset.x = UpdateRegion.DestX;
	Region.imageOffset.y = UpdateRegion.DestY;
	Region.imageOffset.z = UpdateRegion.DestZ;
	Region.imageExtent.width = (uint32)FMath::Min((int32)(TextureSizeX-UpdateRegion.DestX), (int32)UpdateRegion.Width);
	Region.imageExtent.height = (uint32)FMath::Min((int32)(TextureSizeY-UpdateRegion.DestY), (int32)UpdateRegion.Height);
	Region.imageExtent.depth = (uint32)FMath::Min((int32)(TextureSizeZ-UpdateRegion.DestZ), (int32)UpdateRegion.Depth);

	FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	if (!bFromRenderingThread || (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread()))
	{
		FVulkanSurface::InternalLockWrite(Device->GetImmediateContext(), &Texture->Surface, Region, StagingBuffer);
	}
	else
	{
		check(IsInRenderingThread());
		ALLOC_COMMAND_CL(RHICmdList, FRHICommandLockWriteTexture)(&Texture->Surface, Region, StagingBuffer);
	}
}


VkImageView FVulkanTextureView::StaticCreate(FVulkanDevice& Device, VkImage InImage, VkImageViewType ViewType, VkImageAspectFlags AspectFlags, EPixelFormat UEFormat, VkFormat Format, uint32 FirstMip, uint32 NumMips, uint32 ArraySliceIndex, uint32 NumArraySlices, bool bUseIdentitySwizzle, const FSamplerYcbcrConversionInitializer* ConversionInitializer)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanTextures);
	VkImageView OutView = VK_NULL_HANDLE;

	VkImageViewCreateInfo ViewInfo;
	ZeroVulkanStruct(ViewInfo, VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
	ViewInfo.image = InImage;
	ViewInfo.viewType = ViewType;
	ViewInfo.format = Format;

#if VULKAN_SUPPORTS_ASTC_DECODE_MODE
	VkImageViewASTCDecodeModeEXT DecodeMode;
	if (Device.GetOptionalExtensions().HasEXTASTCDecodeMode && IsAstcLdrFormat(Format) && !IsAstcSrgbFormat(Format))
	{
		ZeroVulkanStruct(DecodeMode, VK_STRUCTURE_TYPE_IMAGE_VIEW_ASTC_DECODE_MODE_EXT);
		DecodeMode.decodeMode = VK_FORMAT_R8G8B8A8_UNORM;
		DecodeMode.pNext = ViewInfo.pNext;
		ViewInfo.pNext = &DecodeMode;
	}
#endif

	if (bUseIdentitySwizzle)
	{
		// VK_COMPONENT_SWIZZLE_IDENTITY == 0 and this was memzero'd already
	}
	else
	{
		ViewInfo.components = Device.GetFormatComponentMapping(UEFormat);
	}

#if VULKAN_SUPPORTS_COLOR_CONVERSIONS
	VkSamplerYcbcrConversionInfo ConversionInfo;
	if (ConversionInitializer != nullptr)
	{
		VkSamplerYcbcrConversionCreateInfo ConversionCreateInfo;
		FMemory::Memzero(&ConversionCreateInfo, sizeof(VkSamplerYcbcrConversionCreateInfo));
		ConversionCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
		ConversionCreateInfo.format = ConversionInitializer->Format;

		ConversionCreateInfo.components.a = ConversionInitializer->Components.a;
		ConversionCreateInfo.components.r = ConversionInitializer->Components.r;
		ConversionCreateInfo.components.g = ConversionInitializer->Components.g;
		ConversionCreateInfo.components.b = ConversionInitializer->Components.b;

		ConversionCreateInfo.ycbcrModel = ConversionInitializer->Model;
		ConversionCreateInfo.ycbcrRange = ConversionInitializer->Range;
		ConversionCreateInfo.xChromaOffset = ConversionInitializer->XOffset;
		ConversionCreateInfo.yChromaOffset = ConversionInitializer->YOffset;
		ConversionCreateInfo.chromaFilter = VK_FILTER_NEAREST;
		ConversionCreateInfo.forceExplicitReconstruction = VK_FALSE;

		check(ConversionInitializer->Format != VK_FORMAT_UNDEFINED); // No support for VkExternalFormatANDROID yet.

		FMemory::Memzero(&ConversionInfo, sizeof(VkSamplerYcbcrConversionInfo));
		ConversionInfo.conversion = Device.CreateSamplerColorConversion(ConversionCreateInfo);
		ConversionInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
		ConversionInfo.pNext = ViewInfo.pNext;
		ViewInfo.pNext = &ConversionInfo;
	}
#endif

	ViewInfo.subresourceRange.aspectMask = AspectFlags;
	ViewInfo.subresourceRange.baseMipLevel = FirstMip;
	ensure(NumMips != 0xFFFFFFFF);
	ViewInfo.subresourceRange.levelCount = NumMips;

	auto CheckUseNvidiaWorkaround = [&Device]() -> bool
	{
		if (Device.GetVendorId() == EGpuVendorId::Nvidia)
		{
			// Workaround for 20xx family not copying last mips correctly, so instead the view is created without the last 1x1 and 2x2 mips
			if (GRHIAdapterName.Contains(TEXT("RTX 20")))
			{
				UNvidiaDriverVersion NvidiaVersion;
				const VkPhysicalDeviceProperties& Props = Device.GetDeviceProperties();
				static_assert(sizeof(NvidiaVersion) == sizeof(Props.driverVersion), "Mismatched Nvidia pack driver version!");
				NvidiaVersion.Packed = Props.driverVersion;
				if (NvidiaVersion.Major < 430)
				{
					return true;
				}
			}
		}
		return false;
	};
	static bool bNvidiaWorkaround = CheckUseNvidiaWorkaround();
	if (bNvidiaWorkaround && Format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && Format <= VK_FORMAT_BC7_SRGB_BLOCK && NumMips > 1)
	{
		ViewInfo.subresourceRange.levelCount = FMath::Max(1, int32(NumMips) - 2);
	}

	ensure(ArraySliceIndex != 0xFFFFFFFF);
	ViewInfo.subresourceRange.baseArrayLayer = ArraySliceIndex;
	ensure(NumArraySlices != 0xFFFFFFFF);
	switch (ViewType)
	{
	case VK_IMAGE_VIEW_TYPE_3D:
		ViewInfo.subresourceRange.layerCount = 1;
		break;
	case VK_IMAGE_VIEW_TYPE_CUBE:
		ensure(NumArraySlices == 1);
		ViewInfo.subresourceRange.layerCount = 6;
		break;
	case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
		ViewInfo.subresourceRange.layerCount = 6 * NumArraySlices;
		break;
	case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
	case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
		ViewInfo.subresourceRange.layerCount = NumArraySlices;
		break;
	default:
		ViewInfo.subresourceRange.layerCount = 1;
		break;
	}

	//HACK.  DX11 on PC currently uses a D24S8 depthbuffer and so needs an X24_G8 SRV to visualize stencil.
	//So take that as our cue to visualize stencil.  In the future, the platform independent code will have a real format
	//instead of PF_DepthStencil, so the cross-platform code could figure out the proper format to pass in for this.
	if (UEFormat == PF_X24_G8)
	{
		ensure(ViewInfo.format == VK_FORMAT_UNDEFINED);
		ViewInfo.format = (VkFormat)GPixelFormats[PF_DepthStencil].PlatformFormat;
		ensure(ViewInfo.format != VK_FORMAT_UNDEFINED);
		ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
	}

	INC_DWORD_STAT(STAT_VulkanNumImageViews);
	VERIFYVULKANRESULT(VulkanRHI::vkCreateImageView(Device.GetInstanceHandle(), &ViewInfo, VULKAN_CPU_ALLOCATOR, &OutView));

	return OutView;
}

void FVulkanTextureView::Create(FVulkanDevice& Device, VkImage InImage, VkImageViewType ViewType, VkImageAspectFlags AspectFlags, EPixelFormat UEFormat, VkFormat Format, uint32 FirstMip, uint32 NumMips, uint32 ArraySliceIndex, uint32 NumArraySlices, bool bUseIdentitySwizzle)
{
	View = StaticCreate(Device, InImage, ViewType, AspectFlags, UEFormat, Format, FirstMip, NumMips, ArraySliceIndex, NumArraySlices, bUseIdentitySwizzle, nullptr);
	Image = InImage;
	
	if (UseVulkanDescriptorCache())
	{
		ViewId = ++GVulkanImageViewHandleIdCounter;
	}
}

void FVulkanTextureView::Create(FVulkanDevice& Device, VkImage InImage, VkImageViewType ViewType, VkImageAspectFlags AspectFlags, EPixelFormat UEFormat, VkFormat Format, uint32 FirstMip, uint32 NumMips, uint32 ArraySliceIndex, uint32 NumArraySlices, FSamplerYcbcrConversionInitializer& ConversionInitializer, bool bUseIdentitySwizzle)
{
	View = StaticCreate(Device, InImage, ViewType, AspectFlags, UEFormat, Format, FirstMip, NumMips, ArraySliceIndex, NumArraySlices, bUseIdentitySwizzle, &ConversionInitializer);
	Image = InImage;
	
	if (UseVulkanDescriptorCache())
	{
		ViewId = ++GVulkanImageViewHandleIdCounter;
	}
}

void FVulkanTextureView::Destroy(FVulkanDevice& Device)
{
	if (View)
	{
		DEC_DWORD_STAT(STAT_VulkanNumImageViews);
		Device.GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::ImageView, View);
		Image = VK_NULL_HANDLE;
		View = VK_NULL_HANDLE;
		ViewId = 0;
	}
}

FVulkanTextureBase::FVulkanTextureBase(FVulkanDevice& Device, VkImageViewType ResourceType, EPixelFormat InFormat, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags UEFlags, ERHIAccess InResourceState, const FRHIResourceCreateInfo& CreateInfo)
	: Surface(Device, this, ResourceType, InFormat, SizeX, SizeY, SizeZ, ArraySize, NumMips, NumSamples, UEFlags, InResourceState, CreateInfo)
	, PartialView(nullptr)
{
	Surface.OwningTexture = this;
	VULKAN_TRACK_OBJECT_CREATE(FVulkanTextureBase, this);

	if(UEFlags & TexCreate_CPUReadback)
	{
		return;
	}

	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanTextures);
	const bool bArray = ResourceType == VK_IMAGE_VIEW_TYPE_1D_ARRAY || ResourceType == VK_IMAGE_VIEW_TYPE_2D_ARRAY || ResourceType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
	if (Surface.ViewFormat == VK_FORMAT_UNDEFINED)
	{
		Surface.StorageFormat = UEToVkTextureFormat(InFormat, false);
		Surface.ViewFormat = UEToVkTextureFormat(InFormat, (UEFlags & TexCreate_SRGB) == TexCreate_SRGB);
		checkf(Surface.StorageFormat != VK_FORMAT_UNDEFINED, TEXT("Pixel Format %d not defined!"), (int32)InFormat);
	}

	if (ResourceType != VK_IMAGE_VIEW_TYPE_MAX_ENUM)
	{
		DefaultView.Create(Device, Surface.Image, ResourceType, Surface.GetFullAspectMask(), Surface.PixelFormat, Surface.ViewFormat, 0, FMath::Max(NumMips, 1u), 0, bArray ? FMath::Max(1u, ArraySize) : FMath::Max(1u, SizeZ));
	}

	if (Surface.FullAspectMask == Surface.PartialAspectMask)
	{
		PartialView = &DefaultView;
	}
	else
	{
		PartialView = new FVulkanTextureView;
		PartialView->Create(Device, Surface.Image, Surface.ViewType, Surface.PartialAspectMask, Surface.PixelFormat, Surface.ViewFormat, 0, FMath::Max(NumMips, 1u), 0, bArray ? FMath::Max(1u, ArraySize) : FMath::Max(1u, SizeZ));
	}

	if (!CreateInfo.BulkData)
	{
		return;
	}

	// InternalLockWrite leaves the image in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, so make sure the requested resource state is SRV.
	check(EnumHasAnyFlags(InResourceState, ERHIAccess::SRVMask));

	// Transfer bulk data
	VulkanRHI::FStagingBuffer* StagingBuffer = Device.GetStagingManager().AcquireBuffer(CreateInfo.BulkData->GetResourceBulkDataSize());
	void* Data = StagingBuffer->GetMappedPointer();

	// Do copy
	FMemory::Memcpy(Data, CreateInfo.BulkData->GetResourceBulkData(), CreateInfo.BulkData->GetResourceBulkDataSize());
	CreateInfo.BulkData->Discard();

	uint32 LayersPerArrayIndex = (ResourceType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY || ResourceType == VK_IMAGE_VIEW_TYPE_CUBE) ? 6 : 1;

	VkBufferImageCopy Region;
	FMemory::Memzero(Region);
	//#todo-rco: Use real Buffer offset when switching to suballocations!
	Region.bufferOffset = 0;
	Region.bufferRowLength = Surface.Width;
	Region.bufferImageHeight = Surface.Height;
	
	Region.imageSubresource.mipLevel = 0;
	Region.imageSubresource.baseArrayLayer = 0;
	Region.imageSubresource.layerCount = ArraySize * LayersPerArrayIndex;
	Region.imageSubresource.aspectMask = Surface.GetFullAspectMask();

	Region.imageExtent.width = Region.bufferRowLength;
	Region.imageExtent.height = Region.bufferImageHeight;
	Region.imageExtent.depth = Surface.Depth;

	FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	if (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread())
	{
		FVulkanSurface::InternalLockWrite(Device.GetImmediateContext(), &Surface, Region, StagingBuffer);
	}
	else
	{
		check(IsInRenderingThread());
		ALLOC_COMMAND_CL(RHICmdList, FRHICommandLockWriteTexture)(&Surface, Region, StagingBuffer);
	}
}

FVulkanTextureBase::FVulkanTextureBase(FVulkanDevice& Device, VkImageViewType ResourceType, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 ArraySize, uint32 InNumMips, uint32 InNumSamples, VkImage InImage, VkDeviceMemory InMem, ETextureCreateFlags UEFlags, const FRHIResourceCreateInfo& CreateInfo)
	: Surface(Device, ResourceType, Format, SizeX, SizeY, SizeZ, ArraySize, InNumMips, InNumSamples, InImage, UEFlags, CreateInfo)
	, PartialView(nullptr)
{
	Surface.OwningTexture = this;
	VULKAN_TRACK_OBJECT_CREATE(FVulkanTextureBase, this);
	check(InMem == VK_NULL_HANDLE);
	const bool bArray = ResourceType == VK_IMAGE_VIEW_TYPE_1D_ARRAY || ResourceType == VK_IMAGE_VIEW_TYPE_2D_ARRAY || ResourceType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;

	if (ResourceType != VK_IMAGE_VIEW_TYPE_MAX_ENUM && Surface.Image != VK_NULL_HANDLE)
	{
		DefaultView.Create(Device, Surface.Image, ResourceType, Surface.GetFullAspectMask(), Format, Surface.ViewFormat, 0, FMath::Max(Surface.NumMips, 1u), 0, bArray ? FMath::Max(1u, ArraySize) : FMath::Max(1u, SizeZ));
	}

	if (Surface.FullAspectMask == Surface.PartialAspectMask)
	{
		PartialView = &DefaultView;
	}
	else
	{
		PartialView = new FVulkanTextureView;
		PartialView->Create(Device, Surface.Image, Surface.ViewType, Surface.PartialAspectMask, Surface.PixelFormat, Surface.ViewFormat, 0, FMath::Max(InNumMips, 1u), 0, bArray ? FMath::Max(1u, ArraySize) : FMath::Max(1u, SizeZ));
	}
}

FVulkanTextureBase::FVulkanTextureBase(FVulkanDevice& Device, VkImageViewType ResourceType, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, VkImage InImage, VkDeviceMemory InMem, FSamplerYcbcrConversionInitializer& ConversionInitializer, ETextureCreateFlags UEFlags, const FRHIResourceCreateInfo& CreateInfo)
	: Surface(Device, ResourceType, Format, SizeX, SizeY, SizeZ, ArraySize, NumMips, NumSamples, InImage, UEFlags, CreateInfo)
	, PartialView(nullptr)
{
	Surface.OwningTexture = this;
	VULKAN_TRACK_OBJECT_CREATE(FVulkanTextureBase, this);

	check(InMem == VK_NULL_HANDLE);
	const bool bArray = ResourceType == VK_IMAGE_VIEW_TYPE_1D_ARRAY || ResourceType == VK_IMAGE_VIEW_TYPE_2D_ARRAY || ResourceType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;

	Surface.ViewFormat = ConversionInitializer.Format;
	Surface.StorageFormat = ConversionInitializer.Format;

	if (ResourceType != VK_IMAGE_VIEW_TYPE_MAX_ENUM && Surface.Image != VK_NULL_HANDLE)
	{
		DefaultView.Create(Device, Surface.Image, ResourceType, Surface.GetFullAspectMask(), Format, Surface.ViewFormat, 0, FMath::Max(Surface.NumMips, 1u), 0, bArray ? FMath::Max(1u, ArraySize) : FMath::Max(1u, SizeZ), ConversionInitializer);
	}

	// No MSAA support
	check(NumSamples == 1);
	check(!(UEFlags & TexCreate_RenderTargetable));

	if (Surface.FullAspectMask == Surface.PartialAspectMask)
	{
		PartialView = &DefaultView;
	}
	else
	{
		PartialView = new FVulkanTextureView;
		PartialView->Create(Device, Surface.Image, Surface.ViewType, Surface.PartialAspectMask, Surface.PixelFormat, Surface.ViewFormat, 0, FMath::Max(NumMips, 1u), 0, bArray ? FMath::Max(1u, ArraySize) : FMath::Max(1u, SizeZ), ConversionInitializer);
	}
}

FVulkanTextureBase::FVulkanTextureBase(FTextureRHIRef& SrcTextureRHI, const FVulkanTextureBase* SrcTexture, VkImageViewType ResourceType, uint32 SizeX, uint32 SizeY, uint32 SizeZ)
	: Surface(*SrcTexture->Surface.Device, ResourceType, SrcTexture->Surface.PixelFormat, SizeX, SizeY, SizeZ, SrcTexture->Surface.ArraySize, SrcTexture->Surface.NumMips, SrcTexture->Surface.NumSamples, SrcTexture->Surface.Image, SrcTexture->Surface.UEFlags, FRHIResourceCreateInfo())
	, PartialView(nullptr)
	, AliasedTexture(SrcTextureRHI)
{
	VULKAN_TRACK_OBJECT_CREATE(FVulkanTextureBase, this);

	if (Surface.FullAspectMask == Surface.PartialAspectMask)
	{
		PartialView = &DefaultView;
	}
	else
	{
		PartialView = new FVulkanTextureView;
		// Skip create, since we're aliasing.
	}

	AliasTextureResources(SrcTextureRHI);
}

FVulkanTextureBase::~FVulkanTextureBase()
{
	VULKAN_TRACK_OBJECT_DELETE(FVulkanTextureBase, this);
	DestroyViews();

	if (PartialView != &DefaultView)
	{
		delete PartialView;
	}
}

void FVulkanTextureBase::AliasTextureResources(FTextureRHIRef& SrcTextureRHI)
{
	DestroyViews();

	FVulkanTextureBase* SrcTexture = (FVulkanTextureBase*)SrcTextureRHI->GetTextureBaseRHI();

	Surface.Destroy();
	Surface.Image = SrcTexture->Surface.Image;
	DefaultView.View = SrcTexture->DefaultView.View;
	DefaultView.Image = SrcTexture->DefaultView.Image;
	DefaultView.ViewId = SrcTexture->DefaultView.ViewId;

	if (PartialView != &DefaultView)
	{
		PartialView->View = SrcTexture->PartialView->View;
		PartialView->Image = SrcTexture->PartialView->Image;
		PartialView->ViewId = SrcTexture->PartialView->ViewId;
	}
}

void FVulkanTextureBase::DestroyViews()
{
	if (AliasedTexture == nullptr)
	{
		DefaultView.Destroy(*Surface.Device);

		if (PartialView != &DefaultView && PartialView != nullptr)
		{
			PartialView->Destroy(*Surface.Device);
		}
	}
}

void FVulkanSurface::Evict(FVulkanDevice& Device_)
{
	checkNoEntry(); //not supported
}
void FVulkanSurface::Move(FVulkanDevice& Device_, FVulkanCommandListContext& Context, FVulkanAllocation& NewAllocation)
{
	checkNoEntry(); //not supported
}

bool FVulkanSurface::CanEvict()
{
	return true;
}

bool FVulkanSurface::CanMove()
{
	return false;
}

void FVulkanTextureBase::InvalidateViews(FVulkanDevice& Device)
{
	DefaultView.Destroy(Device);
	uint32 NumMips = Surface.GetNumMips();
	const bool bArray = Surface.ViewType == VK_IMAGE_VIEW_TYPE_1D_ARRAY || Surface.ViewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY || Surface.ViewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
	uint32 SizeZOrArraySize = bArray ? FMath::Max(1u, Surface.ArraySize) : FMath::Max(1u, Surface.Depth);

	if(Surface.ViewType != VK_IMAGE_VIEW_TYPE_MAX_ENUM)
	{
		DefaultView.Create(Device, Surface.Image, Surface.ViewType, Surface.GetFullAspectMask(), Surface.PixelFormat, Surface.ViewFormat, 0, FMath::Max(NumMips, 1u), 0, SizeZOrArraySize);
	}
	if(PartialView != &DefaultView)
	{
		PartialView->Destroy(*Surface.Device);
		PartialView->Create(Device, Surface.Image, Surface.ViewType, Surface.PartialAspectMask, Surface.PixelFormat, Surface.ViewFormat, 0, FMath::Max(NumMips, 1u), 0, SizeZOrArraySize);
	}

	VulkanRHI::FVulkanViewBase* View = FirstView;
	while(View)
	{
		View->Invalidate();
		View = View->NextView;
	}
}

void FVulkanTextureBase::Move(FVulkanDevice& Device, FVulkanCommandListContext& Context, FVulkanAllocation& NewAllocation)
{
	FRHITexture* Tex = GetRHITexture();
	uint64 Size = Surface.GetMemorySize();
	static uint64 TotalSize = 0;
	TotalSize += Size;
	if (GVulkanLogDefrag)
	{
		UE_LOG(LogVulkanRHI, Display, TEXT("Moving Surface, %p <<-- %p    :::: %s\n"), NewAllocation.Offset, 42, *Tex->GetName().ToString());
		UE_LOG(LogVulkanRHI, Display, TEXT("Evicted %8.4fkb %8.4fkb   TB %p // %p  :: IMG %p   %-40s\n"), Size / (1024.f), TotalSize / (1024.f), this, &Surface, Surface.Image, *GetResourceFName().ToString());
	}

	Surface.MoveSurface(Device, Context, NewAllocation);
	InvalidateViews(Device);
}

void FVulkanTextureBase::OnFullDefrag(FVulkanDevice& Device, FVulkanCommandListContext& Context, uint32 NewOffset)
{
	FRHITexture* Tex = GetRHITexture();
	uint64 Size = Surface.GetMemorySize();
	static uint64 TotalSize = 0;
	TotalSize += Size;
	if (GVulkanLogDefrag)
	{
		UE_LOG(LogVulkanRHI, Display, TEXT("Moving Surface, %p <<-- %p    :::: %s\n"), NewOffset, 42, *Tex->GetName().ToString());
		UE_LOG(LogVulkanRHI, Display, TEXT("Evicted %8.4fkb %8.4fkb   TB %p // %p  :: IMG %p   %-40s\n"), Size / (1024.f), TotalSize / (1024.f), this, &Surface, Surface.Image, *GetResourceFName().ToString());
	}

	Surface.OnFullDefrag(Device, Context, NewOffset);
	InvalidateViews(Device);
}




void FVulkanTextureBase::Evict(FVulkanDevice& Device)
{
	check(AliasedTexture == nullptr); //can't evict textures we don't own
	uint64 Size = Surface.GetMemorySize();
	static uint64 TotalSize = 0;
	TotalSize += Size;
	if(GVulkanLogDefrag)
	{
		FGenericPlatformMisc::LowLevelOutputDebugStringf(TEXT("Evicted %8.4fkb %8.4fkb   TB %p // %p  :: IMG %p   %-40s\n"), Size / (1024.f), TotalSize / (1024.f), this, &Surface, Surface.Image, *GetResourceFName().ToString());
	}

	Surface.EvictSurface(Device);
	InvalidateViews(Device);
}


static FCriticalSection ViewCritSection;
void FVulkanTextureBase::AttachView(FVulkanViewBase* View)
{
	FScopeLock Lock(&ViewCritSection);
	check(View->NextView == nullptr);
	View->NextView = FirstView;
	FirstView = View;
}

void FVulkanTextureBase::DetachView(FVulkanViewBase* View)
{
	FScopeLock Lock(&ViewCritSection);
	FVulkanViewBase** NextViewPtr = &FirstView;
	while(*NextViewPtr != View)
	{
		NextViewPtr = &(*NextViewPtr)->NextView;
	}
	*NextViewPtr = View->NextView;
	View->NextView = nullptr;
}


FVulkanTexture2D::FVulkanTexture2D(FVulkanDevice& Device, EPixelFormat InFormat, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags UEFlags, ERHIAccess InResourceState, const FRHIResourceCreateInfo& CreateInfo)
:	FRHITexture2D(SizeX, SizeY, FMath::Max(NumMips, 1u), NumSamples, InFormat, UEFlags, CreateInfo.ClearValueBinding)
,	FVulkanTextureBase(Device, VK_IMAGE_VIEW_TYPE_2D, InFormat, SizeX, SizeY, 1, 1, FMath::Max(NumMips, 1u), NumSamples, UEFlags, InResourceState, CreateInfo)
{
}

FVulkanTexture2D::FVulkanTexture2D(FVulkanDevice& Device, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, VkImage Image, ETextureCreateFlags UEFlags, const FRHIResourceCreateInfo& CreateInfo)
:	FRHITexture2D(SizeX, SizeY, NumMips, NumSamples, Format, UEFlags, CreateInfo.ClearValueBinding)
,	FVulkanTextureBase(Device, VK_IMAGE_VIEW_TYPE_2D, Format, SizeX, SizeY, 1, 1, NumMips, NumSamples, Image, VK_NULL_HANDLE, UEFlags)
{
}

FVulkanTexture2D::FVulkanTexture2D(FVulkanDevice& Device, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, VkImage Image, FSamplerYcbcrConversionInitializer& ConversionInitializer, ETextureCreateFlags UEFlags, const FRHIResourceCreateInfo& CreateInfo)
	: FRHITexture2D(SizeX, SizeY, NumMips, NumSamples, Format, UEFlags, CreateInfo.ClearValueBinding)
	, FVulkanTextureBase(Device, VK_IMAGE_VIEW_TYPE_2D, Format, SizeX, SizeY, 1, 1, NumMips, NumSamples, Image, VK_NULL_HANDLE, ConversionInitializer, UEFlags)
{
}

FVulkanTexture2D::FVulkanTexture2D(FTextureRHIRef& SrcTextureRHI, const FVulkanTexture2D* SrcTexture)
	: FRHITexture2D(SrcTexture->GetSizeX(), SrcTexture->GetSizeY(), SrcTexture->GetNumMips(), SrcTexture->GetNumSamples(), SrcTexture->GetFormat(), SrcTexture->GetFlags(), SrcTexture->GetClearBinding())
	, FVulkanTextureBase(SrcTextureRHI, static_cast<const FVulkanTextureBase*>(SrcTexture), VK_IMAGE_VIEW_TYPE_2D, SrcTexture->GetSizeX(), SrcTexture->GetSizeY(), 1)
{
}

FVulkanTexture2D::~FVulkanTexture2D()
{
}

FVulkanTexture2DArray::FVulkanTexture2DArray(FVulkanDevice& Device, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ERHIAccess InResourceState, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue)
	:	FRHITexture2DArray(SizeX, SizeY, ArraySize, NumMips, NumSamples, Format, Flags, InClearValue)
	,	FVulkanTextureBase(Device, VK_IMAGE_VIEW_TYPE_2D_ARRAY, Format, SizeX, SizeY, 1, ArraySize, NumMips, NumSamples, Flags, InResourceState, BulkData)
{
}

FVulkanTexture2DArray::FVulkanTexture2DArray(FVulkanDevice& Device, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, VkImage Image, ETextureCreateFlags Flags, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue)
	:	FRHITexture2DArray(SizeX, SizeY, ArraySize, NumMips, NumSamples, Format, Flags, InClearValue)
	,	FVulkanTextureBase(Device, VK_IMAGE_VIEW_TYPE_2D_ARRAY, Format, SizeX, SizeY, 1, ArraySize, NumMips, NumSamples, Image, VK_NULL_HANDLE, Flags, BulkData)
{
}

FVulkanTexture2DArray::FVulkanTexture2DArray(FTextureRHIRef& SrcTextureRHI, const FVulkanTexture2DArray* SrcTexture)
	: FRHITexture2DArray(SrcTexture->GetSizeX(), SrcTexture->GetSizeY(), SrcTexture->Surface.GetNumberOfArrayLevels(), SrcTexture->GetNumMips(), SrcTexture->GetNumSamples(), SrcTexture->GetFormat(), SrcTexture->GetFlags(), SrcTexture->GetClearBinding())
	, FVulkanTextureBase(SrcTextureRHI, static_cast<const FVulkanTextureBase*>(SrcTexture), VK_IMAGE_VIEW_TYPE_2D_ARRAY, SrcTexture->GetSizeX(), SrcTexture->GetSizeY(), 1)
{
}

void FVulkanTextureReference::SetReferencedTexture(FRHITexture* InTexture)
{
	FRHITextureReference::SetReferencedTexture(InTexture);
}


FVulkanTextureCube::FVulkanTextureCube(FVulkanDevice& Device, EPixelFormat Format, uint32 Size, bool bArray, uint32 ArraySize, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue)
:	 FRHITextureCube(Size, NumMips, Format, Flags, InClearValue)
	//#todo-rco: Array/slices count
,	FVulkanTextureBase(Device, bArray ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE, Format, Size, Size, 1, ArraySize, NumMips, /*NumSamples=*/ 1, Flags, InResourceState, BulkData)
{
}

FVulkanTextureCube::FVulkanTextureCube(FVulkanDevice& Device, EPixelFormat Format, uint32 Size, bool bArray, uint32 ArraySize, uint32 NumMips, VkImage Image, ETextureCreateFlags Flags, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue)
	:	 FRHITextureCube(Size, NumMips, Format, Flags, InClearValue)
	//#todo-rco: Array/slices count
	,	FVulkanTextureBase(Device, bArray ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE, Format, Size, Size, 1, ArraySize, NumMips, /*NumSamples=*/ 1, Image, VK_NULL_HANDLE, Flags, BulkData)
{
}

FVulkanTextureCube::FVulkanTextureCube(FTextureRHIRef& SrcTextureRHI, const FVulkanTextureCube* SrcTexture)
	: FRHITextureCube(SrcTexture->GetSize(), SrcTexture->GetNumMips(), SrcTexture->GetFormat(), SrcTexture->GetFlags(), SrcTexture->GetClearBinding())
	, FVulkanTextureBase(SrcTextureRHI, static_cast<const FVulkanTextureBase*>(SrcTexture), VK_IMAGE_VIEW_TYPE_CUBE, SrcTexture->GetSize(), SrcTexture->GetSize(), 1)
{
}

FVulkanTextureCube::~FVulkanTextureCube()
{
}


FVulkanTexture3D::FVulkanTexture3D(FVulkanDevice& Device, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FResourceBulkDataInterface* BulkData, const FClearValueBinding& InClearValue)
	: FRHITexture3D(SizeX, SizeY, SizeZ, NumMips, Format, Flags, InClearValue)
	, FVulkanTextureBase(Device, VK_IMAGE_VIEW_TYPE_3D, Format, SizeX, SizeY, SizeZ, 1, NumMips, /*NumSamples=*/ 1, Flags, InResourceState, BulkData)
{
}

FVulkanTexture3D::~FVulkanTexture3D()
{
}

/*-----------------------------------------------------------------------------
	Cubemap texture support.
-----------------------------------------------------------------------------*/
FTextureCubeRHIRef FVulkanDynamicRHI::RHICreateTextureCube(uint32 Size, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	LLM_SCOPE_VULKAN(GetMemoryTagForTextureFlags(Flags));
	return new FVulkanTextureCube(*Device, (EPixelFormat)Format, Size, false, 1, NumMips, Flags, InResourceState, CreateInfo.BulkData, CreateInfo.ClearValueBinding);
}

FTextureCubeRHIRef FVulkanDynamicRHI::RHICreateTextureCubeArray(uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	LLM_SCOPE_VULKAN(GetMemoryTagForTextureFlags(Flags));
	return new FVulkanTextureCube(*Device, (EPixelFormat)Format, Size, true, ArraySize, NumMips, Flags, InResourceState, CreateInfo.BulkData, CreateInfo.ClearValueBinding);
}

void* FVulkanDynamicRHI::RHILockTextureCubeFace(FRHITextureCube* TextureCubeRHI, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanTextures);
	FVulkanTextureCube* Texture = ResourceCast(TextureCubeRHI);
	check(Texture);

	VulkanRHI::FStagingBuffer** StagingBuffer = nullptr;
	{
		FScopeLock Lock(&GTextureMapLock);
		StagingBuffer = &GPendingLockedBuffers.FindOrAdd(FTextureLock(TextureCubeRHI, MipIndex));
		checkf(!*StagingBuffer, TEXT("Can't lock the same texture twice!"));
	}

	uint32 BufferSize = 0;
	DestStride = 0;
	Texture->Surface.GetMipSize(MipIndex, BufferSize);
	Texture->Surface.GetMipStride(MipIndex, DestStride);
	*StagingBuffer = Device->GetStagingManager().AcquireBuffer(BufferSize);

	void* Data = (*StagingBuffer)->GetMappedPointer();
	return Data;
}

void FVulkanDynamicRHI::RHIUnlockTextureCubeFace(FRHITextureCube* TextureCubeRHI,uint32 FaceIndex,uint32 ArrayIndex,uint32 MipIndex,bool bLockWithinMiptail)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanTextures);
	FVulkanTextureCube* Texture = ResourceCast(TextureCubeRHI);
	check(Texture);

	VkDevice LogicalDevice = Device->GetInstanceHandle();

	VulkanRHI::FStagingBuffer* StagingBuffer = nullptr;
	{
		FScopeLock Lock(&GTextureMapLock);
		bool bFound = GPendingLockedBuffers.RemoveAndCopyValue(FTextureLock(TextureCubeRHI, MipIndex), StagingBuffer);
		checkf(bFound, TEXT("Texture was not locked!"));
	}

	EPixelFormat Format = Texture->Surface.PixelFormat;
	uint32 MipWidth = FMath::Max<uint32>(Texture->Surface.Width >> MipIndex, 0);
	uint32 MipHeight = FMath::Max<uint32>(Texture->Surface.Height >> MipIndex, 0);
	ensure(!(MipHeight == 0 && MipWidth == 0));
	MipWidth = FMath::Max<uint32>(MipWidth, 1);
	MipHeight = FMath::Max<uint32>(MipHeight, 1);

	VkBufferImageCopy Region;
	FMemory::Memzero(Region);
	//#todo-rco: Might need an offset here?
	//Region.bufferOffset = 0;
	//Region.bufferRowLength = 0;
	//Region.bufferImageHeight = 0;
	Region.imageSubresource.aspectMask = Texture->Surface.GetPartialAspectMask();
	Region.imageSubresource.mipLevel = MipIndex;
	Region.imageSubresource.baseArrayLayer = ArrayIndex * 6 + FaceIndex;
	Region.imageSubresource.layerCount = 1;
	Region.imageExtent.width = MipWidth;
	Region.imageExtent.height = MipHeight;
	Region.imageExtent.depth = 1;

	FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	if (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread())
	{
		FVulkanSurface::InternalLockWrite(Device->GetImmediateContext(), &Texture->Surface, Region, StagingBuffer);
	}
	else
	{
		check(IsInRenderingThread());
		ALLOC_COMMAND_CL(RHICmdList, FRHICommandLockWriteTexture)(&Texture->Surface, Region, StagingBuffer);
	}
}

void FVulkanDynamicRHI::RHIBindDebugLabelName(FRHITexture* TextureRHI, const TCHAR* Name)
{
#if VULKAN_ENABLE_IMAGE_TRACKING_LAYER
	{
		FVulkanTextureBase* Base = (FVulkanTextureBase*)TextureRHI->GetTextureBaseRHI();
		VulkanRHI::BindDebugLabelName(Base->Surface.Image, Name);
	}
#endif

#if VULKAN_ENABLE_DUMP_LAYER || VULKAN_ENABLE_API_DUMP
	{
// TODO: this dies in the printf on android. Needs investigation.
#if !PLATFORM_ANDROID
		FVulkanTextureBase* Base = (FVulkanTextureBase*)TextureRHI->GetTextureBaseRHI();
#if VULKAN_ENABLE_DUMP_LAYER
		VulkanRHI::PrintfBegin
#elif VULKAN_ENABLE_API_DUMP
		FPlatformMisc::LowLevelOutputDebugStringf
#endif
			(*FString::Printf(TEXT("vkDebugMarkerSetObjectNameEXT(0x%p=%s)\n"), Base->Surface.Image, Name));
#endif
	}
#endif

#if VULKAN_ENABLE_DRAW_MARKERS
#if 0//VULKAN_SUPPORTS_DEBUG_UTILS
	if (auto* SetDebugName = Device->GetSetDebugName())
	{
		FVulkanTextureBase* Base = (FVulkanTextureBase*)TextureRHI->GetTextureBaseRHI();
		FTCHARToUTF8 Converter(Name);
		VulkanRHI::SetDebugName(SetDebugName, Device->GetInstanceHandle(), Base->Surface.Image, Converter.Get());
	}
	else
#endif
	if (auto* SetObjectName = Device->GetDebugMarkerSetObjectName())
	{
		FVulkanTextureBase* Base = (FVulkanTextureBase*)TextureRHI->GetTextureBaseRHI();
		FTCHARToUTF8 Converter(Name);
		VulkanRHI::SetDebugMarkerName(SetObjectName, Device->GetInstanceHandle(), Base->Surface.Image, Converter.Get());
	}
#endif
	FName DebugName(Name);
	TextureRHI->SetName(DebugName);
}

void FVulkanDynamicRHI::RHIBindDebugLabelName(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const TCHAR* Name)
{
#if VULKAN_ENABLE_DUMP_LAYER || VULKAN_ENABLE_API_DUMP
	//if (Device->SupportsDebugMarkers())
	{
		//if (FRHITexture2D* Tex2d = UnorderedAccessViewRHI->GetTexture2D())
		//{
		//	FVulkanTexture2D* VulkanTexture = (FVulkanTexture2D*)Tex2d;
		//	VkDebugMarkerObjectTagInfoEXT Info;
		//	ZeroVulkanStruct(Info, VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT);
		//	Info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT;
		//	Info.object = VulkanTexture->Surface.Image;
		//	vkDebugMarkerSetObjectNameEXT(Device->GetInstanceHandle(), &Info);
		//}
	}
#endif
}


void FVulkanDynamicRHI::RHIVirtualTextureSetFirstMipInMemory(FRHITexture2D* TextureRHI, uint32 FirstMip)
{
	VULKAN_SIGNAL_UNIMPLEMENTED();
}

void FVulkanDynamicRHI::RHIVirtualTextureSetFirstMipVisible(FRHITexture2D* TextureRHI, uint32 FirstMip)
{
	VULKAN_SIGNAL_UNIMPLEMENTED();
}

static VkMemoryRequirements FindOrCalculateTexturePlatformSize(FVulkanDevice* Device, VkImageViewType ViewType, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags)
{
	// Adjust number of mips as UTexture can request non-valid # of mips
	NumMips = FMath::Min(FMath::FloorLog2(FMath::Max(SizeX, FMath::Max(SizeY, SizeZ))) + 1, NumMips);

	struct FTexturePlatformSizeKey
	{
		VkImageViewType ViewType;
		uint32 SizeX;
		uint32 SizeY;
		uint32 SizeZ;
		uint32 Format;
		uint32 NumMips;
		uint32 NumSamples;
		ETextureCreateFlags Flags;
	};

	static TMap<uint32, VkMemoryRequirements> TextureSizes;
	static FCriticalSection TextureSizesLock;

	const FTexturePlatformSizeKey Key = { ViewType, SizeX, SizeY, SizeZ, Format, NumMips, NumSamples, Flags};
	const uint32 Hash = FCrc::MemCrc32(&Key, sizeof(FTexturePlatformSizeKey));

	VkMemoryRequirements* Found = nullptr;
	{
		FScopeLock Lock(&TextureSizesLock);
		Found = TextureSizes.Find(Hash);
		if (Found)
		{
			return *Found;
		}
	}

	EPixelFormat PixelFormat = (EPixelFormat)Format;
	VkMemoryRequirements MemReq;

	// Create temporary image to measure the memory requirements
	FVulkanSurface::FImageCreateInfo TmpCreateInfo;
	FVulkanSurface::GenerateImageCreateInfo(TmpCreateInfo, *Device, ViewType,
		PixelFormat, SizeX, SizeY, SizeZ, 1, NumMips, NumSamples,
		Flags, nullptr, nullptr, false);

	VkImage TmpImage;
	VERIFYVULKANRESULT(VulkanRHI::vkCreateImage(Device->GetInstanceHandle(), &TmpCreateInfo.ImageCreateInfo, VULKAN_CPU_ALLOCATOR, &TmpImage));
	VulkanRHI::vkGetImageMemoryRequirements(Device->GetInstanceHandle(), TmpImage, &MemReq);
	VulkanRHI::vkDestroyImage(Device->GetInstanceHandle(), TmpImage, VULKAN_CPU_ALLOCATOR);

	{
		FScopeLock Lock(&TextureSizesLock);
		TextureSizes.Add(Hash, MemReq);
	}
	
	return MemReq;
}



uint64 FVulkanDynamicRHI::RHICalcTexture2DPlatformSize(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign)
{
	const VkMemoryRequirements MemReq = FindOrCalculateTexturePlatformSize(Device, VK_IMAGE_VIEW_TYPE_2D, SizeX, SizeY, 1, Format, NumMips, NumSamples, Flags);
	OutAlign = MemReq.alignment;
	return MemReq.size;
}

uint64 FVulkanDynamicRHI::RHICalcTexture3DPlatformSize(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign)
{
	const VkMemoryRequirements MemReq = FindOrCalculateTexturePlatformSize(Device, VK_IMAGE_VIEW_TYPE_3D, SizeX, SizeY, SizeZ, Format, NumMips, 1, Flags);
	OutAlign = MemReq.alignment;
	return MemReq.size;
}

uint64 FVulkanDynamicRHI::RHICalcTextureCubePlatformSize(uint32 Size, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign)
{
	const VkMemoryRequirements MemReq = FindOrCalculateTexturePlatformSize(Device, VK_IMAGE_VIEW_TYPE_CUBE, Size, Size, 1, Format, NumMips, 1, Flags);
	OutAlign = MemReq.alignment;
	return MemReq.size;
}

FTextureReferenceRHIRef FVulkanDynamicRHI::RHICreateTextureReference(FLastRenderTimeContainer* LastRenderTime)
{
	return new FVulkanTextureReference(*Device, LastRenderTime);
}

void FVulkanCommandListContext::RHIUpdateTextureReference(FRHITextureReference* TextureRef, FRHITexture* NewTexture)
{
	//#todo-rco: Implementation needs to be verified
	FVulkanTextureReference* VulkanTextureRef = (FVulkanTextureReference*)TextureRef;
	if (VulkanTextureRef)
	{
		VulkanTextureRef->SetReferencedTexture(NewTexture);
	}
}

void FVulkanCommandListContext::RHICopyTexture(FRHITexture* SourceTexture, FRHITexture* DestTexture, const FRHICopyTextureInfo& CopyInfo)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanTextures);
	check(SourceTexture && DestTexture);

	FVulkanTextureBase* Source = static_cast<FVulkanTextureBase*>(SourceTexture->GetTextureBaseRHI());
	FVulkanTextureBase* Dest = static_cast<FVulkanTextureBase*>(DestTexture->GetTextureBaseRHI());

	FVulkanSurface& SrcSurface = Source->Surface;
	FVulkanSurface& DstSurface = Dest->Surface;

	VkImageLayout SrcLayout = LayoutManager.FindLayoutChecked(SrcSurface.Image);
	ensureMsgf(SrcLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, TEXT("Expected source texture to be in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, actual layout is %d"), SrcLayout);

	FVulkanCmdBuffer* InCmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	check(InCmdBuffer->IsOutsideRenderPass());
	VkCommandBuffer CmdBuffer = InCmdBuffer->GetHandle();


	check((SrcSurface.UEFlags & TexCreate_CPUReadback) == 0);
	if((DstSurface.UEFlags & TexCreate_CPUReadback) == TexCreate_CPUReadback)
	{
		check(CopyInfo.DestSliceIndex == 0); //slices not supported in TexCreate_CPUReadback textures.
		FIntVector Size = CopyInfo.Size;
		if(Size == FIntVector::ZeroValue)
		{
			ensure(SrcSurface.Width <= DstSurface.Width && SrcSurface.Height <= DstSurface.Height);
			Size.X = FMath::Max(1u, SrcSurface.Width >> CopyInfo.SourceMipIndex);
			Size.Y = FMath::Max(1u, SrcSurface.Height >> CopyInfo.SourceMipIndex);
		}		
		VkBufferImageCopy CopyRegion[MAX_TEXTURE_MIP_COUNT];
		FMemory::Memzero(CopyRegion);

		const FVulkanCpuReadbackBuffer* CpuReadbackBuffer = DstSurface.GetCpuReadbackBuffer();
		uint32 SourceSliceIndex = CopyInfo.SourceSliceIndex;
		uint32 SourceMipIndex = CopyInfo.SourceMipIndex;
		uint32 DestMipIndex = CopyInfo.DestMipIndex;
		for (uint32 Index = 0; Index < CopyInfo.NumMips; ++Index)
		{
			CopyRegion[Index].bufferOffset = CpuReadbackBuffer->MipOffsets[DestMipIndex + Index];
			CopyRegion[Index].bufferRowLength = Size.X;
			CopyRegion[Index].bufferImageHeight = Size.Y;
			CopyRegion[Index].imageSubresource.aspectMask = SrcSurface.GetFullAspectMask();
			CopyRegion[Index].imageSubresource.mipLevel = SourceMipIndex;
			CopyRegion[Index].imageSubresource.baseArrayLayer = SourceSliceIndex;
			CopyRegion[Index].imageSubresource.layerCount = 1;
			CopyRegion[Index].imageExtent.width = Size.X;
			CopyRegion[Index].imageExtent.height = Size.Y;
			CopyRegion[Index].imageExtent.depth = 1;

			Size.X = FMath::Max(1, Size.X / 2);
			Size.Y = FMath::Max(1, Size.Y / 2);
		}


		VulkanRHI::vkCmdCopyImageToBuffer(CmdBuffer, SrcSurface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, CpuReadbackBuffer->Buffer, CopyInfo.NumMips, &CopyRegion[0]);

		FVulkanPipelineBarrier BarrierMemory;
		BarrierMemory.MemoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		BarrierMemory.MemoryBarrier.pNext = nullptr;
		BarrierMemory.MemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		BarrierMemory.MemoryBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		BarrierMemory.SrcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		BarrierMemory.DstStageMask = VK_PIPELINE_STAGE_HOST_BIT;

		BarrierMemory.Execute(CmdBuffer);
	}
	else
	{
		VkImageLayout DstLayout = LayoutManager.FindLayoutChecked(DstSurface.Image);
		ensureMsgf(DstLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, TEXT("Expected destination texture to be in VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, actual layout is %d"), DstLayout);


		VkImageCopy Region;
		FMemory::Memzero(Region);
		if (CopyInfo.Size == FIntVector::ZeroValue)
		{
			// Copy whole texture when zero vector is specified for region size
			ensure(SrcSurface.Width <= DstSurface.Width && SrcSurface.Height <= DstSurface.Height);
			Region.extent.width = FMath::Max(1u, SrcSurface.Width >> CopyInfo.SourceMipIndex);
			Region.extent.height = FMath::Max(1u, SrcSurface.Height >> CopyInfo.SourceMipIndex);
		}
		else
		{
			ensure(CopyInfo.Size.X > 0 && (uint32)CopyInfo.Size.X <= DstSurface.Width && CopyInfo.Size.Y > 0 && (uint32)CopyInfo.Size.Y <= DstSurface.Height);
			Region.extent.width = CopyInfo.Size.X;
			Region.extent.height = CopyInfo.Size.Y;
		}
		Region.extent.depth = 1;
		Region.srcSubresource.aspectMask = SrcSurface.GetFullAspectMask();
		Region.srcSubresource.baseArrayLayer = CopyInfo.SourceSliceIndex;
		Region.srcSubresource.layerCount = CopyInfo.NumSlices;
		Region.srcSubresource.mipLevel = CopyInfo.SourceMipIndex;
		Region.srcOffset.x = CopyInfo.SourcePosition.X;
		Region.srcOffset.y = CopyInfo.SourcePosition.Y;
		Region.dstSubresource.aspectMask = DstSurface.GetFullAspectMask();
		Region.dstSubresource.baseArrayLayer = CopyInfo.DestSliceIndex;
		Region.dstSubresource.layerCount = CopyInfo.NumSlices;
		Region.dstSubresource.mipLevel = CopyInfo.DestMipIndex;
		Region.dstOffset.x = CopyInfo.DestPosition.X;
		Region.dstOffset.y = CopyInfo.DestPosition.Y;

		for (uint32 Index = 0; Index < CopyInfo.NumMips; ++Index)
		{
			VulkanRHI::vkCmdCopyImage(CmdBuffer,
				SrcSurface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				DstSurface.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &Region);
			Region.extent.width = FMath::Max(1u, Region.extent.width / 2);
			Region.extent.height = FMath::Max(1u, Region.extent.height / 2);
			++Region.srcSubresource.mipLevel;
			++Region.dstSubresource.mipLevel;
		}
	}
}

void FVulkanCommandListContext::RHICopyBufferRegion(FRHIVertexBuffer* DstBuffer, uint64 DstOffset, FRHIVertexBuffer* SrcBuffer, uint64 SrcOffset, uint64 NumBytes)
{
	if (!DstBuffer || !SrcBuffer || DstBuffer == SrcBuffer || !NumBytes)
	{
		return;
	}

	FVulkanVertexBuffer* DstBufferVk = ResourceCast(DstBuffer);
	FVulkanVertexBuffer* SrcBufferVk = ResourceCast(SrcBuffer);

	check(DstBufferVk && SrcBufferVk);
	check(DstOffset + NumBytes <= DstBuffer->GetSize() && SrcOffset + NumBytes <= SrcBuffer->GetSize());

	uint64 DstOffsetVk = DstBufferVk->GetOffset() + DstOffset;
	uint64 SrcOffsetVk = SrcBufferVk->GetOffset() + SrcOffset;

	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	check(CmdBuffer->IsOutsideRenderPass());
	VkCommandBuffer VkCmdBuffer = CmdBuffer->GetHandle();

	VkMemoryBarrier BarrierBefore = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT| VK_ACCESS_TRANSFER_WRITE_BIT };
	VulkanRHI::vkCmdPipelineBarrier(VkCmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &BarrierBefore, 0, nullptr, 0, nullptr);

	VkBufferCopy Region = {};
	Region.srcOffset = SrcOffsetVk;
	Region.dstOffset = DstOffsetVk;
	Region.size = NumBytes;
	VulkanRHI::vkCmdCopyBuffer(VkCmdBuffer, SrcBufferVk->GetHandle(), DstBufferVk->GetHandle(), 1, &Region);

	VkMemoryBarrier BarrierAfter = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT };
	VulkanRHI::vkCmdPipelineBarrier(VkCmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &BarrierAfter, 0, nullptr, 0, nullptr);
}
