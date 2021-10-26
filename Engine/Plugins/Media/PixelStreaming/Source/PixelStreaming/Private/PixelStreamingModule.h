// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingModule.h"
#include "RHI.h"
#include "Tickable.h"

class AController;
class AGameModeBase;
class APlayerController;
class FSceneViewport;
class FStreamer;
class UPixelStreamerInputComponent;
class SWindow;

/**
 * This plugin allows the back buffer to be sent as a compressed video across
 * a network.
 */
class FPixelStreamingModule : public IPixelStreamingModule, public FTickableGameObject
{
private:
	/** IModuleInterface implementation */
	void StartupModule() override;
	void ShutdownModule() override;

	TSharedPtr<class IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;

	/** IPixelStreamingModule implementation */
	FInputDevice& GetInputDevice() override;
	void AddPlayerConfig(TSharedRef<FJsonObject>& JsonObject) override;
	void SendResponse(const FString& Descriptor) override;
	void SendCommand(const FString& Descriptor) override;

	/**
	 * Returns a shared pointer to the device which handles pixel streaming
	 * input.
	 * @return The shared pointer to the input device.
	 */
	TSharedPtr<FInputDevice> GetInputDevicePtr();
	void AddInputComponent(UPixelStreamerInputComponent* InInputComponent) override;
	void RemoveInputComponent(UPixelStreamerInputComponent* InInputComponent) override;
	const TArray<UPixelStreamerInputComponent*> GetInputComponents() override;

	void FreezeFrame(UTexture2D* Texture) override;
	void UnfreezeFrame() override;
	IPixelStreamingAudioSink* GetPeerAudioSink(FPlayerId PlayerId) override;
	IPixelStreamingAudioSink* GetUnlistenedAudioSink() override;

	// FTickableGameObject
	bool IsTickableWhenPaused() const override;
	bool IsTickableInEditor() const override;
	void Tick(float DeltaTime) override;
	TStatId GetStatId() const override;

	bool CheckPlatformCompatibility() const;
	void UpdateViewport(FSceneViewport* Viewport);
	void OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer);
	void OnGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
	void OnGameModeLogout(AGameModeBase* GameMode, AController* Exiting);
	void SendJpeg(TArray<FColor> RawData, const FIntRect& Rect);

	void InitStreamer();

private:
	TUniquePtr<FStreamer> Streamer;
	TSharedPtr<FInputDevice> InputDevice;
	TArray<UPixelStreamerInputComponent*> InputComponents;
	bool bFrozen = false;
	bool bCaptureNextBackBufferAndStream = false;
	double LastVideoEncoderQPReportTime = 0;
};
