// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapCustomPresentVk.h"
#include "MagicLeapHMD.h"
#include "RenderingThread.h"
#include "Lumin/CAPIShims/LuminAPI.h"

#include "MagicLeapGraphics.h"

#if PLATFORM_WINDOWS || PLATFORM_LUMIN
#include "XRThreadUtils.h"
#include "MagicLeapHelperVulkan.h"
#include "VulkanRHIBridge.h"
#include "VulkanContext.h"
#endif // PLATFORM_WINDOWS || PLATFORM_LUMIN

#include "Containers/Union.h"

#if PLATFORM_WINDOWS || PLATFORM_LUMIN

FMagicLeapCustomPresentVulkan::FMagicLeapCustomPresentVulkan(FMagicLeapHMD* plugin)
: FMagicLeapCustomPresent(plugin)
, RenderTargetTexture(VK_NULL_HANDLE)
, RenderTargetTextureAllocation(VK_NULL_HANDLE)
, RenderTargetTextureAllocationOffset(0)
, RenderTargetTextureSRGB(VK_NULL_HANDLE)
, LastAliasedRenderTarget(VK_NULL_HANDLE)
{}

void FMagicLeapCustomPresentVulkan::BeginRendering()
{
#if WITH_MLSDK
	check(IsInRenderingThread());

	ExecuteOnRHIThread([this]()
	{
		// Always use RHITrackingFrame here, which is then copied to the RenderTrackingFrame. 
		FTrackingFrame& RHIframe = Plugin->RHITrackingFrame;
		RHIframe.ProjectionType = MLGraphicsProjectionType_UnsignedZ;
		BeginFrame(RHIframe);
		if (bCustomPresentIsSet)
		{
			Plugin->InitializeRenderFrameFromRHIFrame();
		}
	});
#endif // WITH_MLSDK
}

void FMagicLeapCustomPresentVulkan::FinishRendering()
{
#if WITH_MLSDK
	check(IsInRenderingThread() || IsInRHIThread());

	if (Plugin->IsDeviceInitialized() && Plugin->GetCurrentFrame().bBeginFrameSucceeded)
	{
		// Notify first render here instead of in RenderToMLSurfaces_RenderThread() because the render to MLSurfaces is already finished by now.
		NotifyFirstRender();

		// TODO [Blake] : Hack since we cannot yet specify a handle per view in the view family
		const MLGraphicsFrameInfo& FrameInfo = Plugin->GetCurrentFrame().FrameInfo;
		const uint32 vp_width = static_cast<uint32>(FrameInfo.viewport.w);
		const uint32 vp_height = static_cast<uint32>(FrameInfo.viewport.h);

#if PLATFORM_LUMIN
		// ZI doesn't make use of depth
		// Clear MLGraphics depth textures since on Vulkan they are not pre-cleared. (needs to be done on RHI thread)
		FMagicLeapHelperVulkan::ClearImage(FrameInfo.depth_id, FLinearColor::Black, 0 /* BaseMipLevel */, 1 /* LevelCount */, 0 /* BaseArrayLayer */, FrameInfo.num_virtual_cameras, true);
#endif // PLATFORM_LUMIN

		// Alias the render target with an srgb image description for proper color space output.
		if (RenderTargetTextureAllocation != VK_NULL_HANDLE && LastAliasedRenderTarget != RenderTargetTexture)
		{
			// SDKUNREAL-1135: ML remote image is corrupted on AMD hardware
			if (!IsRHIDeviceAMD())
			{
				FMagicLeapHelperVulkan::DestroyImageSRGB(RenderTargetTextureSRGB);
				RenderTargetTextureSRGB = VK_NULL_HANDLE;
				RenderTargetTextureSRGB = reinterpret_cast<void *>(FMagicLeapHelperVulkan::AliasImageSRGB((uint64)RenderTargetTextureAllocation, RenderTargetTextureAllocationOffset, vp_width * 2, vp_height));
			}
			LastAliasedRenderTarget = RenderTargetTexture;
			UE_LOG(LogMagicLeap, Log, TEXT("Aliased render target for correct sRGB ouput."));
		}

		const VkImage FinalTarget = (RenderTargetTextureSRGB != VK_NULL_HANDLE) ? reinterpret_cast<const VkImage>(RenderTargetTextureSRGB) : reinterpret_cast<const VkImage>(RenderTargetTexture);
		FMagicLeapHelperVulkan::BlitImage((uint64)FinalTarget, 0, 0, 0, vp_width, vp_height, 1, (uint64)FrameInfo.color_id, 0, 0, 0, 0, vp_width, vp_height, 1);
		FMagicLeapHelperVulkan::BlitImage((uint64)FinalTarget, vp_width, 0, 0, vp_width, vp_height, 1, (uint64)FrameInfo.color_id, 1, 0, 0, 0, vp_width, vp_height, 1);

#if PLATFORM_LUMIN
		// ZI doesn't make use of depth
		if (Plugin->DepthBuffer != nullptr)
		{
			const VkImage DepthTarget = static_cast<FVulkanTexture2D*>(Plugin->DepthBuffer->GetTexture2D())->Surface.Image;

			FMagicLeapHelperVulkan::BlitImage((uint64)DepthTarget, 0, 0, 0, vp_width, vp_height, 1, (uint64)FrameInfo.depth_id, 0, 0, 0, 0, vp_width, vp_height, 1, true);
			FMagicLeapHelperVulkan::BlitImage((uint64)DepthTarget, vp_width, 0, 0, vp_width, vp_height, 1, (uint64)FrameInfo.depth_id, 1, 0, 0, 0, vp_width, vp_height, 1, true);
		}
#endif // PLATFORM_LUMIN

		FMagicLeapHelperVulkan::SignalObjects((uint64)FrameInfo.virtual_cameras[0].sync_object, (uint64)FrameInfo.virtual_cameras[1].sync_object, (uint64)FrameInfo.wait_sync_object);

		MLResult Result = MLGraphicsEndFrame(Plugin->GraphicsClient, Plugin->GetCurrentFrame().FrameInfo.handle);
		if (Result != MLResult_Ok)
		{
#if !WITH_EDITOR
			UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsEndFrame failed with status %d"), Result);
#endif
		}
	}
  
	Plugin->InitializeOldFrameFromRenderFrame();
#endif // WITH_MLSDK
}

