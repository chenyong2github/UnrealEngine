// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingModule.h"
#include "Streamer.h"
#include "PixelStreamingInputChannel.h"
#include "PixelStreamingInputComponent.h"
#include "PixelStreamingDelegates.h"
#include "PixelStreamingSignallingConnection.h"
#include "Settings.h"
#include "PixelStreamingPrivate.h"
#include "AudioSink.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Texture2D.h"
#include "Slate/SceneViewport.h"
#include "Utils.h"
#include "UtilsRender.h"

#if PLATFORM_WINDOWS
	#include "Windows/WindowsHWrapper.h"
#elif PLATFORM_LINUX
	#include "CudaModule.h"
#endif

#if PLATFORM_WINDOWS
	#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
	#include <VersionHelpers.h>
THIRD_PARTY_INCLUDES_END
	#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "RenderingThread.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RendererInterface.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "GameFramework/GameModeBase.h"
#include "Dom/JsonObject.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "VideoEncoderFactory.h"
#include "VideoEncoderFactoryLayered.h"
#include "WebRTCLogging.h"
#include "WebSocketsModule.h"

#if !UE_BUILD_SHIPPING
	#include "DrawDebugHelpers.h"
#endif

#include "PixelStreamingVideoInputBackBuffer.h"
#include "VideoSourceGroup.h"
#include "PixelStreamingPeerConnection.h"
#include "Engine/GameEngine.h"

DEFINE_LOG_CATEGORY(LogPixelStreaming);

IPixelStreamingModule* UE::PixelStreaming::FPixelStreamingModule::PixelStreamingModule = nullptr;

namespace UE::PixelStreaming
{
	/**
	 * IModuleInterface implementation
	 */
	void FPixelStreamingModule::StartupModule()
	{
		// Initialise all settings from command line args etc
		Settings::InitialiseSettings();

		// Pixel Streaming does not make sense without an RHI so we don't run in commandlets without one.
		if (IsRunningCommandlet() && !IsAllowCommandletRendering())
		{
			return;
		}

		if (!FSlateApplication::IsInitialized())
		{
			return;
		}

		const ERHIInterfaceType RHIType = GDynamicRHI ? RHIGetInterfaceType() : ERHIInterfaceType::Hidden;

		StreamerInputChannels = MakeShared<FStreamerInputChannels>(FSlateApplication::Get().GetPlatformApplication()->GetMessageHandler());
		IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);

		// only D3D11/D3D12/Vulkan is supported
		if (RHIType == ERHIInterfaceType::D3D11 || RHIType == ERHIInterfaceType::D3D12 || RHIType == ERHIInterfaceType::Vulkan)
		{
			// By calling InitDefaultStreamer post engine init we can use pixel streaming in standalone editor mode
			FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([this]() {
				// Check to see if we can use the Pixel Streaming plugin on this platform.
				// If not then we avoid setting up our delegates to prevent access to the plugin.
				if (!IsPlatformCompatible())
				{
					return;
				}

				if (!ensure(GEngine != nullptr))
				{
					return;
				}

				FApp::SetUnfocusedVolumeMultiplier(1.0f);

				// Allow Pixel Streaming to broadcast to various delegates bound in the application-specific blueprint.
				UPixelStreamingDelegates::CreateInstance();

				// Ensure we have ImageWrapper loaded, used in Freezeframes
				verify(FModuleManager::Get().LoadModule(FName("ImageWrapper")));

				InitDefaultStreamer();
				bModuleReady = true;
				ReadyEvent.Broadcast(*this);
				// We don't want to start immediately streaming in editor
				if (!GIsEditor)
				{
					StartStreaming();
				}
			});
		}
		else
		{
			UE_LOG(LogPixelStreaming, Warning, TEXT("Only D3D11/D3D12/Vulkan Dynamic RHI is supported. Detected %s"), GDynamicRHI != nullptr ? GDynamicRHI->GetName() : TEXT("[null]"));
		}

