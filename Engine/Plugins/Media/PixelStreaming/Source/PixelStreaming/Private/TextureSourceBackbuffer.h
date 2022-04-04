// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingTextureSource.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Templates/SharedPointer.h"

namespace UE::PixelStreaming
{
	class FBackbufferReadyDelegateRouter
	{
	public:
		FBackbufferReadyDelegateRouter(class FTextureSourceBackbuffer* InParent);
		void RouteOnBackbufferReadyCall(SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer);

	private:
		class FTextureSourceBackbuffer* Parent = nullptr;
	};

	/*
	* Copies from the UE backbuffer into texture that stay fully resident on the GPU.
	*/
	class FTextureSourceBackbuffer : public FPixelStreamingTextureSource
	{
	private:
		FSlateRenderer::FOnBackBufferReadyToPresent& BackbufferReadyDelegate;
		FDelegateHandle BackbufferReadyHandle;
		TSharedRef<FBackbufferReadyDelegateRouter> DelegateRouter;

	public:
		FTextureSourceBackbuffer();
		~FTextureSourceBackbuffer();
		virtual TSharedPtr<FPixelStreamingTextureWrapper> CreateBlankStagingTexture(uint32 Width, uint32 Height) override;
		virtual TSharedPtr<IPixelStreamingFrameCapturer> CreateFrameCapturer() override;
		virtual rtc::scoped_refptr<webrtc::I420Buffer> ToWebRTCI420Buffer(TSharedPtr<FPixelStreamingTextureWrapper> Texture) override;
	};
} // namespace UE::PixelStreaming
