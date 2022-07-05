// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingMediaCapture.h"
#include "PixelStreamingInputFrameRHI.h"
#include "PixelStreamingVideoInput.h"
#include "Slate/SceneViewport.h"

void UPixelStreamingMediaCapture::OnRHIResourceCaptured_RenderingThread(
	const FCaptureBaseData& InBaseData,
	TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
	FTextureRHIRef InTexture)
{
	if (VideoInput)
	{
		VideoInput->OnFrame(FPixelStreamingInputFrameRHI(InTexture));
	}
}

bool UPixelStreamingMediaCapture::CaptureSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	Viewport = InSceneViewport;
	SetupVideoInput();
	SetState(EMediaCaptureState::Capturing);
	return true;
}

bool UPixelStreamingMediaCapture::CaptureRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget)
{
	Viewport = nullptr;
	SetupVideoInput();
	SetState(EMediaCaptureState::Capturing);
	return true;
}

void UPixelStreamingMediaCapture::SetupVideoInput()
{
	if (!VideoInput)
	{
		VideoInput = UE::PixelStreaming::FPixelStreamingVideoInput::Create();
	}
}
