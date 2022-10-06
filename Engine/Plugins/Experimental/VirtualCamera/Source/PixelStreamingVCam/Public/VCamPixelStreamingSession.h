// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VCamOutputProviderBase.h"
#include "PixelStreamingMediaCapture.h"
#include "PixelStreamingMediaOutput.h"
#include "PixelStreamingServers.h"
#include "Slate/SceneViewport.h"
#include "Engine/TextureRenderTarget2D.h"
#if WITH_EDITOR
	#include "LevelEditorViewport.h"
#endif
#include "VCamPixelStreamingSession.generated.h"

UCLASS(meta = (DisplayName = "Pixel Streaming Provider"))
class PIXELSTREAMINGVCAM_API UVCamPixelStreamingSession : public UVCamOutputProviderBase
{
	GENERATED_BODY()
public:
	virtual void Initialize() override;
	virtual void Deinitialize() override;
	virtual void Activate() override;
	virtual void Deactivate() override;
	virtual void Tick(const float DeltaTime) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif	
	// If using the output from a Composure Output Provider, specify it here
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	int32 FromComposureOutputProviderIndex = INDEX_NONE;

	// Check this if you wish to control the corresponding CineCamera with transform data received from the LiveLink app
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	bool EnableARKitTracking = true;

	// If not selected, when the editor is not the foreground application, input through the vcam session may seem sluggish or unresponsive.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	bool PreventEditorIdle = true;

	// If true then the Live Link Subject of the owning VCam Component will be set to the subject created by this Output Provider when the Provider is enabled
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	bool bAutoSetLiveLinkSubject = true;
	
protected:
	UPROPERTY(Transient)
	TObjectPtr<UPixelStreamingMediaOutput> MediaOutput = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPixelStreamingMediaCapture> MediaCapture = nullptr;

private:
	void SetupSignallingServer();
	void StopSignallingServer();
	void SetupCapture();
	void StartCapture();
	void SetupCustomInputHandling();
	void OnCaptureStateChanged();
	void OnARKitTransformReceived(FPixelStreamingPlayerId PlayerId, uint8 Type, TArray<uint8> Data);

private:
	FHitResult LastViewportTouchResult;
	bool bUsingDummyUMG = false;
	bool bOldThrottleCPUWhenNotForeground;
};
