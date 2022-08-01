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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	FString IP = TEXT("ws://127.0.0.1");

	// Network port number - change this only if connecting multiple RemoteSession devices to the same PC
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	int32 PortNumber = 8888;
	
	// Http webserver port number - e.g. Go to http://localhost:YourPort to access the streamed VCam output. Warning Ports below 1024 require sudo under Linux so it is recommended to use a higher port on that platform.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	int32 HttpPort = 80;
	
	// If using the output from a Composure Output Provider, specify it here
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	int32 FromComposureOutputProviderIndex = INDEX_NONE;

	// Check this if you wish to control the corresponding CineCamera with transform data received from the LiveLink app
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	bool EnableARKitTracking = true;

	// Check this if you are not separately running a signalling server for this session
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	bool StartSignallingServer = true;

	// If not selected, when the editor is not the foreground application, input through the vcam session may seem sluggish or unresponsive.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	bool PreventEditorIdle = true;

protected:
	UPROPERTY(Transient)
	UPixelStreamingMediaOutput* MediaOutput = nullptr;

	UPROPERTY(Transient)
	UPixelStreamingMediaCapture* MediaCapture = nullptr;

private:
	void StopSignallingServer();
	void OnARKitTransformReceived(FPixelStreamingPlayerId PlayerId, uint8 Type, TArray<uint8> Data);

private:
	FHitResult LastViewportTouchResult;
	bool bUsingDummyUMG = false;
	bool bOldThrottleCPUWhenNotForeground;
};