void FMagicLeapCustomPresentVulkan::UpdateViewport(const FViewport& Viewport, FRHIViewport* InViewportRHI)
{
	check(IsInGameThread());
	check(InViewportRHI);

	const FTexture2DRHIRef& RT = Viewport.GetRenderTargetTexture();
	check(IsValidRef(RT));

	RenderTargetTexture = RT->GetNativeResource();
	RenderTargetTextureAllocation = reinterpret_cast<void*>(static_cast<FVulkanTexture2D*>(RT->GetTexture2D())->Surface.GetAllocationHandle());
	RenderTargetTextureAllocationOffset = static_cast<FVulkanTexture2D*>(RT->GetTexture2D())->Surface.GetAllocationOffset();

	InViewportRHI->SetCustomPresent(this);

	FMagicLeapCustomPresentVulkan* CustomPresent = this;
	ENQUEUE_RENDER_COMMAND(UpdateViewport_RT)(
		[CustomPresent](FRHICommandList& RHICmdList)
		{
			CustomPresent->UpdateViewport_RenderThread();
		}
	);
}

void FMagicLeapCustomPresentVulkan::UpdateViewport_RenderThread()
{
	check(IsInRenderingThread());

	ExecuteOnRHIThread_DoNotWait([this]()
	{
		bCustomPresentIsSet = true;
	});
}

void FMagicLeapCustomPresentVulkan::Reset()
{
	FMagicLeapCustomPresent::Reset();
	if (IsInRenderingThread() && DestTextureRef.IsValid())
	{
		if (DestTextureRef.IsValid())
		{
			DestTextureRef.SafeRelease();
		}
	}

#if WITH_MLSDK
	FMagicLeapHelperVulkan::DestroyImageSRGB(RenderTargetTextureSRGB);
	RenderTargetTextureSRGB = VK_NULL_HANDLE;
#endif
}

#endif // PLATFORM_WINDOWS || PLATFORM_LUMIN
