// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanRenderTarget.cpp: Vulkan render target implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "ScreenRendering.h"
#include "VulkanPendingState.h"
#include "VulkanContext.h"
#include "VulkanSwapChain.h"
#include "SceneUtils.h"
#include "RHISurfaceDataConversion.h"

// Enable this by default for desktop platforms, but avoid it for others such as mobile due to a DEVICE LOST when alt+tabing
// This is a workaround and may end up causing some hitches on the rendering thread
static int32 GVulkanFlushOnMapStaging = PLATFORM_DESKTOP;
static FAutoConsoleVariableRef CVarGVulkanFlushOnMapStaging(
	TEXT("r.Vulkan.FlushOnMapStaging"),
	GVulkanFlushOnMapStaging,
	TEXT("Flush GPU on MapStagingSurface calls without any fence.\n")
	TEXT(" 0: Do not Flush (default)\n")
	TEXT(" 1: Flush"),
	ECVF_Default
);


static int32 GSubmitOnCopyToResolve = 0;
static FAutoConsoleVariableRef CVarVulkanSubmitOnCopyToResolve(
	TEXT("r.Vulkan.SubmitOnCopyToResolve"),
	GSubmitOnCopyToResolve,
	TEXT("Submits the Queue to the GPU on every RHICopyToResolveTarget call.\n")
	TEXT(" 0: Do not submit (default)\n")
	TEXT(" 1: Submit"),
	ECVF_Default
	);

static int32 GIgnoreCPUReads = 0;
static FAutoConsoleVariableRef CVarVulkanIgnoreCPUReads(
	TEXT("r.Vulkan.IgnoreCPUReads"),
	GIgnoreCPUReads,
	TEXT("Debugging utility for GPU->CPU reads.\n")
	TEXT(" 0 will read from the GPU (default).\n")
	TEXT(" 1 will NOT read from the GPU and fill with zeros.\n"),
	ECVF_Default
	);

static FCriticalSection GStagingMapLock;
static TMap<FVulkanTextureBase*, VulkanRHI::FStagingBuffer*> GPendingLockedStagingBuffers;

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
TAutoConsoleVariable<int32> CVarVulkanDebugBarrier(
	TEXT("r.Vulkan.DebugBarrier"),
	0,
	TEXT("Forces a full barrier for debugging. This is a mask/bitfield (so add up the values)!\n")
	TEXT(" 0: Don't (default)\n")
	TEXT(" 1: Enable heavy barriers after EndRenderPass()\n")
	TEXT(" 2: Enable heavy barriers after every dispatch\n")
	TEXT(" 4: Enable heavy barriers after upload cmd buffers\n")
	TEXT(" 8: Enable heavy barriers after active cmd buffers\n")
	TEXT(" 16: Enable heavy buffer barrier after uploads\n")
	TEXT(" 32: Enable heavy buffer barrier between acquiring back buffer and blitting into swapchain\n"),
	ECVF_Default
);
#endif

FVulkanRenderPass* FVulkanCommandListContext::PrepareRenderPassForPSOCreation(const FGraphicsPipelineStateInitializer& Initializer)
{
	FVulkanRenderTargetLayout RTLayout(Initializer);
	return PrepareRenderPassForPSOCreation(RTLayout);
}

FVulkanRenderPass* FVulkanCommandListContext::PrepareRenderPassForPSOCreation(const FVulkanRenderTargetLayout& RTLayout)
{
	FVulkanRenderPass* RenderPass = nullptr;
	RenderPass = LayoutManager.GetOrCreateRenderPass(*Device, RTLayout);
	return RenderPass;
}

template<typename RegionType>
static void SetupCopyOrResolveRegion(RegionType& Region, const FVulkanSurface& SrcSurface, const FVulkanSurface& DstSurface, const VkImageSubresourceRange& SrcRange, const VkImageSubresourceRange& DstRange, const FResolveParams& ResolveParams)
{
	FMemory::Memzero(Region);
	ensure(SrcSurface.Width == DstSurface.Width && SrcSurface.Height == DstSurface.Height);

	if(ResolveParams.Rect.X1 >= 0 && ResolveParams.Rect.Y1 >= 0 && ResolveParams.DestRect.X1 >= 0 && ResolveParams.DestRect.Y1 >= 0)
	{
		Region.srcOffset.x = ResolveParams.Rect.X1;
		Region.srcOffset.y = ResolveParams.Rect.Y1;
		Region.dstOffset.x = ResolveParams.DestRect.X1;
		Region.dstOffset.y = ResolveParams.DestRect.Y1;
	}

	Region.extent.width = FMath::Max(1u, SrcSurface.Width >> ResolveParams.MipIndex);
	Region.extent.height = FMath::Max(1u, SrcSurface.Height >> ResolveParams.MipIndex);
	Region.extent.depth = 1;
	Region.srcSubresource.aspectMask = SrcSurface.GetFullAspectMask();
	Region.srcSubresource.baseArrayLayer = SrcRange.baseArrayLayer;
	Region.srcSubresource.layerCount = 1;
	Region.srcSubresource.mipLevel = ResolveParams.MipIndex;
	Region.dstSubresource.aspectMask = DstSurface.GetFullAspectMask();
	Region.dstSubresource.baseArrayLayer = DstRange.baseArrayLayer;
	Region.dstSubresource.layerCount = 1;
	Region.dstSubresource.mipLevel = ResolveParams.MipIndex;
}

