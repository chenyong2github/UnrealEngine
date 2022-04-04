// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureSourceBackbuffer.h"
#include "Settings.h"
#include "Utils.h"

#include "RHI.h"
#include "RHIResources.h"

namespace
{
	class FBackbufferFrameCapturer : public IPixelStreamingFrameCapturer
	{
	public:
		FGPUFenceRHIRef Fence;

	public:
		virtual void CaptureTexture(FPixelStreamingTextureWrapper& TextureToCopy, TSharedPtr<FPixelStreamingTextureWrapper> DestinationTexture) override;
		virtual bool IsCaptureFinished() override;
		virtual void OnCaptureFinished(TSharedPtr<FPixelStreamingTextureWrapper> CapturedTexture) override;
	};

	void FBackbufferFrameCapturer::CaptureTexture(FPixelStreamingTextureWrapper& TextureToCopy, TSharedPtr<FPixelStreamingTextureWrapper> DestinationTexture)
	{
		UE::PixelStreaming::CopyTextureToRHI(TextureToCopy.GetTexture<FTextureRHIRef>(), DestinationTexture->GetTexture<FTextureRHIRef>(), Fence);
	}

	bool FBackbufferFrameCapturer::IsCaptureFinished()
	{
		return Fence->Poll();
	}

	void FBackbufferFrameCapturer::OnCaptureFinished(TSharedPtr<FPixelStreamingTextureWrapper> CapturedTexture)
	{
		Fence->Clear();
	}
} // namespace

namespace UE::PixelStreaming
{

	/* ----------------- FBackbufferReadyDelegateRouter ----------------- */

	FBackbufferReadyDelegateRouter::FBackbufferReadyDelegateRouter(FTextureSourceBackbuffer* InParent)
		: Parent(InParent)
	{
	}

	void FBackbufferReadyDelegateRouter::RouteOnBackbufferReadyCall(SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer)
	{
		FPixelStreamingTextureWrapper NewFrame = FPixelStreamingTextureWrapper(FrameBuffer);
		Parent->OnNewTexture.Broadcast(NewFrame, FrameBuffer->GetDesc().Extent.X, FrameBuffer->GetDesc().Extent.Y);
	}

	/* ----------------- FTextureSourceBackbuffer ----------------- */

	FTextureSourceBackbuffer::FTextureSourceBackbuffer()
		// Store this because accessing slate application is difficult during destructor
		: BackbufferReadyDelegate(FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent())
		, DelegateRouter(MakeShared<FBackbufferReadyDelegateRouter>(this))
	{
		BackbufferReadyHandle = BackbufferReadyDelegate.AddSP(DelegateRouter, &FBackbufferReadyDelegateRouter::RouteOnBackbufferReadyCall);
	}

	FTextureSourceBackbuffer::~FTextureSourceBackbuffer()
	{
		BackbufferReadyDelegate.Remove(BackbufferReadyHandle);
	}

	TSharedPtr<FPixelStreamingTextureWrapper> FTextureSourceBackbuffer::CreateBlankStagingTexture(uint32 Width, uint32 Height)
	{
		return MakeShared<FPixelStreamingTextureWrapper>(UE::PixelStreaming::CreateRHITexture(Width, Height));
	}

	TSharedPtr<IPixelStreamingFrameCapturer> FTextureSourceBackbuffer::CreateFrameCapturer()
	{
		TSharedPtr<FBackbufferFrameCapturer> Capturer = MakeShared<FBackbufferFrameCapturer>();
		Capturer->Fence = GDynamicRHI->RHICreateGPUFence(TEXT("VideoCapturerCopyFence"));
		return Capturer;
	}

	rtc::scoped_refptr<webrtc::I420Buffer> FTextureSourceBackbuffer::ToWebRTCI420Buffer(TSharedPtr<FPixelStreamingTextureWrapper> Texture)
	{
		verifyf(false, TEXT("The I420 conversion should not be required for the GPU code path. If this is getting hit something is wrong."));
		return nullptr;
	}
} // namespace UE::PixelStreaming
