// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapCustomPresentMetal.h"
#include "MagicLeapHMD.h"
#include "RenderingThread.h"
#include "Lumin/CAPIShims/LuminAPIRemote.h"
#include "XRThreadUtils.h"

#include "Containers/Union.h"

#if PLATFORM_MAC

#include "MetalRHI.h"
#include "MetalRHIPrivate.h"
#include "MetalContext.h"
#include "MetalState.h"
#include "MetalResources.h"

FMagicLeapCustomPresentMetal::FMagicLeapCustomPresentMetal(FMagicLeapHMD* plugin)
: FMagicLeapCustomPresent(plugin)
, DestTextureRef(nullptr)
, SrcTextureSRGBRef(nullptr)
{}

void FMagicLeapCustomPresentMetal::BeginRendering()
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

void FMagicLeapCustomPresentMetal::FinishRendering()
{
#if WITH_MLSDK
	check(IsInRenderingThread() || IsInRHIThread());

	if (Plugin->IsDeviceInitialized() && Plugin->GetCurrentFrame().bBeginFrameSucceeded)
	{
		// Actual rendering on the graphics textures happens in FMagicLeapCustomPresent::RenderToMLSurfaces_RenderThread() called via FMagicLeapHMD::RenderTexture_RenderThread()
		// FMagicLeapCustomPresent::FinishRendering() can be called on the RHI thread as well and 
		// we should setup the render pass on the render thread only.

		// Notify first render here instead of in RenderToMLSurfaces_RenderThread() because the render to MLSurfaces is already finished by now.
		NotifyFirstRender();

		const MLGraphicsFrameInfo& FrameInfo = Plugin->GetCurrentFrame().FrameInfo;

		FMetalDynamicRHI* MetalDynamicRHI = static_cast<FMetalDynamicRHI*>(GDynamicRHI);
		FMetalRHIImmediateCommandContext* ImmediateCommandContext = static_cast<FMetalRHIImmediateCommandContext*>(MetalDynamicRHI->RHIGetDefaultContext());
		FMetalContext& Context = ImmediateCommandContext->GetInternalContext();
		mtlpp::CommandBuffer& CommandBuffer = Context.GetCurrentCommandBuffer();

		// Signal the sync objects only after the command buffer is completed.
		// TODO : Investigate why this is needed for metal but works fine for vulkan.
		// MagicLeapCustomPresent::Present() is supposed to be called only after all GPU work
		// on the engine side is complete for the render targets.
		CommandBuffer.AddCompletedHandler([this, FrameInfo](mtlpp::CommandBuffer const& InBuffer){
			MLResult Result;
			for (uint32 i = 0; i < FrameInfo.num_virtual_cameras; ++i)
			{
				Result = MLRemoteGraphicsSignalSyncObjectMTL(Plugin->GraphicsClient, FrameInfo.virtual_cameras[i].sync_object);
				UE_CLOG(Result != MLResult_Ok, LogMagicLeap, Error, TEXT("MLRemoteGraphicsSignalSyncObjectMTL(%d) failed with status %s"), i, UTF8_TO_TCHAR(MLGetResultString(Result)));
			}
		});

		MLResult Result = MLGraphicsEndFrame(Plugin->GraphicsClient, FrameInfo.handle);
		if (Result != MLResult_Ok)
		{
#if !WITH_EDITOR
			UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsEndFrame failed with status %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif
		}
	}
  
	Plugin->InitializeOldFrameFromRenderFrame();
#endif // WITH_MLSDK
}

void FMagicLeapCustomPresentMetal::UpdateViewport(const FViewport& Viewport, FRHIViewport* InViewportRHI)
{
	check(IsInGameThread());

	InViewportRHI->SetCustomPresent(this);

	FMagicLeapCustomPresentMetal* CustomPresent = this;
	ENQUEUE_RENDER_COMMAND(UpdateViewport_RT)(
		[CustomPresent](FRHICommandList& RHICmdList)
		{
			CustomPresent->UpdateViewport_RenderThread();
		}
	);
}

void FMagicLeapCustomPresentMetal::UpdateViewport_RenderThread()
{
	check(IsInRenderingThread());

	ExecuteOnRHIThread_DoNotWait([this]()
	{
		bCustomPresentIsSet = true;
	});
}