void FVulkanCommandListContext::RHICopyToResolveTarget(FRHITexture* SourceTextureRHI, FRHITexture* DestTextureRHI, const FResolveParams& InResolveParams)
{
	if (!SourceTextureRHI || !DestTextureRHI)
	{
		// no need to do anything (silently ignored)
		return;
	}

	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	check(CmdBuffer->IsOutsideRenderPass());

	FRHITexture2D* SourceTexture2D = SourceTextureRHI->GetTexture2D();
	FRHITexture2DArray* SourceTexture2DArray = SourceTextureRHI->GetTexture2DArray();
	FRHITexture3D* SourceTexture3D = SourceTextureRHI->GetTexture3D();
	FRHITextureCube* SourceTextureCube = SourceTextureRHI->GetTextureCube();
	FRHITexture2D* DestTexture2D = DestTextureRHI->GetTexture2D();
	FRHITexture2DArray* DestTexture2DArray = DestTextureRHI->GetTexture2DArray();
	FRHITexture3D* DestTexture3D = DestTextureRHI->GetTexture3D();
	FRHITextureCube* DestTextureCube = DestTextureRHI->GetTextureCube();

	const FVulkanSurface* SrcSurface = nullptr;
	const FVulkanSurface* DstSurface = nullptr;
	uint32 SrcNumLayers, DstNumLayers;

	if (SourceTexture2D && DestTexture2D)
	{
		SrcSurface = &((FVulkanTexture2D*)SourceTexture2D)->Surface;
		DstSurface = &((FVulkanTexture2D*)DestTexture2D)->Surface;
		SrcNumLayers = DstNumLayers = 1;
	}
	else if (SourceTextureCube && DestTextureCube) 
	{
		SrcSurface = &((FVulkanTextureCube*)SourceTextureCube)->Surface;
		DstSurface = &((FVulkanTextureCube*)DestTextureCube)->Surface;
		SrcNumLayers = DstNumLayers = 6;
	}
	else if (SourceTexture2D && DestTextureCube) 
	{
		SrcSurface = &((FVulkanTexture2D*)SourceTexture2D)->Surface;
		DstSurface = &((FVulkanTextureCube*)DestTextureCube)->Surface;
		SrcNumLayers = 1;
		DstNumLayers = 6;
	}
	else if (SourceTexture3D && DestTexture3D) 
	{
		SrcSurface = &((FVulkanTexture3D*)SourceTexture3D)->Surface;
		DstSurface = &((FVulkanTexture3D*)DestTexture3D)->Surface;
		SrcNumLayers = DstNumLayers = 1;
	}
	else if (SourceTexture2DArray && DestTexture2DArray)
	{
		FVulkanTexture2DArray* VulkanSrcTexture = (FVulkanTexture2DArray*)SourceTexture2DArray;
		SrcSurface = &VulkanSrcTexture->Surface;
		SrcNumLayers = VulkanSrcTexture->GetSizeZ();

		FVulkanTexture2DArray* VulkanDstTexture = (FVulkanTexture2DArray*)DestTexture2DArray;
		DstSurface = &VulkanDstTexture->Surface;
		DstNumLayers = VulkanDstTexture->GetSizeZ();
	} 
	else 
	{
		checkNoEntry();
		return;
	}

	VkImageSubresourceRange SrcRange;
	SrcRange.aspectMask = SrcSurface->GetFullAspectMask();
	SrcRange.baseMipLevel = InResolveParams.MipIndex;
	SrcRange.levelCount = 1;
	SrcRange.baseArrayLayer = InResolveParams.SourceArrayIndex * SrcNumLayers + (SrcNumLayers == 6 ? InResolveParams.CubeFace : 0);
	SrcRange.layerCount = 1;

	VkImageSubresourceRange DstRange;
	DstRange.aspectMask = DstSurface->GetFullAspectMask();
	DstRange.baseMipLevel = InResolveParams.MipIndex;
	DstRange.levelCount = 1;
	DstRange.baseArrayLayer = InResolveParams.DestArrayIndex * DstNumLayers + (DstNumLayers == 6 ? InResolveParams.CubeFace : 0);
	DstRange.layerCount = 1;

	ERHIAccess SrcCurrentAccess, DstCurrentAccess;

	check((SrcSurface->UEFlags & TexCreate_CPUReadback) == 0);
	VkImageLayout& SrcLayout = LayoutManager.FindOrAddLayoutRW(*SrcSurface, VK_IMAGE_LAYOUT_UNDEFINED);

	if(DstSurface->UEFlags & TexCreate_CPUReadback)
	{
		//Readback textures are represented as a buffer, so we can support miplevels on hardware that does not expose it.
		FVulkanPipelineBarrier BarrierBefore;
		// We'll transition the entire resources to the correct copy states, so we don't need to worry about sub-resource states.
		if (SrcLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			BarrierBefore.AddImageLayoutTransition(SrcSurface->Image, SrcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, FVulkanPipelineBarrier::MakeSubresourceRange(SrcSurface->GetFullAspectMask()));
			SrcLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		}
		BarrierBefore.Execute(CmdBuffer->GetHandle());
		const FVulkanCpuReadbackBuffer* CpuReadbackBuffer = DstSurface->GetCpuReadbackBuffer();
		check(DstRange.baseArrayLayer == 0);
		VkBufferImageCopy CopyRegion;
		FMemory::Memzero(CopyRegion);
		uint32 MipLevel = InResolveParams.MipIndex;
		uint32 SizeX = FMath::Max(SrcSurface->Width >> MipLevel, 1u);
		uint32 SizeY = FMath::Max(SrcSurface->Height >> MipLevel, 1u);
		CopyRegion.bufferOffset = CpuReadbackBuffer->MipOffsets[MipLevel];
		CopyRegion.bufferRowLength = SizeX;
		CopyRegion.bufferImageHeight = SizeY;
		CopyRegion.imageSubresource.aspectMask = SrcSurface->GetFullAspectMask();
		CopyRegion.imageSubresource.mipLevel = MipLevel;
		CopyRegion.imageSubresource.baseArrayLayer = 0;
		CopyRegion.imageSubresource.layerCount = 1;
		CopyRegion.imageExtent.width = SizeX;
		CopyRegion.imageExtent.height = SizeY;
		CopyRegion.imageExtent.depth = 1;
		VulkanRHI::vkCmdCopyImageToBuffer(CmdBuffer->GetHandle(), SrcSurface->Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, CpuReadbackBuffer->Buffer, 1, &CopyRegion);



		{
			FVulkanPipelineBarrier BarrierMemory;
			BarrierMemory.MemoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			BarrierMemory.MemoryBarrier.pNext = nullptr;
			BarrierMemory.MemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			BarrierMemory.MemoryBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
			BarrierMemory.SrcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			BarrierMemory.DstStageMask = VK_PIPELINE_STAGE_HOST_BIT;
			BarrierMemory.Execute(CmdBuffer->GetHandle());
		}
		SrcCurrentAccess = ERHIAccess::CopySrc;
		if(SrcCurrentAccess != InResolveParams.SourceAccessFinal && InResolveParams.SourceAccessFinal != ERHIAccess::Unknown)
		{
			FVulkanPipelineBarrier BarrierAfter;
			BarrierAfter.AddImageAccessTransition(*SrcSurface, SrcCurrentAccess, InResolveParams.SourceAccessFinal, FVulkanPipelineBarrier::MakeSubresourceRange(SrcSurface->GetFullAspectMask()), SrcLayout);
			BarrierAfter.Execute(CmdBuffer->GetHandle());

		}
	}
	else
	{
		VkImageLayout& DstLayout = LayoutManager.FindOrAddLayoutRW(*DstSurface, VK_IMAGE_LAYOUT_UNDEFINED);
		if (SrcSurface->Image != DstSurface->Image)
		{
			const bool bIsResolve = SrcSurface->GetNumSamples() > DstSurface->GetNumSamples();
			checkf(!bIsResolve || !DstSurface->IsDepthOrStencilAspect(), TEXT("Vulkan does not support multisample depth resolve."));

			FVulkanPipelineBarrier BarrierBefore;
		
			// We'll transition the entire resources to the correct copy states, so we don't need to worry about sub-resource states.
			if (SrcLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
			{
				BarrierBefore.AddImageLayoutTransition(SrcSurface->Image, SrcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, FVulkanPipelineBarrier::MakeSubresourceRange(SrcSurface->GetFullAspectMask()));
				SrcLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			}

			if (DstLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
			{
				BarrierBefore.AddImageLayoutTransition(DstSurface->Image, DstLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, FVulkanPipelineBarrier::MakeSubresourceRange(DstSurface->GetFullAspectMask()));
				DstLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			}

			BarrierBefore.Execute(CmdBuffer->GetHandle());

			if (!bIsResolve)
			{
				VkImageCopy Region;
				SetupCopyOrResolveRegion(Region, *SrcSurface, *DstSurface, SrcRange, DstRange, InResolveParams);
				VulkanRHI::vkCmdCopyImage(CmdBuffer->GetHandle(),
					SrcSurface->Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					DstSurface->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &Region);
			}
			else
			{
				check(DstSurface->GetNumSamples() == 1);
				VkImageResolve Region;
				SetupCopyOrResolveRegion(Region, *SrcSurface, *DstSurface, SrcRange, DstRange, InResolveParams);
				VulkanRHI::vkCmdResolveImage(CmdBuffer->GetHandle(),
					SrcSurface->Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					DstSurface->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &Region);
			}

			SrcCurrentAccess = ERHIAccess::CopySrc;
			DstCurrentAccess = ERHIAccess::CopyDest;
		}
		else
		{
			SrcCurrentAccess = ERHIAccess::Unknown;
			DstCurrentAccess = ERHIAccess::Unknown;
		}

		if (InResolveParams.SourceAccessFinal != ERHIAccess::Unknown && InResolveParams.DestAccessFinal != ERHIAccess::Unknown)
		{
			FVulkanPipelineBarrier BarrierAfter;
			if (SrcSurface->Image != DstSurface->Image && SrcCurrentAccess != InResolveParams.SourceAccessFinal && InResolveParams.SourceAccessFinal != ERHIAccess::Unknown)
			{
				BarrierAfter.AddImageAccessTransition(*SrcSurface, SrcCurrentAccess, InResolveParams.SourceAccessFinal, FVulkanPipelineBarrier::MakeSubresourceRange(SrcSurface->GetFullAspectMask()), SrcLayout);
			}

			if (DstCurrentAccess != InResolveParams.DestAccessFinal && InResolveParams.DestAccessFinal != ERHIAccess::Unknown)
			{
				BarrierAfter.AddImageAccessTransition(*DstSurface, DstCurrentAccess, InResolveParams.DestAccessFinal, FVulkanPipelineBarrier::MakeSubresourceRange(DstSurface->GetFullAspectMask()), DstLayout);
			}

			BarrierAfter.Execute(CmdBuffer->GetHandle());
		}
	}

	if (GSubmitOnCopyToResolve)
	{
		InternalSubmitActiveCmdBuffer();
	}
}

void FVulkanDynamicRHI::RHIReadSurfaceData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FColor>& OutData, FReadSurfaceDataFlags InFlags)
{
	uint32 NumPixels = (Rect.Max.X - Rect.Min.X) * (Rect.Max.Y - Rect.Min.Y);
	if(GIgnoreCPUReads)
	{
		// Debug: Fill with CPU
		OutData.Empty(0);
		OutData.AddZeroed(NumPixels);
		return;
	}
	FRHITexture2D* TextureRHI2D = TextureRHI->GetTexture2D();
	check(TextureRHI2D);
	FVulkanTexture2D* Texture2D = (FVulkanTexture2D*)TextureRHI2D;
	FVulkanSurface* Surface = &Texture2D->Surface;

	Device->PrepareForCPURead();

	FVulkanCommandListContext& ImmediateContext = Device->GetImmediateContext();

	ensure(Texture2D->Surface.StorageFormat == VK_FORMAT_R8G8B8A8_UNORM || Texture2D->Surface.StorageFormat == VK_FORMAT_B8G8R8A8_UNORM || Texture2D->Surface.StorageFormat == VK_FORMAT_R16G16B16A16_SFLOAT || Texture2D->Surface.StorageFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32 || Texture2D->Surface.StorageFormat == VK_FORMAT_R16G16B16A16_UNORM);
	bool bIs8Bpp = true;
	switch (Texture2D->Surface.StorageFormat)
	{
	case VK_FORMAT_R16G16B16A16_SFLOAT:
	case VK_FORMAT_R16G16B16A16_SNORM:
	case VK_FORMAT_R16G16B16A16_UINT:
	case VK_FORMAT_R16G16B16A16_SINT:
		bIs8Bpp = false;
		break;
	default:
		break;
	}
	const uint32 Size = NumPixels * sizeof(FColor) * (bIs8Bpp ? 2 : 1);

	uint8* MappedPointer = nullptr;
	VulkanRHI::FStagingBuffer* StagingBuffer = nullptr;
	FVulkanCmdBuffer* CmdBuffer = nullptr;
	bool bCPUReadback = (Surface->UEFlags & TexCreate_CPUReadback) == TexCreate_CPUReadback;
	if(!bCPUReadback) //this function supports reading back arbitrary rendertargets, so if its not a cpu readback surface, we do a copy.
	{
		ImmediateContext.GetCommandBufferManager()->GetUploadCmdBuffer();
		CmdBuffer = ImmediateContext.GetCommandBufferManager()->GetUploadCmdBuffer();
		StagingBuffer = Device->GetStagingManager().AcquireBuffer(Size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		VkBufferImageCopy CopyRegion;
		FMemory::Memzero(CopyRegion);
		uint32 MipLevel = InFlags.GetMip();
		uint32 SizeX = FMath::Max(TextureRHI2D->GetSizeX() >> MipLevel, 1u);
		uint32 SizeY = FMath::Max(TextureRHI2D->GetSizeY() >> MipLevel, 1u);
		CopyRegion.bufferRowLength = SizeX;
		CopyRegion.bufferImageHeight = SizeY;
		CopyRegion.imageSubresource.aspectMask = Texture2D->Surface.GetFullAspectMask();
		CopyRegion.imageSubresource.mipLevel = MipLevel;
		CopyRegion.imageSubresource.layerCount = 1;
		CopyRegion.imageExtent.width = SizeX;
		CopyRegion.imageExtent.height = SizeY;
		CopyRegion.imageExtent.depth = 1;

		VkImageLayout& CurrentLayout = Device->GetImmediateContext().GetLayoutManager().FindOrAddLayoutRW(Texture2D->Surface, VK_IMAGE_LAYOUT_UNDEFINED);
		bool bHadLayout = (CurrentLayout != VK_IMAGE_LAYOUT_UNDEFINED);
		if (CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			VulkanSetImageLayoutAllMips(CmdBuffer->GetHandle(), Texture2D->Surface.Image, CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		}

		VulkanRHI::vkCmdCopyImageToBuffer(CmdBuffer->GetHandle(), Texture2D->Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, StagingBuffer->GetHandle(), 1, &CopyRegion);
		if (bHadLayout && CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			VulkanSetImageLayoutAllMips(CmdBuffer->GetHandle(), Texture2D->Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, CurrentLayout);
		}
		else
		{
			CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		}
		ensure(StagingBuffer->GetSize() >= Size);

		VkMemoryBarrier Barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER , nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT };
		VulkanRHI::vkCmdPipelineBarrier(CmdBuffer->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);

		// Force upload
		ImmediateContext.GetCommandBufferManager()->SubmitUploadCmdBuffer();

	}
	else
	{
		MappedPointer = (uint8*)Surface->GetMappedPointer();
	}



	Device->WaitUntilIdle();
	if(!bCPUReadback)
	{
		StagingBuffer->InvalidateMappedMemory();
		MappedPointer = (uint8*)StagingBuffer->GetMappedPointer();
	}

	OutData.SetNum(NumPixels);
	FColor* Dest = OutData.GetData();

	uint32 DestWidth = Rect.Max.X - Rect.Min.X;
	uint32 DestHeight = Rect.Max.Y - Rect.Min.Y;
	if (Texture2D->Surface.StorageFormat == VK_FORMAT_R16G16B16A16_SFLOAT)
	{
		uint32 PixelByteSize = 8u;
		uint8* In = MappedPointer + (Rect.Min.Y * TextureRHI2D->GetSizeX() + Rect.Min.X) * PixelByteSize;
		uint32 SrcPitch = TextureRHI2D->GetSizeX() * PixelByteSize;
		ConvertRawR16G16B16A16FDataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest, false);
	}
	else if (Texture2D->Surface.StorageFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32)
	{
		uint32 PixelByteSize = 4u;
		uint8* In = MappedPointer + (Rect.Min.Y * TextureRHI2D->GetSizeX() + Rect.Min.X) * PixelByteSize;
		uint32 SrcPitch = TextureRHI2D->GetSizeX() * PixelByteSize;
		ConvertRawR10G10B10A2DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
	}
	else if (Texture2D->Surface.StorageFormat == VK_FORMAT_R8G8B8A8_UNORM)
	{
		uint32 PixelByteSize = 4u;
		uint8* In = MappedPointer + (Rect.Min.Y * TextureRHI2D->GetSizeX() + Rect.Min.X) * PixelByteSize;
		uint32 SrcPitch = TextureRHI2D->GetSizeX() * PixelByteSize;
		ConvertRawR8G8B8A8DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
	}
	else if (Texture2D->Surface.StorageFormat == VK_FORMAT_R16G16B16A16_UNORM)
	{
		uint32 PixelByteSize = 8u;
		uint8* In = MappedPointer + (Rect.Min.Y * TextureRHI2D->GetSizeX() + Rect.Min.X) * PixelByteSize;
		uint32 SrcPitch = TextureRHI2D->GetSizeX() * PixelByteSize;
		ConvertRawR16G16B16A16DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
	}
	else if (Texture2D->Surface.StorageFormat == VK_FORMAT_B8G8R8A8_UNORM)
	{
		uint32 PixelByteSize = 4u;
		uint8* In = MappedPointer + (Rect.Min.Y * TextureRHI2D->GetSizeX() + Rect.Min.X) * PixelByteSize;
		uint32 SrcPitch = TextureRHI2D->GetSizeX() * PixelByteSize;
		ConvertRawB8G8R8A8DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
	}

	if (!bCPUReadback)
	{
		Device->GetStagingManager().ReleaseBuffer(CmdBuffer, StagingBuffer);
	}

	ImmediateContext.GetCommandBufferManager()->PrepareForNewActiveCommandBuffer();
}

void FVulkanDynamicRHI::RHIReadSurfaceData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FLinearColor>& OutData, FReadSurfaceDataFlags InFlags)
{
	TArray<FColor> FromColorData;
	RHIReadSurfaceData(TextureRHI, Rect, FromColorData, InFlags);
	for (FColor& From : FromColorData)
	{
		OutData.Emplace(FLinearColor(From));
	}
}

