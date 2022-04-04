// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingModule.h"
#include "RHI.h"
#include "Tickable.h"
#include "InputDevice.h"
#include "Poller.h"
#include "FixedFPSPump.h"
#include "PixelStreamingPumpable.h"
#include "TextureSourceFactory.h"

class AController;
class AGameModeBase;
class APlayerController;
class FSceneViewport;
class UPixelStreamingInput;
class SWindow;

namespace UE::PixelStreaming
{
	class FStreamer;

	/*
	 * This plugin allows the back buffer to be sent as a compressed video across a network.
	 */
	class FPixelStreamingModule : public IPixelStreamingModule, public FTickableGameObject
	{
	public:
		static IPixelStreamingModule* GetModule();

		virtual bool StartStreaming(const FString& SignallingServerUrl) override;
		virtual void StopStreaming() override;

	private:
		/** IModuleInterface implementation */
		void StartupModule() override;
		void ShutdownModule() override;

		TSharedPtr<class IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;

		/** IPixelStreamingModule implementation */
		FReadyEvent& OnReady() override;
		FStreamingStartedEvent& OnStreamingStarted() override;
		FStreamingStoppedEvent& OnStreamingStopped() override;
		bool IsReady() override;
		IInputDevice& GetInputDevice() override;
		void AddPlayerConfig(TSharedRef<FJsonObject>& JsonObject) override;
		void SendResponse(const FString& Descriptor) override;
		void SendCommand(const FString& Descriptor) override;

		/*
		 * Returns a shared pointer to the device which handles pixel streaming
		 * input.
		 * @return The shared pointer to the input device.
		 */
		TSharedPtr<FInputDevice> GetInputDevicePtr();
		void AddInputComponent(UPixelStreamingInput* InInputComponent) override;
		void RemoveInputComponent(UPixelStreamingInput* InInputComponent) override;
		const TArray<UPixelStreamingInput*> GetInputComponents() override;

		void FreezeFrame(UTexture2D* Texture) override;
		void UnfreezeFrame() override;
		void KickPlayer(FPixelStreamingPlayerId PlayerId);
		IPixelStreamingAudioSink* GetPeerAudioSink(FPixelStreamingPlayerId PlayerId) override;
		IPixelStreamingAudioSink* GetUnlistenedAudioSink() override;
		virtual rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> CreateExternalVideoSource(FName SourceType) override;
		virtual webrtc::VideoEncoderFactory* CreateVideoEncoderFactory() override;
		virtual void RegisterPumpable(rtc::scoped_refptr<FPixelStreamingPumpable> Pumpable) override;
		virtual void UnregisterPumpable(rtc::scoped_refptr<FPixelStreamingPumpable> Pumpable) override;
		virtual void AddPollerTask(TFunction<void()> Task, TFunction<bool()> IsTaskFinished, TSharedRef<bool, ESPMode::ThreadSafe> bKeepRunning) override;
		virtual IPixelStreamingTextureSourceFactory& GetTextureSourceFactory() override;
		virtual void SetActiveTextureSourceTypes(const TArray<FName>& SourceTypes) override;
		virtual const TArray<FName>& GetActiveTextureSourceTypes() const override;
		/** End IPixelStreamingModule implementation */

		// FTickableGameObject
		bool IsTickableWhenPaused() const override;
		bool IsTickableInEditor() const override;
		void Tick(float DeltaTime) override;
		TStatId GetStatId() const override;

		bool IsPlatformCompatible() const;
		void UpdateViewport(FSceneViewport* Viewport);
		void OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer);
		void SendJpeg(TArray<FColor> RawData, const FIntRect& Rect);
		void SendFileData(TArray<uint8>& ByteData, FString& MimeType, FString& FileExtension);

		void InitStreamer();

	private:
		FReadyEvent ReadyEvent;
		FStreamingStartedEvent StreamingStartedEvent;
		FStreamingStoppedEvent StreamingStoppedEvent;
		TUniquePtr<FStreamer> Streamer;
		TSharedPtr<FInputDevice> InputDevice;
		TArray<UPixelStreamingInput*> InputComponents;
		bool bFrozen = false;
		bool bCaptureNextBackBufferAndStream = false;
		double LastVideoEncoderQPReportTime = 0;
		static IPixelStreamingModule* PixelStreamingModule;

		TUniquePtr<FFixedFPSPump> FramePump;
		TUniquePtr<FPoller> Poller;
		TUniquePtr<IPixelStreamingTextureSourceFactory> TextureSourceFactory;

		bool bDecoupleFrameRate = false;
	};
} // namespace UE::PixelStreaming
