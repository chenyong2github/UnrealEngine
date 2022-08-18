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

bool UPixelStreamingMediaCapture::InitializeCapture()
{
	SetupVideoInput();
	SetState(EMediaCaptureState::Capturing);
	return true;
}

bool UPixelStreamingMediaCapture::PostInitializeCaptureViewport(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	Viewport = InSceneViewport;
	OnCaptureViewportInitialized.Broadcast();
	return true;
}

void UPixelStreamingMediaCapture::SetupVideoInput()
{
	if (!VideoInput)
	{
		VideoInput = UE::PixelStreaming::FPixelStreamingVideoInput::Create();
	}
}