void FVulkanDynamicRHI::RHIMapStagingSurface(FRHITexture* TextureRHI, FRHIGPUFence* FenceRHI, void*& OutData, int32& OutWidth, int32& OutHeight, uint32 GPUIndex)
{
	FRHITexture2D* TextureRHI2D = TextureRHI->GetTexture2D();
	check(TextureRHI2D);
	FVulkanTexture2D* Texture2D = ResourceCast(TextureRHI2D);

	if (FenceRHI && !FenceRHI->Poll())
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		Device->SubmitCommandsAndFlushGPU();
		FVulkanGPUFence* Fence = ResourceCast(FenceRHI);
		Device->GetImmediateContext().GetCommandBufferManager()->WaitForCmdBuffer(Fence->GetCmdBuffer());
	}
	else
	{
		if(GVulkanFlushOnMapStaging)
		{
			FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			Device->WaitUntilIdle();
		}
	}


	check((Texture2D->Surface.UEFlags & TexCreate_CPUReadback) == TexCreate_CPUReadback);
	OutData = Texture2D->Surface.GetMappedPointer();
	Texture2D->Surface.InvalidateMappedMemory();
	OutWidth = Texture2D->GetSizeX();
	OutHeight = Texture2D->GetSizeY();
}

void FVulkanDynamicRHI::RHIUnmapStagingSurface(FRHITexture* TextureRHI, uint32 GPUIndex)
{
}