void FMagicLeapCustomPresentMetal::Reset()
{
	FMagicLeapCustomPresent::Reset();
	if (IsInRenderingThread() && DestTextureRef.IsValid())
	{
		if (DestTextureRef.IsValid())
		{
			DestTextureRef.SafeRelease();
		}
		if (SrcTextureSRGBRef.IsValid())
		{
			SrcTextureSRGBRef.SafeRelease();
		}
	}
}

void FMagicLeapCustomPresentMetal::RenderToMLSurfaces_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture)
{
#if WITH_MLSDK
	if (Plugin->IsDeviceInitialized() && Plugin->GetCurrentFrame().bBeginFrameSucceeded)
	{
		const MLGraphicsFrameInfo& FrameInfo = Plugin->GetCurrentFrame().FrameInfo;

		// Manage MLGraphics texture as an FMetalTexture to use it with Unreal's render pipeline.
		// Use AutoRelease for ownership so that color_id is not owned by the FMetalTexture object, lifetime is externally managed.
		FMetalTexture DestMTLTexture((id<MTLTexture>)FrameInfo.color_id, ns::Ownership::AutoRelease);
		if (!DestTextureRef.IsValid())
		{
			const uint32 vp_width = static_cast<uint32>(FrameInfo.viewport.w);
			const uint32 vp_height = static_cast<uint32>(FrameInfo.viewport.h);
			// TODO: @njain add an overload to FMetalSurface to handle externally owned textures, similar to FVulkanSurface.
			DestTextureRef = new FMetalTexture2DArray(PF_R8G8B8A8, vp_width, vp_height, FrameInfo.num_virtual_cameras, 1, TexCreate_RenderTargetable, nullptr, FClearValueBinding::Transparent);
		}

		// Set MLGraphics texture into Unreal's FMetalTexture2DArray.
		static_cast<FMetalTexture2DArray*>(DestTextureRef.GetReference())->Surface.Texture = DestMTLTexture;

		FMetalTexture2D* SrcMetalTexture = static_cast<FMetalTexture2D*>(SrcTexture);
		if (!SrcTextureSRGBRef.IsValid())
		{
			SrcTextureSRGBRef = new FMetalTexture2D(SrcMetalTexture->GetFormat(), SrcMetalTexture->GetSizeX(), SrcMetalTexture->GetSizeY(), SrcMetalTexture->GetNumMips(), SrcMetalTexture->GetNumSamples(), SrcMetalTexture->GetFlags(), nullptr, FClearValueBinding::Transparent);
		}

		// Unreal render target texture is marked as linear but it actually contains pixels in sRGB color space.
		// Rendering from a linear color space texture to an sRGB texture causes implicit color conversion by the graphics library.
		// This causes a double gamma correction on the pixels and the final eye texture rendered on the device / simulator appears very bright and washed out.
		// We cannot reslove this by requesting linear color space eye textures from ml_graphics as the implicit conversion happens further down the compositor pipeline.
		// The issue is fixed by creating a new texture marked as sRGB but which uses the same memory as the original Unreal render target texture.
		// We use this new sRGB texture to render to ml_graphics' eye textures.
		static_cast<FMetalTexture2D*>(SrcTextureSRGBRef.GetReference())->Surface.Texture = FMetalTexture(SrcMetalTexture->Surface.Texture.NewTextureView(static_cast<mtlpp::PixelFormat>(MLRemoteGraphicsMTLFormatFromMLSurfaceFormat(MLSurfaceFormat_RGBA8UNormSRGB))));

		// Left Eye (flip vertically) = U1 = 0, V1 = 1, U2 = (0 + 0.5), V2 = (1 - 1)
		RenderToTextureSlice_RenderThread(RHICmdList, SrcTextureSRGBRef.GetReference(), DestTextureRef.GetReference(), 0, FVector4(0, 1.0f, 0.5f, -1.0f));
		// Right Eye (flip vertically) = U1 = 0.5, V1 = 1, U2 = (0.5 + 0.5), V2 = (1 - 1)
		RenderToTextureSlice_RenderThread(RHICmdList, SrcTextureSRGBRef.GetReference(), DestTextureRef.GetReference(), 1, FVector4(0.5f, 1.0f, 0.5f, -1.0f));
	}
#endif // WITH_MLSDK
}

#endif // PLATFORM_MAC
