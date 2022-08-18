// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaCapture.h"
#include "IPixelStreamingVideoInput.h"
#include "PixelStreamingMediaCapture.generated.h"

UCLASS(BlueprintType)
class UPixelStreamingMediaCapture : public UMediaCapture
{
	GENERATED_BODY()

	//~ Begin UMediaCapture interface
public:
	virtual void OnRHIResourceCaptured_RenderingThread(
		const FCaptureBaseData& InBaseData,
		TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
		FTextureRHIRef InTexture) override;

	virtual bool InitializeCapture() override;
	virtual bool PostInitializeCaptureViewport(TSharedPtr<FSceneViewport>& InSceneViewport) override;
	virtual bool ShouldCaptureRHIResource() const { return true; }
	virtual void StopCaptureImpl(bool bAllowPendingFrameToBeProcess) override {}
	//~ End UMediaCapture interface

	TSharedPtr<IPixelStreamingVideoInput> GetVideoInput() const { return VideoInput; }
	TSharedPtr<FSceneViewport> GetViewport() const { return Viewport.Pin(); }

	DECLARE_MULTICAST_DELEGATE(FOnCaptureViewportInitialized);
	FOnCaptureViewportInitialized OnCaptureViewportInitialized;
private:
	void SetupVideoInput();

private:
	TSharedPtr<IPixelStreamingVideoInput> VideoInput;
	TWeakPtr<FSceneViewport> Viewport;
};