void FVulkanDynamicRHI::RHIReadSurfaceFloatData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FFloat16Color>& OutData, ECubeFace CubeFace,int32 ArrayIndex,int32 MipIndex)
{
	auto DoCopyFloat = [](FVulkanDevice* InDevice, FVulkanCmdBuffer* InCmdBuffer, const FVulkanSurface& Surface, uint32 InMipIndex, uint32 SrcBaseArrayLayer, FIntRect InRect, TArray<FFloat16Color>& OutputData)
	{
		ensure(Surface.StorageFormat == VK_FORMAT_R16G16B16A16_SFLOAT);

		uint32 NumPixels = (Surface.Width >> InMipIndex) * (Surface.Height >> InMipIndex);
		const uint32 Size = NumPixels * sizeof(FFloat16Color);
		VulkanRHI::FStagingBuffer* StagingBuffer = InDevice->GetStagingManager().AcquireBuffer(Size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

		if (GIgnoreCPUReads == 0)
		{
			VkBufferImageCopy CopyRegion;
			FMemory::Memzero(CopyRegion);
			//Region.bufferOffset = 0;
			CopyRegion.bufferRowLength = Surface.Width >> InMipIndex;
			CopyRegion.bufferImageHeight = Surface.Height >> InMipIndex;
			CopyRegion.imageSubresource.aspectMask = Surface.GetFullAspectMask();
			CopyRegion.imageSubresource.mipLevel = InMipIndex;
			CopyRegion.imageSubresource.baseArrayLayer = SrcBaseArrayLayer;
			CopyRegion.imageSubresource.layerCount = 1;
			CopyRegion.imageExtent.width = Surface.Width >> InMipIndex;
			CopyRegion.imageExtent.height = Surface.Height >> InMipIndex;
			CopyRegion.imageExtent.depth = 1;

			//#todo-rco: Multithreaded!
			VkImageLayout& CurrentLayout = InDevice->GetImmediateContext().GetLayoutManager().FindOrAddLayoutRW(Surface, VK_IMAGE_LAYOUT_UNDEFINED);
			bool bHadLayout = (CurrentLayout != VK_IMAGE_LAYOUT_UNDEFINED);
			if (CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
			{
				VulkanSetImageLayoutSimple(InCmdBuffer->GetHandle(), Surface.Image, CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			}

			VulkanRHI::vkCmdCopyImageToBuffer(InCmdBuffer->GetHandle(), Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, StagingBuffer->GetHandle(), 1, &CopyRegion);

			if (bHadLayout && CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
			{
				VulkanSetImageLayoutSimple(InCmdBuffer->GetHandle(), Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, CurrentLayout);
			}
			else
			{
				CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			}
		}
		else
		{
			VulkanRHI::vkCmdFillBuffer(InCmdBuffer->GetHandle(), StagingBuffer->GetHandle(), 0, Size, (FFloat16(1.0).Encoded << 16) + FFloat16(1.0).Encoded);
		}

		// the staging buffer size may be bigger then the size due to alignment, etc. but it must not be smaller!
		ensure(StagingBuffer->GetSize() >= Size);

		VkMemoryBarrier Barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER , nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT };
		VulkanRHI::vkCmdPipelineBarrier(InCmdBuffer->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);

		// Force upload
		InDevice->GetImmediateContext().GetCommandBufferManager()->SubmitUploadCmdBuffer();
		InDevice->WaitUntilIdle();

		StagingBuffer->InvalidateMappedMemory();

		uint32 OutWidth = InRect.Max.X - InRect.Min.X;
		uint32 OutHeight= InRect.Max.Y - InRect.Min.Y;
		OutputData.SetNum(OutWidth * OutHeight);
		uint32 OutIndex = 0;
		FFloat16Color* Dest = OutputData.GetData();
		for (int32 Row = InRect.Min.Y; Row < InRect.Max.Y; ++Row)
		{
			FFloat16Color* Src = (FFloat16Color*)StagingBuffer->GetMappedPointer() + Row * (Surface.Width >> InMipIndex) + InRect.Min.X;
			for (int32 Col = InRect.Min.X; Col < InRect.Max.X; ++Col)
			{
				OutputData[OutIndex++] = *Src++;
			}
		}
		InDevice->GetStagingManager().ReleaseBuffer(InCmdBuffer, StagingBuffer);
	};

	if(GIgnoreCPUReads == 1)
	{
		// Debug: Fill with CPU
		uint32 NumPixels = 0;
		if (TextureRHI->GetTextureCube())
		{
			FRHITextureCube* TextureRHICube = TextureRHI->GetTextureCube();
			FVulkanTextureCube* TextureCube = (FVulkanTextureCube*)TextureRHICube;
			NumPixels = (TextureCube->Surface.Width >> MipIndex) * (TextureCube->Surface.Height >> MipIndex);
		}
		else
		{
			FRHITexture2D* TextureRHI2D = TextureRHI->GetTexture2D();
			check(TextureRHI2D);
			FVulkanTexture2D* Texture2D = (FVulkanTexture2D*)TextureRHI2D;
			NumPixels = (Texture2D->Surface.Width >> MipIndex) * (Texture2D->Surface.Height >> MipIndex);
		}

		OutData.Empty(0);
		OutData.AddZeroed(NumPixels);
	}
	else
	{
		Device->PrepareForCPURead();

		FVulkanCmdBuffer* CmdBuffer = Device->GetImmediateContext().GetCommandBufferManager()->GetUploadCmdBuffer();
		if (TextureRHI->GetTextureCube())
		{
			FRHITextureCube* TextureRHICube = TextureRHI->GetTextureCube();
			FVulkanTextureCube* TextureCube = (FVulkanTextureCube*)TextureRHICube;
			DoCopyFloat(Device, CmdBuffer, TextureCube->Surface, MipIndex, CubeFace + 6 * ArrayIndex, Rect, OutData);
		}
		else
		{
			FRHITexture2D* TextureRHI2D = TextureRHI->GetTexture2D();
			check(TextureRHI2D);
			FVulkanTexture2D* Texture2D = (FVulkanTexture2D*)TextureRHI2D;
			DoCopyFloat(Device, CmdBuffer, Texture2D->Surface, MipIndex, ArrayIndex, Rect, OutData);
		}
		Device->GetImmediateContext().GetCommandBufferManager()->PrepareForNewActiveCommandBuffer();
	}
}

void FVulkanDynamicRHI::RHIRead3DSurfaceFloatData(FRHITexture* TextureRHI,FIntRect InRect,FIntPoint ZMinMax,TArray<FFloat16Color>& OutData)
{
	FRHITexture3D* TextureRHI3D = TextureRHI->GetTexture3D();
	check(TextureRHI3D);
	FVulkanTexture3D* Texture3D = (FVulkanTexture3D*)TextureRHI3D;
	FVulkanSurface& Surface = Texture3D->Surface;

	uint32 SizeX = InRect.Width();
	uint32 SizeY = InRect.Height();
	uint32 SizeZ = ZMinMax.Y - ZMinMax.X;
	uint32 NumPixels = SizeX * SizeY * SizeZ;
	const uint32 Size = NumPixels * sizeof(FFloat16Color);

	// Allocate the output buffer.
	OutData.Reserve(Size);
	if (GIgnoreCPUReads == 1)
	{
		OutData.AddZeroed(Size);

		// Debug: Fill with CPU
		return;
	}

	Device->PrepareForCPURead();
	FVulkanCmdBuffer* CmdBuffer = Device->GetImmediateContext().GetCommandBufferManager()->GetUploadCmdBuffer();

	ensure(Surface.StorageFormat == VK_FORMAT_R16G16B16A16_SFLOAT);

	VulkanRHI::FStagingBuffer* StagingBuffer = Device->GetStagingManager().AcquireBuffer(Size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
	if (GIgnoreCPUReads == 0)
	{
		VkBufferImageCopy CopyRegion;
		FMemory::Memzero(CopyRegion);
		//Region.bufferOffset = 0;
		CopyRegion.bufferRowLength = Surface.Width;
		CopyRegion.bufferImageHeight = Surface.Height;
		CopyRegion.imageSubresource.aspectMask = Surface.GetFullAspectMask();
		//CopyRegion.imageSubresource.mipLevel = 0;
		//CopyRegion.imageSubresource.baseArrayLayer = 0;
		CopyRegion.imageSubresource.layerCount = 1;
		CopyRegion.imageOffset.x = InRect.Min.X;
		CopyRegion.imageOffset.y = InRect.Min.Y;
		CopyRegion.imageOffset.z = ZMinMax.X;
		CopyRegion.imageExtent.width = SizeX;
		CopyRegion.imageExtent.height = SizeY;
		CopyRegion.imageExtent.depth = SizeZ;

		//#todo-rco: Multithreaded!
		VkImageLayout& CurrentLayout = Device->GetImmediateContext().GetLayoutManager().FindOrAddLayoutRW(Surface, VK_IMAGE_LAYOUT_UNDEFINED);
		bool bHadLayout = (CurrentLayout != VK_IMAGE_LAYOUT_UNDEFINED);
		if (CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			VulkanSetImageLayoutSimple(CmdBuffer->GetHandle(), Surface.Image, CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		}

		VulkanRHI::vkCmdCopyImageToBuffer(CmdBuffer->GetHandle(), Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, StagingBuffer->GetHandle(), 1, &CopyRegion);

		if (bHadLayout && CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			VulkanSetImageLayoutSimple(CmdBuffer->GetHandle(), Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, CurrentLayout);
		}
		else
		{
			CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		}
	}
	else
	{
		VulkanRHI::vkCmdFillBuffer(CmdBuffer->GetHandle(), StagingBuffer->GetHandle(), 0, Size, (FFloat16(1.0).Encoded << 16) + FFloat16(1.0).Encoded);
	}

	// the staging buffer size may be bigger then the size due to alignment, etc. but it must not be smaller!
	ensure(StagingBuffer->GetSize() >= Size);

	VkMemoryBarrier Barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER , nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT };
	VulkanRHI::vkCmdPipelineBarrier(CmdBuffer->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);

	// Force upload
	Device->GetImmediateContext().GetCommandBufferManager()->SubmitUploadCmdBuffer();
	Device->WaitUntilIdle();

	StagingBuffer->InvalidateMappedMemory();

	OutData.SetNum(NumPixels);
	FFloat16Color* Dest = OutData.GetData();
	for (int32 Layer = ZMinMax.X; Layer < ZMinMax.Y; ++Layer)
	{
		for (int32 Row = InRect.Min.Y; Row < InRect.Max.Y; ++Row)
		{
			FFloat16Color* Src = (FFloat16Color*)StagingBuffer->GetMappedPointer() + Layer * SizeX * SizeY + Row * Surface.Width + InRect.Min.X;
			for (int32 Col = InRect.Min.X; Col < InRect.Max.X; ++Col)
			{
				*Dest++ = *Src++;
			}
		}
	}
	FFloat16Color* End = OutData.GetData() + OutData.Num();
	checkf(Dest <= End, TEXT("Memory overwrite! Calculated total size %d: SizeX %d SizeY %d SizeZ %d; InRect(%d, %d, %d, %d) InZ(%d, %d)"),
		Size, SizeX, SizeY, SizeZ, InRect.Min.X, InRect.Min.Y, InRect.Max.X, InRect.Max.Y, ZMinMax.X, ZMinMax.Y);
	Device->GetStagingManager().ReleaseBuffer(CmdBuffer, StagingBuffer);
	Device->GetImmediateContext().GetCommandBufferManager()->PrepareForNewActiveCommandBuffer();
}

VkSurfaceTransformFlagBitsKHR FVulkanCommandListContext::GetSwapchainQCOMRenderPassTransform() const
{
	TArray<FVulkanViewport*>& viewports = RHI->GetViewports();
	if (viewports.Num() == 0)
	{
		return VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	}

	// check(viewports.Num() == 1);
	return viewports[0]->GetSwapchainQCOMRenderPassTransform();
}

VkFormat FVulkanCommandListContext::GetSwapchainImageFormat() const
{
	TArray<FVulkanViewport*>& viewports = RHI->GetViewports();
	if (viewports.Num() == 0)
	{
		return VK_FORMAT_UNDEFINED;
	}

	return viewports[0]->GetSwapchainImageFormat();
}

FVulkanSwapChain* FVulkanCommandListContext::GetSwapChain() const
{
	TArray<FVulkanViewport*>& viewports = RHI->GetViewports();
	uint32 numViewports = viewports.Num();

	if (viewports.Num() == 0)
	{
		return nullptr;
	}

	return viewports[0]->GetSwapChain();
}

bool FVulkanCommandListContext::IsSwapchainImage(FRHITexture* InTexture) const
{
	TArray<FVulkanViewport*>& viewports = RHI->GetViewports();
	uint32 numViewports = viewports.Num();

	for (uint32 i = 0; i < numViewports; i++)
	{
		for (int swapchainImageIdx = 0; swapchainImageIdx < FVulkanViewport::NUM_BUFFERS; swapchainImageIdx++)
		{
			VkImage Image = FVulkanTextureBase::Cast(InTexture)->Surface.Image;
			if (Image == viewports[i]->GetBackBufferImage(swapchainImageIdx))
			{
				return true;
			}
		}
	}
	return false;
}

void FVulkanCommandListContext::RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName)
{
	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();

	if (GVulkanSubmitAfterEveryEndRenderPass)
	{
		CommandBufferManager->SubmitActiveCmdBuffer();
		CommandBufferManager->PrepareForNewActiveCommandBuffer();
		CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	}
	else if (SafePointSubmit())
	{
		CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	}

	RenderPassInfo = InInfo;
	RHIPushEvent(InName ? InName : TEXT("<unnamed RenderPass>"), FColor::Green);
	if (InInfo.bOcclusionQueries)
	{
		BeginOcclusionQueryBatch(CmdBuffer, InInfo.NumOcclusionQueries);
	}

	FRHITexture* DSTexture = InInfo.DepthStencilRenderTarget.DepthStencilTarget;
	VkImageLayout CurrentDSLayout;
	if (DSTexture)
	{
		FVulkanSurface& Surface = FVulkanTextureBase::Cast(DSTexture)->Surface;
		CurrentDSLayout = LayoutManager.FindLayoutChecked(Surface.Image);
	}
	else
	{
		CurrentDSLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	FVulkanRenderTargetLayout RTLayout(*Device, InInfo, CurrentDSLayout);
	check(RTLayout.GetExtent2D().width != 0 && RTLayout.GetExtent2D().height != 0);

	FVulkanRenderPass* RenderPass = LayoutManager.GetOrCreateRenderPass(*Device, RTLayout);
	FRHISetRenderTargetsInfo RTInfo;
	InInfo.ConvertToRenderTargetsInfo(RTInfo);

	FVulkanFramebuffer* Framebuffer = LayoutManager.GetOrCreateFramebuffer(*Device, RTInfo, RTLayout, RenderPass);
	checkf(RenderPass != nullptr && Framebuffer != nullptr, TEXT("RenderPass not started! Bad combination of values? Depth %p #Color %d Color0 %p"), (void*)InInfo.DepthStencilRenderTarget.DepthStencilTarget, InInfo.GetNumColorRenderTargets(), (void*)InInfo.ColorRenderTargets[0].RenderTarget);
	LayoutManager.BeginRenderPass(*this, *Device, CmdBuffer, InInfo, RTLayout, RenderPass, Framebuffer);
}

void FVulkanCommandListContext::RHIEndRenderPass()
{
	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	if (RenderPassInfo.bOcclusionQueries)
	{
		EndOcclusionQueryBatch(CmdBuffer);
	}
	else
	{
		LayoutManager.EndRenderPass(CmdBuffer);
	}
	if(!RenderPassInfo.bIsMSAA)
	{
		for (int32 Index = 0; Index < MaxSimultaneousRenderTargets; ++Index)
		{
			if (RenderPassInfo.ColorRenderTargets[Index].ResolveTarget)
			{
				RHICopyToResolveTarget(RenderPassInfo.ColorRenderTargets[Index].RenderTarget, RenderPassInfo.ColorRenderTargets[Index].ResolveTarget, RenderPassInfo.ResolveParameters);
			}
			else
			{
				break;
			}
		}
		if (RenderPassInfo.DepthStencilRenderTarget.DepthStencilTarget && RenderPassInfo.DepthStencilRenderTarget.ResolveTarget)
		{
			RHICopyToResolveTarget(RenderPassInfo.DepthStencilRenderTarget.DepthStencilTarget, RenderPassInfo.DepthStencilRenderTarget.ResolveTarget, RenderPassInfo.ResolveParameters);
		}
	}
	RHIPopEvent();
}

void FVulkanCommandListContext::RHINextSubpass()
{
	check(LayoutManager.CurrentRenderPass);
	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	VkCommandBuffer Cmd = CmdBuffer->GetHandle();
	VulkanRHI::vkCmdNextSubpass(Cmd, VK_SUBPASS_CONTENTS_INLINE);
}

// Need a separate struct so we can memzero/remove dependencies on reference counts
struct FRenderPassCompatibleHashableStruct
{
	FRenderPassCompatibleHashableStruct()
	{
		FMemory::Memzero(*this);
	}

	uint8							NumAttachments;
	uint8							MultiViewCount;
	uint8							NumSamples;
	uint8							SubpassHint;
	VkSurfaceTransformFlagBitsKHR	QCOMRenderPassTransform;
	// +1 for DepthStencil, +1 for Fragment Density
	VkFormat						Formats[MaxSimultaneousRenderTargets + 2];
	uint16							AttachmentsToResolve;
};

// Need a separate struct so we can memzero/remove dependencies on reference counts
struct FRenderPassFullHashableStruct
{
	FRenderPassFullHashableStruct()
	{
		FMemory::Memzero(*this);
	}

	// +1 for Depth, +1 for Stencil, +1 for Fragment Density
	TEnumAsByte<VkAttachmentLoadOp>		LoadOps[MaxSimultaneousRenderTargets + 3];
	TEnumAsByte<VkAttachmentStoreOp>	StoreOps[MaxSimultaneousRenderTargets + 3];
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
	// If the initial != final we need to add FinalLayout and potentially RefLayout
	VkImageLayout						InitialLayout[MaxSimultaneousRenderTargets + 2];
	//VkImageLayout						FinalLayout[MaxSimultaneousRenderTargets + 2];
	//VkImageLayout						RefLayout[MaxSimultaneousRenderTargets + 2];
#endif
};


FVulkanRenderTargetLayout::FVulkanRenderTargetLayout(FVulkanDevice& InDevice, const FRHISetRenderTargetsInfo& RTInfo)
	: NumAttachmentDescriptions(0)
	, NumColorAttachments(0)
	, bHasDepthStencil(false)
	, bHasResolveAttachments(false)
	, bHasFragmentDensityAttachment(false)
	, NumSamples(0)
	, NumUsedClearValues(0)
	, MultiViewCount(0)
{
	FMemory::Memzero(ColorReferences);
	FMemory::Memzero(DepthStencilReference);
	FMemory::Memzero(FragmentDensityReference);
	FMemory::Memzero(ResolveReferences);
	FMemory::Memzero(InputAttachments);
	FMemory::Memzero(Desc);
	FMemory::Memzero(Extent);

	FRenderPassCompatibleHashableStruct CompatibleHashInfo;
	FRenderPassFullHashableStruct FullHashInfo;

	bool bSetExtent = false;
	bool bFoundClearOp = false;
	for (int32 Index = 0; Index < RTInfo.NumColorRenderTargets; ++Index)
	{
		const FRHIRenderTargetView& RTView = RTInfo.ColorRenderTarget[Index];
		if (RTView.Texture)
		{
			FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(RTView.Texture);
			check(Texture);

			if (InDevice.GetImmediateContext().IsSwapchainImage(RTView.Texture))
			{
				QCOMRenderPassTransform = InDevice.GetImmediateContext().GetSwapchainQCOMRenderPassTransform();
			}

			if (bSetExtent)
			{
				ensure(Extent.Extent3D.width == FMath::Max(1u, Texture->Surface.Width >> RTView.MipIndex));
				ensure(Extent.Extent3D.height == FMath::Max(1u, Texture->Surface.Height >> RTView.MipIndex));
				ensure(Extent.Extent3D.depth == Texture->Surface.Depth);
			}
			else
			{
				bSetExtent = true;
				Extent.Extent3D.width = FMath::Max(1u, Texture->Surface.Width >> RTView.MipIndex);
				Extent.Extent3D.height = FMath::Max(1u, Texture->Surface.Height >> RTView.MipIndex);
				Extent.Extent3D.depth = Texture->Surface.Depth;
			}

			FVulkanSurface* Surface = &Texture->Surface;

			ensure(!NumSamples || NumSamples == Surface->GetNumSamples());
			NumSamples = Surface->GetNumSamples();
		
			VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
			CurrDesc.samples = static_cast<VkSampleCountFlagBits>(NumSamples);
			CurrDesc.format = UEToVkTextureFormat(RTView.Texture->GetFormat(), (Texture->Surface.UEFlags & TexCreate_SRGB) == TexCreate_SRGB);
			CurrDesc.loadOp = RenderTargetLoadActionToVulkan(RTView.LoadAction);
			bFoundClearOp = bFoundClearOp || (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
			CurrDesc.storeOp = RenderTargetStoreActionToVulkan(RTView.StoreAction);
			CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

			// Removed this temporarily as we need a way to determine if the target is actually memoryless
			/*if (Texture->Surface.UEFlags & TexCreate_Memoryless)
			{
				ensure(CurrDesc.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
			}*/

			// If the initial != final we need to change the FullHashInfo and use FinalLayout
			CurrDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			CurrDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			ColorReferences[NumColorAttachments].attachment = NumAttachmentDescriptions;
			ColorReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			const bool bHasValidResolveAttachment = RTInfo.bHasResolveAttachments && RTInfo.ColorResolveRenderTarget[Index].Texture;
			if (CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT && bHasValidResolveAttachment)
			{
				Desc[NumAttachmentDescriptions + 1] = Desc[NumAttachmentDescriptions];
				Desc[NumAttachmentDescriptions + 1].samples = VK_SAMPLE_COUNT_1_BIT;
				Desc[NumAttachmentDescriptions + 1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				Desc[NumAttachmentDescriptions + 1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				ResolveReferences[NumColorAttachments].attachment = NumAttachmentDescriptions + 1;
				ResolveReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				CompatibleHashInfo.AttachmentsToResolve |= (uint16)(1 << NumColorAttachments);
				++NumAttachmentDescriptions;
				bHasResolveAttachments = true;
			}

			CompatibleHashInfo.Formats[NumColorAttachments] = CurrDesc.format;
			FullHashInfo.LoadOps[NumColorAttachments] = CurrDesc.loadOp;
			FullHashInfo.StoreOps[NumColorAttachments] = CurrDesc.storeOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
			FullHashInfo.InitialLayout[NumColorAttachments] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
#endif
			++CompatibleHashInfo.NumAttachments;

			++NumAttachmentDescriptions;
			++NumColorAttachments;
		}
	}

	VkImageLayout DepthStencilLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (RTInfo.DepthStencilRenderTarget.Texture)
	{
		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);
		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(RTInfo.DepthStencilRenderTarget.Texture);
		check(Texture);

		FVulkanSurface* Surface = &Texture->Surface;
		ensure(!NumSamples || NumSamples == Surface->GetNumSamples());
		NumSamples = Surface->GetNumSamples();

		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(NumSamples);
		CurrDesc.format = UEToVkTextureFormat(RTInfo.DepthStencilRenderTarget.Texture->GetFormat(), false);
		CurrDesc.loadOp = RenderTargetLoadActionToVulkan(RTInfo.DepthStencilRenderTarget.DepthLoadAction);
		CurrDesc.stencilLoadOp = RenderTargetLoadActionToVulkan(RTInfo.DepthStencilRenderTarget.StencilLoadAction);
		bFoundClearOp = bFoundClearOp || (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR || CurrDesc.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
		if (CurrDesc.samples == VK_SAMPLE_COUNT_1_BIT)
		{
			CurrDesc.storeOp = RenderTargetStoreActionToVulkan(RTInfo.DepthStencilRenderTarget.DepthStoreAction);
			CurrDesc.stencilStoreOp = RenderTargetStoreActionToVulkan(RTInfo.DepthStencilRenderTarget.GetStencilStoreAction());

			// Removed this temporarily as we need a way to determine if the target is actually memoryless
			/*if (Texture->Surface.UEFlags & TexCreate_Memoryless)
			{
				ensure(CurrDesc.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
				ensure(CurrDesc.stencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
			}*/
		}
		else
		{
			// Never want to store MSAA depth/stencil
			CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}

		DepthStencilLayout = VulkanRHI::GetDepthStencilLayout(RTInfo.DepthStencilRenderTarget.GetDepthStencilAccess(), InDevice);

		// If the initial != final we need to change the FullHashInfo and use FinalLayout
		CurrDesc.initialLayout = DepthStencilLayout;
		CurrDesc.finalLayout = DepthStencilLayout;

		DepthStencilReference.attachment = NumAttachmentDescriptions;
		DepthStencilReference.layout = DepthStencilLayout;

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets] = CurrDesc.loadOp;
		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets] = CurrDesc.storeOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilStoreOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets] = DepthStencilLayout;
#endif
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets] = CurrDesc.format;

		++NumAttachmentDescriptions;

		bHasDepthStencil = true;

		if (bSetExtent)
		{
			// Depth can be greater or equal to color. Clamp to the smaller size.
			Extent.Extent3D.width = FMath::Min<uint32>(Extent.Extent3D.width, Texture->Surface.Width);
			Extent.Extent3D.height = FMath::Min<uint32>(Extent.Extent3D.height, Texture->Surface.Height);
		}
		else
		{
			bSetExtent = true;
			Extent.Extent3D.width = Texture->Surface.Width;
			Extent.Extent3D.height = Texture->Surface.Height;
			Extent.Extent3D.depth = Texture->Surface.GetNumberOfArrayLevels();
		}
	}

	if (InDevice.GetOptionalExtensions().HasEXTFragmentDensityMap && RTInfo.ShadingRateTexture)
	{
		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(RTInfo.ShadingRateTexture);
		check(Texture);

		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);

		const VkImageLayout FragmentDensityLayout = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;

		CurrDesc.flags = 0;
		CurrDesc.format = UEToVkTextureFormat(RTInfo.ShadingRateTexture->GetFormat(), false);
		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(RTInfo.ShadingRateTexture->GetNumSamples());
		CurrDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.initialLayout = FragmentDensityLayout;
		CurrDesc.finalLayout = FragmentDensityLayout;

		FragmentDensityReference.attachment = NumAttachmentDescriptions;
		FragmentDensityReference.layout = FragmentDensityLayout;

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilStoreOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets + 1] = FragmentDensityLayout;
#endif
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets + 1] = CurrDesc.format;

		++NumAttachmentDescriptions;
		bHasFragmentDensityAttachment = true;
	}

	SubpassHint = ESubpassHint::None;
	CompatibleHashInfo.SubpassHint = 0;

	CompatibleHashInfo.QCOMRenderPassTransform = QCOMRenderPassTransform;

	CompatibleHashInfo.NumSamples = NumSamples;
	CompatibleHashInfo.MultiViewCount = MultiViewCount;

	RenderPassCompatibleHash = FCrc::MemCrc32(&CompatibleHashInfo, sizeof(CompatibleHashInfo));
	RenderPassFullHash = FCrc::MemCrc32(&FullHashInfo, sizeof(FullHashInfo), RenderPassCompatibleHash);
	NumUsedClearValues = bFoundClearOp ? NumAttachmentDescriptions : 0;
	bCalculatedHash = true;
}


