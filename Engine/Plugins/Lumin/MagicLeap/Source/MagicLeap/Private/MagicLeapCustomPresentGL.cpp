// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapCustomPresentGL.h"
#include "MagicLeapHMD.h"
#include "RenderingThread.h"
#include "Lumin/CAPIShims/LuminAPI.h"

#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
#include "OpenGLDrvPrivate.h"
#include "MagicLeapHelperOpenGL.h"
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN

#include "MagicLeapGraphics.h"
#include "Containers/Union.h"

#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN

FMagicLeapCustomPresentOpenGL::FMagicLeapCustomPresentOpenGL(FMagicLeapHMD* plugin)
: FMagicLeapCustomPresent(plugin)
, RenderTargetTexture(0)
, bFramebuffersValid(false)
{}

void FMagicLeapCustomPresentOpenGL::BeginRendering()
{
#if WITH_MLSDK
	check(IsInRenderingThread());

	FTrackingFrame& frame = Plugin->GetCurrentFrameMutable();
	BeginFrame(frame);
#endif //WITH_MLSDK
}

void FMagicLeapCustomPresentOpenGL::FinishRendering()
{
#if WITH_MLSDK
	check(IsInRenderingThread());

	if (Plugin->IsDeviceInitialized() && Plugin->GetCurrentFrame().bBeginFrameSucceeded)
	{
		NotifyFirstRender();

		// TODO [Blake] : Hack since we cannot yet specify a handle per view in the view family
		const MLGraphicsVirtualCameraInfoArray& vp_array = Plugin->GetCurrentFrame().FrameInfo.virtual_camera_info_array;
		const uint32 vp_width = static_cast<uint32>(vp_array.viewport.w);
		const uint32 vp_height = static_cast<uint32>(vp_array.viewport.h);

		if (!bFramebuffersValid)
		{
			glGenFramebuffers(2, Framebuffers);
			bFramebuffersValid = true;
		}

		GLint CurrentFB = 0;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &CurrentFB);

		GLint FramebufferSRGB = 0;
		glGetIntegerv(GL_FRAMEBUFFER_SRGB, &FramebufferSRGB);
		if (FramebufferSRGB)
		{
			// Unreal render target texture is marked as linear but it actually contains pixels in sRGB color space.
			// Rendering from a linear color space texture to an sRGB texture, when GL_FRAMEBUFFER_SRGB is enabled, causes implicit color conversion by the graphics library.
			// This causes a double gamma correction on the pixels and the final eye texture rendered on the device / simulator appears very bright and washed out.
			// We cannot reslove this by requesting linear color space eye textures from ml_graphics as the implicit conversion happens further down the compositor pipeline.
			// The issue is fixed by disabling GL_FRAMEBUFFER_SRGB for the texture blit operation.
			glDisable(GL_FRAMEBUFFER_SRGB);
		}

		//check(vp_array.num_virtual_cameras >= 2); // We assume at least one virtual camera per eye

		const FIntPoint& IdealRenderTargetSize = Plugin->GetHMDDevice()->GetIdealRenderTargetSize();
		const int32 SizeX = FMath::CeilToInt(IdealRenderTargetSize.X * Plugin->GetCurrentFrame().PixelDensity);
		const int32 SizeY = FMath::CeilToInt(IdealRenderTargetSize.Y * Plugin->GetCurrentFrame().PixelDensity);

		// this texture contains both eye renders
		glBindFramebuffer(GL_FRAMEBUFFER, Framebuffers[0]);
		FOpenGL::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, RenderTargetTexture, 0);

		glBindFramebuffer(GL_FRAMEBUFFER, Framebuffers[1]);
		FOpenGL::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, vp_array.color_id, 0, 0);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, Framebuffers[0]);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, Framebuffers[1]);

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const bool bShouldFlipVertically = !IsES2Platform(GMaxRHIShaderPlatform);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		bShouldFlipVertically ?
			FOpenGL::BlitFramebuffer(0, 0, SizeX / 2, SizeY, 0, vp_height, vp_width, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST) :
			FOpenGL::BlitFramebuffer(0, 0, SizeX / 2, SizeY, 0, 0, vp_width, vp_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		MLResult Result = MLGraphicsSignalSyncObjectGL(Plugin->GraphicsClient, vp_array.virtual_cameras[0].sync_object);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsSignalSyncObjectGL for eye 0 failed with status %d"), Result);
		}

		FOpenGL::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, vp_array.color_id, 0, 1);

		bShouldFlipVertically ?
			FOpenGL::BlitFramebuffer(SizeX / 2, 0, SizeX, SizeY, 0, vp_height, vp_width, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST) :
			FOpenGL::BlitFramebuffer(SizeX / 2, 0, SizeX, SizeY, 0, 0, vp_width, vp_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		Result = MLGraphicsSignalSyncObjectGL(Plugin->GraphicsClient, vp_array.virtual_cameras[1].sync_object);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsSignalSyncObjectGL for eye 1 failed with status %d"), Result);
		}

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, CurrentFB);
		if (FramebufferSRGB)
		{
			glEnable(GL_FRAMEBUFFER_SRGB);
		}

		static_assert(UE_ARRAY_COUNT(vp_array.virtual_cameras) == 2, "The MLSDK has updated the size of the virtual_cameras array.");
#if 0 // Enable this in case the MLSDK increases the size of the virtual_cameras array past 2
		for (uint32 i = 2; i < vp_array.num_virtual_cameras; ++i)
		{
			Result = MLGraphicsSignalSyncObjectGL(Plugin->GraphicsClient, vp_array.virtual_cameras[i].sync_object);
			if (Result != MLResult_Ok)
			{
				UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsSignalSyncObjectGL for eye %d failed with status %d"), i, Result);
			}
		}
#endif

		Result = MLGraphicsEndFrame(Plugin->GraphicsClient, Plugin->GetCurrentFrame().FrameInfo.handle);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsEndFrame failed with status %d"), Result);
		}
	}
	Plugin->InitializeOldFrameFromRenderFrame();
#endif //WITH_MLSDK
}

void FMagicLeapCustomPresentOpenGL::Reset()
{
	if (IsInGameThread())
	{
		// Wait for all resources to be released
		FlushRenderingCommands();
	}
	else if (IsInRenderingThread() && bFramebuffersValid)
	{
		glDeleteFramebuffers(2, Framebuffers);
		bFramebuffersValid = false;
	}
}

void FMagicLeapCustomPresentOpenGL::UpdateViewport(const FViewport& Viewport, FRHIViewport* InViewportRHI)
{
	check(IsInGameThread());
	check(InViewportRHI);

	const FTexture2DRHIRef& RT = Viewport.GetRenderTargetTexture();
	check(IsValidRef(RT));

	RenderTargetTexture = *(reinterpret_cast<uint32_t*>(RT->GetNativeResource()));
	InViewportRHI->SetCustomPresent(this);
	
	FMagicLeapCustomPresentOpenGL* CustomPresent = this;
	ENQUEUE_RENDER_COMMAND(UpdateViewport_RT)(
		[CustomPresent](FRHICommandList& RHICmdList)
		{
			CustomPresent->UpdateViewport_RenderThread();
		}
	);
}

void FMagicLeapCustomPresentOpenGL::UpdateViewport_RenderThread()
{
	check(IsInRenderingThread());
	bCustomPresentIsSet = true;
}

#endif  // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