		rtc::InitializeSSL();
		RedirectWebRtcLogsToUnreal(rtc::LoggingSeverity::LS_VERBOSE);
		FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("AVEncoder"));
		FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");

		// ExternalVideoSourceGroup is used so that we can have a video source without a streamer
		ExternalVideoSourceGroup = FVideoSourceGroup::Create();
		ExternalVideoSourceGroup->SetVideoInput(FPixelStreamingVideoInputBackBuffer::Create());
		ExternalVideoSourceGroup->Start();

		bStartupCompleted = true;
	}

	void FPixelStreamingModule::ShutdownModule()
	{
		if (!bStartupCompleted)
		{
			return;
		}

		IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);

		// We explicitly call release on streamer so WebRTC gets shutdown before our module is deleted
		Streamers.Empty();
		ExternalVideoSourceGroup->Stop();

		FPixelStreamingPeerConnection::Shutdown();

		rtc::CleanupSSL();

		IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);

		bStartupCompleted = false;
	}

	/**
	 * End IModuleInterface implementation
	 */

	/**
	 * IPixelStreamingModule implementation
	 */
	IPixelStreamingModule* FPixelStreamingModule::GetModule()
	{
		if (PixelStreamingModule)
		{
			return PixelStreamingModule;
		}
		IPixelStreamingModule* Module = FModuleManager::Get().LoadModulePtr<IPixelStreamingModule>("PixelStreaming");
		if (Module)
		{
			PixelStreamingModule = Module;
		}
		return PixelStreamingModule;
	}

	void FPixelStreamingModule::SetCodec(EPixelStreamingCodec Codec)
	{
		Settings::SetCodec(Codec);
	}

	EPixelStreamingCodec FPixelStreamingModule::GetCodec() const
	{
		return Settings::GetSelectedCodec();
	}

	IPixelStreamingModule::FReadyEvent& FPixelStreamingModule::OnReady()
	{
		return ReadyEvent;
	}

	bool FPixelStreamingModule::IsReady()
	{
		return bModuleReady;
	}

	bool FPixelStreamingModule::StartStreaming()
	{
		bool bSuccess = true;
		TMap<FString, TSharedPtr<IPixelStreamingStreamer>>::TIterator Iter = Streamers.CreateIterator();
		for (; Iter; ++Iter)
		{
			TSharedPtr<IPixelStreamingStreamer> Streamer = Iter.Value();

			if (Streamer.IsValid())
			{
				Streamer->SetStreamFPS(Settings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread());
				// default to the scene viewport if we have a game engine. if we are
				// running editor we require the user to set the viewport via FStreamer as above
				if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
				{
					FSceneViewport* TargetViewport = GameEngine->SceneViewport.Get();
					Streamer->SetTargetViewport(TargetViewport);
					Streamer->SetTargetWindow(TargetViewport->FindWindow());
				}
				Streamer->StartStreaming();
				bSuccess &= true;
				continue;
			}
			bSuccess &= false;
		}
		return bSuccess;
	}

	void FPixelStreamingModule::StopStreaming()
	{
		TMap<FString, TSharedPtr<IPixelStreamingStreamer>>::TIterator Iter = Streamers.CreateIterator();
		for (; Iter; ++Iter)
		{
			TSharedPtr<IPixelStreamingStreamer> Streamer = Iter.Value();

			if (Streamer.IsValid())
			{
				Streamer->StopStreaming();
			}
		}
	}

	TSharedPtr<IPixelStreamingStreamer> FPixelStreamingModule::CreateStreamer(const FString& StreamerId)
	{
		TSharedPtr<IPixelStreamingStreamer> ExistingStreamer = GetStreamer(StreamerId);
		if (ExistingStreamer)
		{
			return ExistingStreamer;
		}

		TSharedPtr<FStreamer> NewStreamer = FStreamer::Create(StreamerId);
		{
			FScopeLock Lock(&StreamersCS);
			Streamers.Add(StreamerId, NewStreamer);
		}
		NewStreamer->SetInputChannel(StreamerInputChannels->CreateInputChannel());

		return NewStreamer;
	}

	TArray<FString> FPixelStreamingModule::GetStreamerIds()
	{
		TArray<FString> StreamerKeys;
		FScopeLock Lock(&StreamersCS);
		Streamers.GenerateKeyArray(StreamerKeys);
		return StreamerKeys;
	}

	TSharedPtr<IPixelStreamingStreamer> FPixelStreamingModule::GetStreamer(const FString& StreamerId)
	{
		FScopeLock Lock(&StreamersCS);
		if (Streamers.Contains(StreamerId))
		{
			return Streamers[StreamerId];
		}
		return nullptr;
	}

	TSharedPtr<IPixelStreamingStreamer> FPixelStreamingModule::DeleteStreamer(const FString& StreamerId)
	{
		TSharedPtr<IPixelStreamingStreamer> ToBeDeleted;
		FScopeLock Lock(&StreamersCS);
		if (Streamers.Contains(StreamerId))
		{
			ToBeDeleted = Streamers[StreamerId];
			Streamers.Remove(StreamerId);
		}
		return ToBeDeleted;
	}

	void FPixelStreamingModule::SetExternalVideoSourceFPS(uint32 InFPS)
	{
		ExternalVideoSourceGroup->SetFPS(InFPS);
	}

	rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> FPixelStreamingModule::CreateExternalVideoSource()
	{
		return ExternalVideoSourceGroup->CreateVideoSource(false, []() { return true; });
	}

	void FPixelStreamingModule::ReleaseExternalVideoSource(const webrtc::VideoTrackSourceInterface* InVideoSource)
	{
		ExternalVideoSourceGroup->RemoveVideoSource(InVideoSource);
	}

	void FPixelStreamingModule::AddInputComponent(UPixelStreamingInput* InInputComponent)
	{
		InputComponents.Add(InInputComponent);
	}

	void FPixelStreamingModule::RemoveInputComponent(UPixelStreamingInput* InInputComponent)
	{
		InputComponents.Remove(InInputComponent);
	}

	const TArray<UPixelStreamingInput*> FPixelStreamingModule::GetInputComponents()
	{
		return InputComponents;
	}

	TUniquePtr<webrtc::VideoEncoderFactory> FPixelStreamingModule::CreateVideoEncoderFactory()
	{
		return MakeUnique<FVideoEncoderFactoryLayered>();
	}

	FString FPixelStreamingModule::GetDefaultStreamerID()
	{
		return Settings::GetDefaultStreamerID();
	}

	void FPixelStreamingModule::ForEachStreamer(const TFunction<void(TSharedPtr<IPixelStreamingStreamer>)>& Func)
	{
		TSet<FString> KeySet;
		{
			FScopeLock Lock(&StreamersCS);
			Streamers.GetKeys(KeySet);
		}
		for (auto&& StreamerId : KeySet)
		{
			if (TSharedPtr<IPixelStreamingStreamer> Streamer = GetStreamer(StreamerId))
			{
				Func(Streamer);
			}
		}
	}

	/**
	 * End IPixelStreamingModule implementation
	 */

	void FPixelStreamingModule::InitDefaultStreamer()
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("PixelStreaming streamer ID: %s"), *Settings::GetDefaultStreamerID());

		FString SignallingServerURL;
		if (!Settings::GetSignallingServerUrl(SignallingServerURL))
		{
			// didnt get the startup URL for pixel streaming. Check deprecated options...
			FString SignallingServerIP;
			uint16 SignallingServerPort;
			if (Settings::GetSignallingServerIP(SignallingServerIP) && Settings::GetSignallingServerPort(SignallingServerPort))
			{
				// got both old parameters. Warn about deprecation and build the proper url.
				UE_LOG(LogPixelStreaming, Warning, TEXT("PixelStreamingIP and PixelStreamingPort are deprecated flags. Use PixelStreamingURL instead. eg. -PixelStreamingURL=ws://%s:%d"), *SignallingServerIP, SignallingServerPort);
				SignallingServerURL = FString::Printf(TEXT("ws://%s:%d"), *SignallingServerIP, SignallingServerPort);
			}
			else
			{
				SignallingServerURL = Settings::GetDefaultSignallingURL();
				UE_LOG(LogPixelStreaming, Log, TEXT("-PixelStreamingURL was not specified on the command line, using the default connection url: %s"), *SignallingServerURL);
			}
		}

		TSharedPtr<IPixelStreamingStreamer> Streamer = CreateStreamer(Settings::GetDefaultStreamerID());
		// The PixelStreamingEditorModule handles setting video input in the editor
		if (!GIsEditor)
		{
			Streamer->SetVideoInput(FPixelStreamingVideoInputBackBuffer::Create());
		}

		if (!SignallingServerURL.IsEmpty())
		{
			Streamer->SetSignallingServerURL(SignallingServerURL);
		}
	}

	bool FPixelStreamingModule::IsPlatformCompatible() const
	{
		bool bCompatible = true;

#if PLATFORM_WINDOWS
		bool bWin8OrHigher = IsWindows8OrGreater();
		if (!bWin8OrHigher)
		{
			FString ErrorString(TEXT("Failed to initialize Pixel Streaming plugin because minimum requirement is Windows 8"));
			FText ErrorText = FText::FromString(ErrorString);
			FText TitleText = FText::FromString(TEXT("Pixel Streaming Plugin"));
			FMessageDialog::Open(EAppMsgType::Ok, ErrorText, &TitleText);
			UE_LOG(LogPixelStreaming, Error, TEXT("%s"), *ErrorString);
			bCompatible = false;
		}
#endif

		if (Settings::CVarPixelStreamingEncoderCodec.GetValueOnAnyThread() == "H264"
			&& !AVEncoder::FVideoEncoderFactory::Get().HasEncoderForCodec(AVEncoder::ECodecType::H264))
		{
			UE_LOG(LogPixelStreaming, Warning, TEXT("Could not setup hardware encoder for H.264. This is usually a driver issue, try reinstalling your drivers."));
			UE_LOG(LogPixelStreaming, Warning, TEXT("Falling back to VP8 software video encoding."));
			Settings::CVarPixelStreamingEncoderCodec.AsVariable()->Set(TEXT("VP8"), ECVF_SetByCommandline);
		}

		return bCompatible;
	}

	/**
	 * End own methods
	 */

	TSharedPtr<IInputDevice> FPixelStreamingModule::CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
	{
		return StreamerInputChannels;
	}

	void FPixelStreamingModule::RegisterCreateInputChannel(IPixelStreamingInputChannel::FCreateInputChannelFunc& InCreateInputChannel)
	{
		checkf(StreamerInputChannels, TEXT("StreamerInputChannels does not exist yet"));
		StreamerInputChannels->OverrideInputChannel(InCreateInputChannel);
	}
} // namespace UE::PixelStreaming

IMPLEMENT_MODULE(UE::PixelStreaming::FPixelStreamingModule, PixelStreaming)
