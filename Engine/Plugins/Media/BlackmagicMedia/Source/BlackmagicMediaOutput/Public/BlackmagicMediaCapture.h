// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaCapture.h"
#include "HAL/CriticalSection.h"
#include "MediaIOCoreEncodeTime.h"
#include "Misc/FrameRate.h"
#include "BlackmagicMediaOutput.h"
#include "BlackmagicMediaCapture.generated.h"

class FEvent;


namespace BlackmagicMediaCaptureHelpers
{
	class FBlackmagicMediaCaptureEventCallback;
}


/**
 * Output Media for Blackmagic streams.
 * The output format could be any of EBlackmagicMediaOutputPixelFormat.
 */
UCLASS(BlueprintType)
class BLACKMAGICMEDIAOUTPUT_API UBlackmagicMediaCapture : public UMediaCapture
{
	GENERATED_UCLASS_BODY()

	//~ UMediaCapture interface
public:
	virtual bool HasFinishedProcessing() const override;
protected:
	virtual bool ValidateMediaOutput() const override;
	virtual bool CaptureSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport) override;
	virtual bool CaptureRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget) override;
	virtual bool UpdateSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport) override;
	virtual bool UpdateRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget) override;
	virtual void StopCaptureImpl(bool bAllowPendingFrameToBeProcess) override;

	virtual void OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, void* InBuffer, int32 Width, int32 Height) override;

private:
	struct FBlackmagicOutputCallback;
	friend FBlackmagicOutputCallback;

private:
	bool InitBlackmagic(UBlackmagicMediaOutput* InMediaOutput);
	void WaitForSync_RenderingThread();
	void ApplyViewportTextureAlpha(TSharedPtr<FSceneViewport> InSceneViewport);
	void RestoreViewportTextureAlpha(TSharedPtr<FSceneViewport> InSceneViewport);

private:
	friend BlackmagicMediaCaptureHelpers::FBlackmagicMediaCaptureEventCallback;
	BlackmagicMediaCaptureHelpers::FBlackmagicMediaCaptureEventCallback* EventCallback;

	/** Option from MediaOutput */
	bool bWaitForSyncEvent;
	bool bEncodeTimecodeInTexel;
	bool bLogDropFrame;
	
	/** MediaOutput cached value */
	EBlackmagicMediaOutputPixelFormat BlackmagicMediaOutputPixelFormat;

	/** Saved IgnoreTextureAlpha flag from viewport */
	bool bSavedIgnoreTextureAlpha;
	bool bIgnoreTextureAlphaChanged;

	/** Selected FrameRate of this output */
	FFrameRate FrameRate;

	/** Critical section for synchronizing access to the OutputChannel */
	FCriticalSection RenderThreadCriticalSection;

	/** Event to wakeup When waiting for sync */
	FEvent* WakeUpEvent;

	/** Last frame drop count to detect count */
	uint64 LastFrameDropCount_BlackmagicThread;
};
