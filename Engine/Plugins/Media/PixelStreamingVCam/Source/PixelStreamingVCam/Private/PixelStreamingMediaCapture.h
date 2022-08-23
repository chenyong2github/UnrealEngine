// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaCapture.h"
#include "PixelStreamingVideoInput.h"
#include "Slate/SceneViewport.h"
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

	TSharedPtr<FPixelStreamingVideoInput> GetVideoInput() const { return VideoInput; }
	TSharedPtr<FSceneViewport> GetViewport() const { return SceneViewport.Pin(); }

	DECLARE_MULTICAST_DELEGATE(FOnCaptureViewportInitialized);
	FOnCaptureViewportInitialized OnCaptureViewportInitialized;
private:
	void SetupVideoInput();

private:
	TSharedPtr<FPixelStreamingVideoInput> VideoInput;
	TWeakPtr<FSceneViewport> SceneViewport;
};