FVulkanRenderTargetLayout::FVulkanRenderTargetLayout(FVulkanDevice& InDevice, const FRHIRenderPassInfo& RPInfo, VkImageLayout CurrentDSLayout)
	: NumAttachmentDescriptions(0)
	, NumColorAttachments(0)
	, bHasDepthStencil(false)
	, bHasResolveAttachments(false)
	, bHasFragmentDensityAttachment(false)
	, NumSamples(0)
	, NumUsedClearValues(0)
	, MultiViewCount(RPInfo.MultiViewCount)
{
	FMemory::Memzero(ColorReferences);
	FMemory::Memzero(DepthStencilReference);
	FMemory::Memzero(FragmentDensityReference);
	FMemory::Memzero(ResolveReferences);
	FMemory::Memzero(InputAttachments);
	FMemory::Memzero(Desc);
	FMemory::Memzero(Extent);

	FRenderPassCompatibleHashableStruct CompatibleHashInfo;
	FRenderPassFullHashableStruct FullHashInfo;

	bool bSetExtent = false;
	bool bFoundClearOp = false;
	bool bMultiviewRenderTargets = false;

	int32 NumColorRenderTargets = RPInfo.GetNumColorRenderTargets();
	for (int32 Index = 0; Index < NumColorRenderTargets; ++Index)
	{
		const FRHIRenderPassInfo::FColorEntry& ColorEntry = RPInfo.ColorRenderTargets[Index];
		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(ColorEntry.RenderTarget);
		check(Texture);

		if (InDevice.GetImmediateContext().IsSwapchainImage(ColorEntry.RenderTarget))
		{
			QCOMRenderPassTransform = InDevice.GetImmediateContext().GetSwapchainQCOMRenderPassTransform();
		}
		check(QCOMRenderPassTransform == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR || NumAttachmentDescriptions == 0);

		if (bSetExtent)
		{
			ensure(Extent.Extent3D.width == FMath::Max(1u, Texture->Surface.Width >> ColorEntry.MipIndex));
			ensure(Extent.Extent3D.height == FMath::Max(1u, Texture->Surface.Height >> ColorEntry.MipIndex));
			ensure(Extent.Extent3D.depth == Texture->Surface.Depth);
		}
		else
		{
			bSetExtent = true;
			Extent.Extent3D.width = FMath::Max(1u, Texture->Surface.Width >> ColorEntry.MipIndex);
			Extent.Extent3D.height = FMath::Max(1u, Texture->Surface.Height >> ColorEntry.MipIndex);
			Extent.Extent3D.depth = Texture->Surface.Depth;
		}

		ensure(!NumSamples || NumSamples == ColorEntry.RenderTarget->GetNumSamples());
		NumSamples = ColorEntry.RenderTarget->GetNumSamples();

		ensure( !bMultiviewRenderTargets || Texture->Surface.GetNumberOfArrayLevels() > 1 );
		bMultiviewRenderTargets = Texture->Surface.GetNumberOfArrayLevels() > 1;

		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(NumSamples);
		CurrDesc.format = UEToVkTextureFormat(ColorEntry.RenderTarget->GetFormat(), (Texture->Surface.UEFlags & TexCreate_SRGB) == TexCreate_SRGB);
		CurrDesc.loadOp = RenderTargetLoadActionToVulkan(GetLoadAction(ColorEntry.Action));
		bFoundClearOp = bFoundClearOp || (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
		CurrDesc.storeOp = RenderTargetStoreActionToVulkan(GetStoreAction(ColorEntry.Action));
		CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		if (Texture->Surface.UEFlags & TexCreate_Memoryless)
		{
			ensure(CurrDesc.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
		}

		// If the initial != final we need to change the FullHashInfo and use FinalLayout
		CurrDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		CurrDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		ColorReferences[NumColorAttachments].attachment = NumAttachmentDescriptions;
		ColorReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		if (CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT && ColorEntry.ResolveTarget)
		{
			Desc[NumAttachmentDescriptions + 1] = Desc[NumAttachmentDescriptions];
			Desc[NumAttachmentDescriptions + 1].samples = VK_SAMPLE_COUNT_1_BIT;
			Desc[NumAttachmentDescriptions + 1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			Desc[NumAttachmentDescriptions + 1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			ResolveReferences[NumColorAttachments].attachment = NumAttachmentDescriptions + 1;
			ResolveReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			CompatibleHashInfo.AttachmentsToResolve |= (uint16)(1 << NumColorAttachments);
			++NumAttachmentDescriptions;
			bHasResolveAttachments = true;
		}

		CompatibleHashInfo.Formats[NumColorAttachments] = CurrDesc.format;
		FullHashInfo.LoadOps[NumColorAttachments] = CurrDesc.loadOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
		FullHashInfo.InitialLayout[NumColorAttachments] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
#endif
		FullHashInfo.StoreOps[NumColorAttachments] = CurrDesc.storeOp;
		++CompatibleHashInfo.NumAttachments;

		++NumAttachmentDescriptions;
		++NumColorAttachments;
	}

	VkImageLayout DepthStencilLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (RPInfo.DepthStencilRenderTarget.DepthStencilTarget)
	{
		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);
		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(RPInfo.DepthStencilRenderTarget.DepthStencilTarget);
		check(Texture);

		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(RPInfo.DepthStencilRenderTarget.DepthStencilTarget->GetNumSamples());
		ensure(!NumSamples || CurrDesc.samples == NumSamples);
		NumSamples = CurrDesc.samples;
		CurrDesc.format = UEToVkTextureFormat(RPInfo.DepthStencilRenderTarget.DepthStencilTarget->GetFormat(), false);
		CurrDesc.loadOp = RenderTargetLoadActionToVulkan(GetLoadAction(GetDepthActions(RPInfo.DepthStencilRenderTarget.Action)));
		CurrDesc.stencilLoadOp = RenderTargetLoadActionToVulkan(GetLoadAction(GetStencilActions(RPInfo.DepthStencilRenderTarget.Action)));
		bFoundClearOp = bFoundClearOp || (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR || CurrDesc.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
		if (CurrDesc.samples != VK_SAMPLE_COUNT_1_BIT)
		{
			// Can't resolve MSAA depth/stencil
			ensure(GetStoreAction(GetDepthActions(RPInfo.DepthStencilRenderTarget.Action)) != ERenderTargetStoreAction::EMultisampleResolve);
			ensure(GetStoreAction(GetStencilActions(RPInfo.DepthStencilRenderTarget.Action)) != ERenderTargetStoreAction::EMultisampleResolve);
		}

		CurrDesc.storeOp = RenderTargetStoreActionToVulkan(GetStoreAction(GetDepthActions(RPInfo.DepthStencilRenderTarget.Action)));
		CurrDesc.stencilStoreOp = RenderTargetStoreActionToVulkan(GetStoreAction(GetStencilActions(RPInfo.DepthStencilRenderTarget.Action)));

		if (Texture->Surface.UEFlags & TexCreate_Memoryless)
		{
			ensure(CurrDesc.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
			ensure(CurrDesc.stencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
		}

		FExclusiveDepthStencil ExclusiveDepthStencil = RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil;
		if (FVulkanPlatform::RequiresDepthWriteOnStencilClear() &&
			RPInfo.DepthStencilRenderTarget.Action == EDepthStencilTargetActions::LoadDepthClearStencil_StoreDepthStencil)
		{
			ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;
		}

		// Make sure that the requested depth-stencil access is compatible with the current layout of the DS target.
		const bool bWritableDepth = ExclusiveDepthStencil.IsDepthWrite();
		const bool bWritableStencil = ExclusiveDepthStencil.IsStencilWrite();
		switch (CurrentDSLayout)
		{
			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
				// Writable depth-stencil is compatible with all the requested modes.
				break;

			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
				// Read-only on both aspects requires the requested access to be read-only.
				ensureMsgf(!bWritableDepth && !bWritableStencil, TEXT("Both aspects of the DS target are read-only, but the requested mode requires write access: D=%s S=%s."),
					ExclusiveDepthStencil.IsUsingDepth() ? (ExclusiveDepthStencil.IsDepthWrite() ? TEXT("Write") : TEXT("Read")) : TEXT("Nop"),
					ExclusiveDepthStencil.IsUsingStencil() ? (ExclusiveDepthStencil.IsStencilWrite() ? TEXT("Write") : TEXT("Read")) : TEXT("Nop")
				);
				break;

			case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL_KHR:
				// If only stencil is writable, the requested depth access must be read-only.
				ensureMsgf(!bWritableDepth, TEXT("The depth aspect is read-only, but the requested mode requires depth writes: D=%s S=%s."),
					ExclusiveDepthStencil.IsUsingDepth() ? (ExclusiveDepthStencil.IsDepthWrite() ? TEXT("Write") : TEXT("Read")) : TEXT("Nop"),
					ExclusiveDepthStencil.IsUsingStencil() ? (ExclusiveDepthStencil.IsStencilWrite() ? TEXT("Write") : TEXT("Read")) : TEXT("Nop")
				);
				break;

			case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL_KHR:
				// If only depth is writable, the requested stencil access must be read-only.
				ensureMsgf(!bWritableStencil, TEXT("The stencil aspect is read-only, but the requested mode requires stencil writes: D=%s S=%s."),
					ExclusiveDepthStencil.IsUsingDepth() ? (ExclusiveDepthStencil.IsDepthWrite() ? TEXT("Write") : TEXT("Read")) : TEXT("Nop"),
					ExclusiveDepthStencil.IsUsingStencil() ? (ExclusiveDepthStencil.IsStencilWrite() ? TEXT("Write") : TEXT("Read")) : TEXT("Nop")
				);
				break;

			default:
				// Any other layout is invalid when starting a render pass.
				ensureMsgf(false, TEXT("Depth target is in layout %u, which is invalid for a render pass."), CurrentDSLayout);
				break;
		}

		DepthStencilLayout = CurrentDSLayout;

		// If the initial != final we need to change the FullHashInfo and use FinalLayout
		CurrDesc.initialLayout = DepthStencilLayout;
		CurrDesc.finalLayout = DepthStencilLayout;
		DepthStencilReference.attachment = NumAttachmentDescriptions;
		DepthStencilReference.layout = DepthStencilLayout;

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets] = CurrDesc.loadOp;
		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets] = CurrDesc.storeOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilStoreOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets] = DepthStencilLayout;
#endif
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets] = CurrDesc.format;

		++NumAttachmentDescriptions;

		bHasDepthStencil = true;

		if (bSetExtent)
		{
			// Depth can be greater or equal to color. Clamp to the smaller size.
			Extent.Extent3D.width = FMath::Min<uint32>(Extent.Extent3D.width, Texture->Surface.Width);
			Extent.Extent3D.height = FMath::Min<uint32>(Extent.Extent3D.height, Texture->Surface.Height);
		}
		else
		{
			bSetExtent = true;
			Extent.Extent3D.width = Texture->Surface.Width;
			Extent.Extent3D.height = Texture->Surface.Height;
			Extent.Extent3D.depth = Texture->Surface.Depth;
		}
	}

	if (InDevice.GetOptionalExtensions().HasEXTFragmentDensityMap && RPInfo.ShadingRateTexture)
	{
		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(RPInfo.ShadingRateTexture);
		check(Texture);

		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);

		const VkImageLayout FragmentDensityLayout = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;

		CurrDesc.flags = 0;
		CurrDesc.format = UEToVkTextureFormat(RPInfo.ShadingRateTexture->GetFormat(), false);
		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(RPInfo.ShadingRateTexture->GetNumSamples());
		CurrDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.initialLayout = FragmentDensityLayout;
		CurrDesc.finalLayout = FragmentDensityLayout;

		FragmentDensityReference.attachment = NumAttachmentDescriptions;
		FragmentDensityReference.layout = FragmentDensityLayout;

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilStoreOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets + 1] = FragmentDensityLayout;
#endif
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets + 1] = CurrDesc.format;

		++NumAttachmentDescriptions;
		bHasFragmentDensityAttachment = true;
	}

	SubpassHint = RPInfo.SubpassHint;
	CompatibleHashInfo.SubpassHint = (uint8)RPInfo.SubpassHint;

	CompatibleHashInfo.QCOMRenderPassTransform = QCOMRenderPassTransform;

	CompatibleHashInfo.NumSamples = NumSamples;
	CompatibleHashInfo.MultiViewCount = MultiViewCount;

	if (MultiViewCount > 1 && !bMultiviewRenderTargets)
	{
		UE_LOG(LogVulkan, Error, TEXT("Non multiview textures on a multiview layout!"));
	}

	RenderPassCompatibleHash = FCrc::MemCrc32(&CompatibleHashInfo, sizeof(CompatibleHashInfo));
	RenderPassFullHash = FCrc::MemCrc32(&FullHashInfo, sizeof(FullHashInfo), RenderPassCompatibleHash);
	NumUsedClearValues = bFoundClearOp ? NumAttachmentDescriptions : 0;
	bCalculatedHash = true;
}

