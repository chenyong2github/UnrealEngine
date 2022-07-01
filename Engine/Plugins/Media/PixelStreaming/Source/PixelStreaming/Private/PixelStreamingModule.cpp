// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingModule.h"
#include "Streamer.h"
#include "InputDevice.h"
#include "PixelStreamingInputComponent.h"
#include "PixelStreamingDelegates.h"
#include "PixelStreamingSignallingConnection.h"
#include "Settings.h"
#include "PixelStreamingPrivate.h"
#include "PlayerSession.h"
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
#include "VideoEncoderFactorySimple.h"
#include "WebRTCLogging.h"
#include "WebSocketsModule.h"

#if !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
#endif

#include "VideoInputBackBuffer.h"
#include "VideoSourceGroup.h"
#include "PixelStreamingPeerConnection.h"

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

		StreamerInputDevices = MakeShared<FStreamerInputDevices>(FSlateApplication::Get().GetPlatformApplication()->GetMessageHandler());
		IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);

		// only D3D11/D3D12/Vulkan is supported
		if (RHIType == ERHIInterfaceType::D3D11 || RHIType == ERHIInterfaceType::D3D12 || RHIType == ERHIInterfaceType::Vulkan)
		{
			// By calling InitDefaultStreamer post engine init we can use pixel streaming in standalone editor mode
			FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([this]()
			{
				InitDefaultStreamer();
				bModuleReady = true;
				ReadyEvent.Broadcast(*this);
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
		ExternalVideoSourceGroup = MakeUnique<FVideoSourceGroup>();
		ExternalVideoSourceGroup->SetVideoInput(MakeShared<FVideoInputBackBuffer>());
		ExternalVideoSourceGroup->Start();

		bStartupCompleted = true;
	}

	void FPixelStreamingModule::ShutdownModule()
	{
		if (!bStartupCompleted)
		{
			return;
		}

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
		if(ExistingStreamer)
		{
			return ExistingStreamer;
		}

		TSharedPtr<FStreamer> NewStreamer = MakeShared<FStreamer>(StreamerId);
		{
			FScopeLock Lock(&StreamersCS);
			Streamers.Add(StreamerId, NewStreamer);
		}

		NewStreamer->SetInputDevice(StreamerInputDevices->CreateInputDevice());
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

	rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> FPixelStreamingModule::CreateExternalVideoSource()
	{
		return ExternalVideoSourceGroup->CreateVideoSource([]() { return true; });
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

	webrtc::VideoEncoderFactory* FPixelStreamingModule::CreateVideoEncoderFactory()
	{
		return new FVideoEncoderFactorySimple();
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
		for(auto&& StreamerId : KeySet)
		{
			if(TSharedPtr<IPixelStreamingStreamer> Streamer = GetStreamer(StreamerId))
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
		UE_LOG(LogPixelStreaming, Log, TEXT("PixelStreaming endpoint ID: %s"), *Settings::GetDefaultStreamerID());

		// Check to see if we can use the Pixel Streaming plugin on this platform.
		// If not then we avoid setting up our delegates to prevent access to the
		// plugin. Note that Pixel Streaming is not currently performed in the
		// Editor.
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
		verify(FModuleManager::Get().LoadModule(FName("ImageWrapper")));

		FString SignallingServerURL;
		if (!FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingURL="), SignallingServerURL))
		{
			// didnt get the startup URL for pixel streaming. Check deprecated options...
			FString SignallingServerIP;
			uint16 SignallingServerPort;
			if (FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingIP="), SignallingServerIP)
				&& FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingPort="), SignallingServerPort))
			{
				// got both old parameters. Warn about deprecation and build the proper url.
				UE_LOG(LogPixelStreaming, Warning, TEXT("PixelStreamingIP and PixelStreamingPort are deprecated flags. Use PixelStreamingURL instead. eg. -PixelStreamingURL=ws://%s:%d"), *SignallingServerIP, SignallingServerPort);
				SignallingServerURL = FString::Printf(TEXT("ws://%s:%d"), *SignallingServerIP, SignallingServerPort);
			}
		}

		TSharedPtr<IPixelStreamingStreamer> Streamer = CreateStreamer(Settings::GetDefaultStreamerID());
		Streamer->SetVideoInput(MakeShared<FVideoInputBackBuffer>());

		if (!SignallingServerURL.IsEmpty())
		{
			// have a startup url. dont start in editor though.
			if (GIsEditor)
			{
				FText TitleText = FText::FromString(TEXT("Pixel Streaming Plugin"));
				FString ErrorString = TEXT("Pixel Streaming Plugin is not supported in editor, but it was explicitly enabled by command-line arguments. Please remove `PixelStreamingURL` or `PixelStreamingIP` and `PixelStreamingPort` args from editor command line.");
				FText ErrorText = FText::FromString(ErrorString);
				FMessageDialog::Open(EAppMsgType::Ok, ErrorText, &TitleText);
				UE_LOG(LogPixelStreaming, Error, TEXT("%s"), *ErrorString);
			}
			else
			{
				Streamer->SetSignallingServerURL(SignallingServerURL);
				Streamer->StartStreaming();
			}
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

	TSharedPtr<IInputDevice> FPixelStreamingModule::CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
	{
		return StreamerInputDevices;
	}

	void FPixelStreamingModule::RegisterCreateInputDevice(IPixelStreamingInputDevice::FCreateInputDeviceFunc& InCreateInputDevice)
	{
		checkf(StreamerInputDevices, TEXT("StreamerInputDevices does not exist yet"));
		StreamerInputDevices->OverrideInputDevice(InCreateInputDevice);
	}
	/**
	 * End own methods
	 */
} // namespace UE::PixelStreaming

IMPLEMENT_MODULE(UE::PixelStreaming::FPixelStreamingModule, PixelStreaming)
