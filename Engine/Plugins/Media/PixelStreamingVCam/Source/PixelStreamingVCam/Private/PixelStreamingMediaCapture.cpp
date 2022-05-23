// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingMediaCapture.h"
#include "PixelStreamingSourceFrame.h"
#include "Slate/SceneViewport.h"

void UPixelStreamingMediaCapture::OnCustomCapture_RenderingThread(
	FRHICommandListImmediate& RHICmdList,
	const FCaptureBaseData& InBaseData,
	TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
	FTexture2DRHIRef InSourceTexture,
	FTextureRHIRef TargetableTexture,
	FResolveParams& ResolveParams,
	FVector2D CropU,
	FVector2D CropV)
{
	if (VideoInput)
	{
		VideoInput->OnFrame.Broadcast(FPixelStreamingSourceFrame(InSourceTexture));
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
		VideoInput = MakeShared<FPixelStreamingVideoInput>();
	}
}
