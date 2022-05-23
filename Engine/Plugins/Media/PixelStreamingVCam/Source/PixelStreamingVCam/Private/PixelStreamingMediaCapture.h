// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaCapture.h"
#include "PixelStreamingVideoInput.h"
#include "PixelStreamingMediaCapture.generated.h"

UCLASS(BlueprintType)
class UPixelStreamingMediaCapture : public UMediaCapture
{
	GENERATED_BODY()

	//~ Begin UMediaCapture interface
public:
	virtual void OnCustomCapture_RenderingThread(
		FRHICommandListImmediate& RHICmdList,
		const FCaptureBaseData& InBaseData,
		TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
		FTexture2DRHIRef InSourceTexture,
		FTextureRHIRef TargetableTexture,
		FResolveParams& ResolveParams,
		FVector2D CropU,
		FVector2D CropV) override;
	virtual bool CaptureSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport) override;
	virtual bool CaptureRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget) override;
	virtual bool ShouldCaptureRHITexture() const { return true; }
	virtual void StopCaptureImpl(bool bAllowPendingFrameToBeProcess) override {}
	//~ End UMediaCapture interface

	TSharedPtr<FPixelStreamingVideoInput> GetVideoInput() const { return VideoInput; }
	TSharedPtr<FSceneViewport> GetViewport() const { return Viewport.Pin(); }

private:
	void SetupVideoInput();

private:
	TSharedPtr<FPixelStreamingVideoInput> VideoInput;
	TWeakPtr<FSceneViewport> Viewport;
};