FVulkanRenderTargetLayout::FVulkanRenderTargetLayout(const FGraphicsPipelineStateInitializer& Initializer)
	: NumAttachmentDescriptions(0)
	, NumColorAttachments(0)
	, bHasDepthStencil(false)
	, bHasResolveAttachments(false)
	, bHasFragmentDensityAttachment(false)
	, NumSamples(0)
	, NumUsedClearValues(0)
	, MultiViewCount(0)
{
	FMemory::Memzero(ColorReferences);
	FMemory::Memzero(DepthStencilReference);
	FMemory::Memzero(FragmentDensityReference);
	FMemory::Memzero(ResolveReferences);
	FMemory::Memzero(InputAttachments);
	FMemory::Memzero(Desc);
	FMemory::Memzero(Extent);

	FRenderPassCompatibleHashableStruct CompatibleHashInfo;
	FRenderPassFullHashableStruct FullHashInfo;

	bool bSetExtent = false;
	bool bFoundClearOp = false;
	MultiViewCount = Initializer.MultiViewCount;
	NumSamples = Initializer.NumSamples;
	for (uint32 Index = 0; Index < Initializer.RenderTargetsEnabled; ++Index)
	{
		EPixelFormat UEFormat = (EPixelFormat)Initializer.RenderTargetFormats[Index];
		if (UEFormat != PF_Unknown)
		{
			VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
			CurrDesc.samples = static_cast<VkSampleCountFlagBits>(NumSamples);
			CurrDesc.format = UEToVkTextureFormat(UEFormat, (Initializer.RenderTargetFlags[Index] & TexCreate_SRGB) == TexCreate_SRGB);
			CurrDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

			// If the initial != final we need to change the FullHashInfo and use FinalLayout
			CurrDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			CurrDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			ColorReferences[NumColorAttachments].attachment = NumAttachmentDescriptions;
			ColorReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			if (CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT)
			{
				Desc[NumAttachmentDescriptions + 1] = Desc[NumAttachmentDescriptions];
				Desc[NumAttachmentDescriptions + 1].samples = VK_SAMPLE_COUNT_1_BIT;
				Desc[NumAttachmentDescriptions + 1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				Desc[NumAttachmentDescriptions + 1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				ResolveReferences[NumColorAttachments].attachment = NumAttachmentDescriptions + 1;
				ResolveReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				CompatibleHashInfo.AttachmentsToResolve |= (uint16)(1 << NumColorAttachments);
				++NumAttachmentDescriptions;
				bHasResolveAttachments = true;
			}

			CompatibleHashInfo.Formats[NumColorAttachments] = CurrDesc.format;
			FullHashInfo.LoadOps[NumColorAttachments] = CurrDesc.loadOp;
			FullHashInfo.StoreOps[NumColorAttachments] = CurrDesc.storeOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
			FullHashInfo.InitialLayout[NumColorAttachments] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
#endif
			++CompatibleHashInfo.NumAttachments;

			++NumAttachmentDescriptions;
			++NumColorAttachments;
		}
	}

	if (Initializer.DepthStencilTargetFormat != PF_Unknown)
	{
		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);

		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(NumSamples);
		CurrDesc.format = UEToVkTextureFormat(Initializer.DepthStencilTargetFormat, false);
		CurrDesc.loadOp = RenderTargetLoadActionToVulkan(Initializer.DepthTargetLoadAction);
		CurrDesc.stencilLoadOp = RenderTargetLoadActionToVulkan(Initializer.StencilTargetLoadAction);
		if (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR || CurrDesc.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
		{
			bFoundClearOp = true;
		}
		if (CurrDesc.samples == VK_SAMPLE_COUNT_1_BIT)
		{
			CurrDesc.storeOp = RenderTargetStoreActionToVulkan(Initializer.DepthTargetStoreAction);
			CurrDesc.stencilStoreOp = RenderTargetStoreActionToVulkan(Initializer.StencilTargetStoreAction);
		}
		else
		{
			// Never want to store MSAA depth/stencil
			CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}

		// If the initial != final we need to change the FullHashInfo and use FinalLayout
		CurrDesc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		CurrDesc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		DepthStencilReference.attachment = NumAttachmentDescriptions;
		DepthStencilReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets] = CurrDesc.loadOp;
		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets] = CurrDesc.storeOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilStoreOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets] = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
#endif
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets] = CurrDesc.format;

		++NumAttachmentDescriptions;
		bHasDepthStencil = true;
	}

	if (Initializer.bHasFragmentDensityAttachment)
	{
		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);

		const VkImageLayout FragmentDensityLayout = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;

		CurrDesc.flags = 0;
		CurrDesc.format = VK_FORMAT_R8G8_UNORM;
		CurrDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		CurrDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.initialLayout = FragmentDensityLayout;
		CurrDesc.finalLayout = FragmentDensityLayout;

		FragmentDensityReference.attachment = NumAttachmentDescriptions;
		FragmentDensityReference.layout = FragmentDensityLayout;

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilStoreOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets + 1] = FragmentDensityLayout;
#endif
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets + 1] = CurrDesc.format;

		++NumAttachmentDescriptions;
		bHasFragmentDensityAttachment = true;
	}

	SubpassHint = Initializer.SubpassHint;
	CompatibleHashInfo.SubpassHint = (uint8)Initializer.SubpassHint;

	FVulkanCommandListContext& ImmediateContext = GVulkanRHI->GetDevice()->GetImmediateContext();

	if (GVulkanRHI->GetDevice()->GetOptionalExtensions().HasQcomRenderPassTransform)
	{
		VkFormat SwapchainImageFormat = ImmediateContext.GetSwapchainImageFormat();
		if (Desc[0].format == SwapchainImageFormat)
		{
			// Potential Swapchain RenderPass
			QCOMRenderPassTransform = ImmediateContext.GetSwapchainQCOMRenderPassTransform();
		}
		// TODO: add some checks to detect potential Swapchain pass
		else if (SwapchainImageFormat == VK_FORMAT_UNDEFINED)
		{
			// WA: to have compatible RP created with VK_RENDER_PASS_CREATE_TRANSFORM_BIT_QCOM flag
			QCOMRenderPassTransform = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
		}
	}

	CompatibleHashInfo.QCOMRenderPassTransform = QCOMRenderPassTransform;

	CompatibleHashInfo.NumSamples = NumSamples;
	CompatibleHashInfo.MultiViewCount = MultiViewCount;

	RenderPassCompatibleHash = FCrc::MemCrc32(&CompatibleHashInfo, sizeof(CompatibleHashInfo));
	RenderPassFullHash = FCrc::MemCrc32(&FullHashInfo, sizeof(FullHashInfo), RenderPassCompatibleHash);
	NumUsedClearValues = bFoundClearOp ? NumAttachmentDescriptions : 0;
	bCalculatedHash = true;
}
