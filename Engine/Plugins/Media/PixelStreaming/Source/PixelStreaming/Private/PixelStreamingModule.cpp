// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingModule.h"
#include "IPixelStreamingInputModule.h"
#include "Streamer.h"
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
#include "PixelStreamingUtils.h"
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
#include "Stats.h"
#include "Video/Resources/VideoResourceRHI.h"
#include "PixelStreamingInputEnums.h"

DEFINE_LOG_CATEGORY(LogPixelStreaming);

IPixelStreamingModule* UE::PixelStreaming::FPixelStreamingModule::PixelStreamingModule = nullptr;

namespace UE::PixelStreaming
{
	typedef EPixelStreamingMessageTypes EType;
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
		// only D3D11/D3D12/Vulkan is supported
		if (!(RHIType == ERHIInterfaceType::D3D11 || RHIType == ERHIInterfaceType::D3D12 || RHIType == ERHIInterfaceType::Vulkan))
		{
#if !WITH_DEV_AUTOMATION_TESTS
			UE_LOG(LogPixelStreaming, Warning, TEXT("Only D3D11/D3D12/Vulkan Dynamic RHI is supported. Detected %s"), GDynamicRHI != nullptr ? GDynamicRHI->GetName() : TEXT("[null]"));
#endif
			return;
		}

		PopulateProtocol();
		RegisterCustomHandlers();

		// By calling InitDefaultStreamer post engine init we can use pixel streaming in standalone editor mode
		FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([this, RHIType]() {
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

			// HACK (Luke): Until or if we ever find a workaround for fencing, we need to ensure capture always uses a fence
			// if we don't then we get frequent and intermittent stuttering as textures are rendered to while being encoded.
			// From testing NVENC + CUDA pathway seems acceptable without a fence in most cases so we use the faster, unsafer path there.
			if (RHIType == ERHIInterfaceType::D3D11 || IsRHIDeviceAMD())
			{
				Settings::CVarPixelStreamingCaptureUseFence.AsVariable()->Set(true);
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

		rtc::InitializeSSL();
		RedirectWebRtcLogsToUnreal(rtc::LoggingSeverity::LS_VERBOSE);
		FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");

		// ExternalVideoSourceGroup is used so that we can have a video source without a streamer
		ExternalVideoSourceGroup = FVideoSourceGroup::Create();
		// ExternalVideoSourceGroup->SetVideoInput(FPixelStreamingVideoInputBackBuffer::Create());
		// ExternalVideoSourceGroup->Start();

		// Call FStats::Get() to initialize the singleton
		FStats::Get();
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
		ForEachStreamer([&bSuccess](TSharedPtr<IPixelStreamingStreamer> Streamer) {
			if (Streamer.IsValid())
			{
				Streamer->StartStreaming();
				bSuccess &= true;
			}
			else
			{
				bSuccess = false;
			}
		});
		return bSuccess;
	}

	void FPixelStreamingModule::StopStreaming()
	{
		ForEachStreamer([this](TSharedPtr<IPixelStreamingStreamer> Streamer) {
			if (Streamer.IsValid())
			{
				Streamer->StopStreaming();
			}
		});
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

	TUniquePtr<webrtc::VideoEncoderFactory> FPixelStreamingModule::CreateVideoEncoderFactory()
	{
		return MakeUnique<FVideoEncoderFactoryLayered>();
	}

	FString FPixelStreamingModule::GetDefaultStreamerID()
	{
		return Settings::GetDefaultStreamerID();
	}

	FString FPixelStreamingModule::GetDefaultSignallingURL()
	{
		return Settings::GetDefaultSignallingURL();
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
		}

		TSharedPtr<IPixelStreamingStreamer> Streamer = CreateStreamer(Settings::GetDefaultStreamerID());
		TSharedPtr<IPixelStreamingSignallingConnection> SignallingConnection = MakeShared<FPixelStreamingSignallingConnection>(Streamer->GetSignallingConnectionObserver().Pin(), Settings::GetDefaultStreamerID());
		SignallingConnection->SetAutoReconnect(true);
		Streamer->SetSignallingConnection(SignallingConnection);

		// The PixelStreamingEditorModule handles setting video input in the editor
		if (!GIsEditor)
		{
			// default to the scene viewport if we have a game engine
			if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
			{
				TSharedPtr<FSceneViewport> TargetViewport = GameEngine->SceneViewport;
				if (TargetViewport.IsValid())
				{
					Streamer->SetTargetViewport(TargetViewport->GetViewportWidget());
					Streamer->SetTargetWindow(TargetViewport->FindWindow());
				}
				else
				{
					UE_LOG(LogPixelStreaming, Error, TEXT("Cannot set target viewport/window - target viewport is not valid."));
				}
			}
		}

		if (!SignallingServerURL.IsEmpty())
		{
			// The user has specified a URL on the command line meaning their intention is to start streaming immediately
			// in that case, set up the video input for them (as long as we're not in editor)
			if (!GIsEditor)
			{
				Streamer->SetVideoInput(FPixelStreamingVideoInputBackBuffer::Create());
			}
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

		if ((Settings::CVarPixelStreamingEncoderCodec.GetValueOnAnyThread() == "H264" && !FVideoEncoder::IsSupported<FVideoResourceRHI, FVideoEncoderConfigH264>())
			|| (Settings::CVarPixelStreamingEncoderCodec.GetValueOnAnyThread() == "H265" && !FVideoEncoder::IsSupported<FVideoResourceRHI, FVideoEncoderConfigH265>()))
		{
			UE_LOG(LogPixelStreaming, Warning, TEXT("Could not setup hardware encoder. This is usually a driver issue, try reinstalling your drivers."));
			UE_LOG(LogPixelStreaming, Warning, TEXT("Falling back to VP8 software video encoding."));
			Settings::CVarPixelStreamingEncoderCodec.AsVariable()->Set(TEXT("VP8"), ECVF_SetByCommandline);
		}

		return bCompatible;
	}

	void FPixelStreamingModule::PopulateProtocol()
	{
		// Old EToStreamerMsg Commands
		/*
		 * Control Messages.
		 */
		// Simple command with no payload
		// Note, we only specify the ID when creating these messages to preserve backwards compatability
		// when adding your own message type, you can simply do FPixelStreamingInputProtocol.Direction.Add("XXX");
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("IFrameRequest", FPixelStreamingInputMessage(0));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("RequestQualityControl", FPixelStreamingInputMessage(1));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("FpsRequest", FPixelStreamingInputMessage(2));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("AverageBitrateRequest", FPixelStreamingInputMessage(3));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("StartStreaming", FPixelStreamingInputMessage(4));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("StopStreaming", FPixelStreamingInputMessage(5));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("LatencyTest", FPixelStreamingInputMessage(6));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("RequestInitialSettings", FPixelStreamingInputMessage(7));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("TestEcho", FPixelStreamingInputMessage(8));

		/*
		 * Input Messages.
		 */
		// Generic Input Messages.
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("UIInteraction", FPixelStreamingInputMessage(50));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("Command", FPixelStreamingInputMessage(51));

		// Keyboard Input Message.
		// Complex command with payload, therefore we specify the length of the payload (bytes) as well as the structure of the payload
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("KeyDown", FPixelStreamingInputMessage(60, { EType::Uint8, EType::Uint8 }));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("KeyUp", FPixelStreamingInputMessage(61, { EType::Uint8 }));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("KeyPress", FPixelStreamingInputMessage(62, { EType::Uint16 }));

		// Mouse Input Messages.
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("MouseEnter", FPixelStreamingInputMessage(70));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("MouseLeave", FPixelStreamingInputMessage(71));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("MouseDown", FPixelStreamingInputMessage(72, { EType::Uint8, EType::Uint16, EType::Uint16 }));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("MouseUp", FPixelStreamingInputMessage(73, { EType::Uint8, EType::Uint16, EType::Uint16 }));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("MouseMove", FPixelStreamingInputMessage(74, { EType::Uint16, EType::Uint16, EType::Uint16, EType::Uint16 }));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("MouseWheel", FPixelStreamingInputMessage(75, { EType::Int16, EType::Uint16, EType::Uint16 }));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("MouseDouble", FPixelStreamingInputMessage(76, { EType::Uint8, EType::Uint16, EType::Uint16 }));

		// Touch Input Messages.
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("TouchStart", FPixelStreamingInputMessage(80, { EType::Uint8, EType::Uint16, EType::Uint16, EType::Uint8, EType::Uint8, EType::Uint8 }));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("TouchEnd", FPixelStreamingInputMessage(81, { EType::Uint8, EType::Uint16, EType::Uint16, EType::Uint8, EType::Uint8, EType::Uint8 }));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("TouchMove", FPixelStreamingInputMessage(82, { EType::Uint8, EType::Uint16, EType::Uint16, EType::Uint8, EType::Uint8, EType::Uint8 }));

		// Gamepad Input Messages.
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("GamepadButtonPressed", FPixelStreamingInputMessage(90, { EType::Uint8, EType::Uint8, EType::Uint8 }));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("GamepadButtonReleased", FPixelStreamingInputMessage(91, { EType::Uint8, EType::Uint8, EType::Uint8 }));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("GamepadAnalog", FPixelStreamingInputMessage(92, { EType::Uint8, EType::Uint8, EType::Double }));

		// XR Input Messages.
		// clang-format off
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("XRHMDTransform", FPixelStreamingInputMessage(110, {	// 4x4 Transform
																													EType::Float, EType::Float, EType::Float, EType::Float,
																													EType::Float, EType::Float, EType::Float, EType::Float,
																													EType::Float, EType::Float, EType::Float, EType::Float,
																													EType::Float, EType::Float, EType::Float, EType::Float,
																												}));
		
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("XRControllerTransform", FPixelStreamingInputMessage(111, {// 4x4 Transform
																														EType::Float, EType::Float, EType::Float, EType::Float, 
																														EType::Float, EType::Float, EType::Float, EType::Float, 
																														EType::Float, EType::Float, EType::Float, EType::Float, 
																														EType::Float, EType::Float, EType::Float, EType::Float,
																														// Handedness (L, R, Any)
																														EType::Uint8 
																														}));
		
		// clang-format on
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("XRButtonPressed", FPixelStreamingInputMessage(112, // Handedness,   ButtonIdx,      IsRepeat
																												{ EType::Uint8, EType::Uint8, EType::Uint8 }));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("XRButtonTouched", FPixelStreamingInputMessage(113, // Handedness,   ButtonIdx,      IsRepeat
																												{ EType::Uint8, EType::Uint8, EType::Uint8 }));
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("XRButtonReleased", FPixelStreamingInputMessage(114, // Handedness,   ButtonIdx,     IsRepeat
																					 							{ EType::Uint8, EType::Uint8, EType::Uint8 }));
																												
		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("XRAnalog", FPixelStreamingInputMessage(115, { EType::Uint8, EType::Uint8, EType::Double }));

		FPixelStreamingInputProtocol::ToStreamerProtocol.Add("XRSystem", FPixelStreamingInputMessage(116, { EType::Uint8 }));

		// Old EToPlayerMsg commands
		FPixelStreamingInputProtocol::FromStreamerProtocol.Add("QualityControlOwnership", FPixelStreamingInputMessage(0));
		FPixelStreamingInputProtocol::FromStreamerProtocol.Add("Response", FPixelStreamingInputMessage(1));
		FPixelStreamingInputProtocol::FromStreamerProtocol.Add("Command", FPixelStreamingInputMessage(2));
		FPixelStreamingInputProtocol::FromStreamerProtocol.Add("FreezeFrame", FPixelStreamingInputMessage(3));
		FPixelStreamingInputProtocol::FromStreamerProtocol.Add("UnfreezeFrame", FPixelStreamingInputMessage(4));
		FPixelStreamingInputProtocol::FromStreamerProtocol.Add("VideoEncoderAvgQP", FPixelStreamingInputMessage(5));
		FPixelStreamingInputProtocol::FromStreamerProtocol.Add("LatencyTest", FPixelStreamingInputMessage(6));
		FPixelStreamingInputProtocol::FromStreamerProtocol.Add("InitialSettings", FPixelStreamingInputMessage(7));
		FPixelStreamingInputProtocol::FromStreamerProtocol.Add("FileExtension", FPixelStreamingInputMessage(8));
		FPixelStreamingInputProtocol::FromStreamerProtocol.Add("FileMimeType", FPixelStreamingInputMessage(9));
		FPixelStreamingInputProtocol::FromStreamerProtocol.Add("FileContents", FPixelStreamingInputMessage(10));
		FPixelStreamingInputProtocol::FromStreamerProtocol.Add("TestEcho", FPixelStreamingInputMessage(11));
		FPixelStreamingInputProtocol::FromStreamerProtocol.Add("InputControlOwnership", FPixelStreamingInputMessage(12));
		FPixelStreamingInputProtocol::FromStreamerProtocol.Add("Protocol", FPixelStreamingInputMessage(255));
	}

	void FPixelStreamingModule::RegisterCustomHandlers()
	{
		IPixelStreamingInputModule& InputModule = IPixelStreamingInputModule::Get();
		InputModule.RegisterMessage(EPixelStreamingMessageDirection::ToStreamer,
			"UIInteraction",
			FPixelStreamingInputMessage(50),
			[this](FMemoryReader Ar) { HandleUIInteraction(Ar); });

		// The current handler is the function that will currently be executed if a message with type "Command" is received
		TFunction<void(FMemoryReader)> BaseOnCommandHandler = InputModule.FindMessageHandler("Command");
		// We then create our new handler which will execute the "base" handler
		TFunction<void(FMemoryReader)> ExtendedOnCommandHandler = [this, BaseOnCommandHandler](FMemoryReader Ar) {
			// and then perform out extended functionality after.
			// equivalent to the super::DoSomeFunc pattern
			BaseOnCommandHandler(Ar);

			// In this case, the base handler handles raw console commands. Our extended functionality parses the commands
			// for PS specific parameters
			HandleOnCommand(Ar);
		};

		// Handle receiving commands from peers
		InputModule.RegisterMessage(EPixelStreamingMessageDirection::ToStreamer, "Command", FPixelStreamingInputMessage(51), ExtendedOnCommandHandler);
		// Handle sending commands to peers
		InputModule.OnSendMessage.AddRaw(this, &UE::PixelStreaming::FPixelStreamingModule::HandleSendCommand);
	}

	void FPixelStreamingModule::HandleOnCommand(FMemoryReader Ar)
	{
		FString Res;
		Res.GetCharArray().SetNumUninitialized(Ar.TotalSize() / 2 + 1);
		Ar.Serialize(Res.GetCharArray().GetData(), Ar.TotalSize());
		FString Descriptor = Res.Mid(1);
		bool bSuccess = false;

		/**
		 * Encoder Settings
		 */
		FString MinQPString;
		UE::PixelStreaming::ExtractJsonFromDescriptor(Descriptor, TEXT("Encoder.MinQP"), MinQPString, bSuccess);
		if (bSuccess)
		{
			int MinQP = FCString::Atoi(*MinQPString);
			UE::PixelStreaming::Settings::CVarPixelStreamingEncoderMinQP->Set(MinQP, ECVF_SetByCommandline);
			return;
		}

		FString MaxQPString;
		UE::PixelStreaming::ExtractJsonFromDescriptor(Descriptor, TEXT("Encoder.MaxQP"), MaxQPString, bSuccess);
		if (bSuccess)
		{
			int MaxQP = FCString::Atoi(*MaxQPString);
			UE::PixelStreaming::Settings::CVarPixelStreamingEncoderMaxQP->Set(MaxQP, ECVF_SetByCommandline);
			return;
		}

		/**
		 * WebRTC Settings
		 */
		FString FPSString;
		UE::PixelStreaming::ExtractJsonFromDescriptor(Descriptor, TEXT("WebRTC.Fps"), FPSString, bSuccess);
		if (bSuccess)
		{
			int FPS = FCString::Atoi(*FPSString);
			UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCFps->Set(FPS, ECVF_SetByCommandline);
			return;
		}

		FString MinBitrateString;
		UE::PixelStreaming::ExtractJsonFromDescriptor(Descriptor, TEXT("WebRTC.MinBitrate"), MinBitrateString, bSuccess);
		if (bSuccess)
		{
			int MinBitrate = FCString::Atoi(*MinBitrateString);
			UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCMinBitrate->Set(MinBitrate, ECVF_SetByCommandline);
			return;
		}

		FString MaxBitrateString;
		UE::PixelStreaming::ExtractJsonFromDescriptor(Descriptor, TEXT("WebRTC.MaxBitrate"), MaxBitrateString, bSuccess);
		if (bSuccess)
		{
			int MaxBitrate = FCString::Atoi(*MaxBitrateString);
			UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCMaxBitrate->Set(MaxBitrate, ECVF_SetByCommandline);
			return;
		}
	}

	void FPixelStreamingModule::HandleSendCommand(FMemoryReader Ar)
	{
		FString Descriptor;
		Ar << Descriptor;
		ForEachStreamer([&Descriptor, this](TSharedPtr<IPixelStreamingStreamer> Streamer) {
			Streamer->SendPlayerMessage(FPixelStreamingInputProtocol::FromStreamerProtocol.Find("Command")->GetID(), Descriptor);
		});
	}

	void FPixelStreamingModule::HandleUIInteraction(FMemoryReader Ar)
	{
		FString Res;
		Res.GetCharArray().SetNumUninitialized(Ar.TotalSize() / 2 + 1);
		Ar.Serialize(Res.GetCharArray().GetData(), Ar.TotalSize());

		FString Descriptor = Res.Mid(1);

		UE_LOG(LogPixelStreaming, Verbose, TEXT("UIInteraction: %s"), *Descriptor);
		for (UPixelStreamingInput* InputComponent : InputComponents)
		{
			InputComponent->OnInputEvent.Broadcast(Descriptor);
		}
	}
	/**
	 * End own methods
	 */

	/**
	 * Deprecated methods
	 */
	const FPixelStreamingInputProtocol FPixelStreamingModule::GetProtocol()
	{
		return FPixelStreamingInputProtocol();
	}

	void FPixelStreamingModule::RegisterMessage(EPixelStreamingMessageDirection MessageDirection, const FString& MessageType, FPixelStreamingInputMessage Message, const TFunction<void(FMemoryReader)>& Handler)
	{
		IPixelStreamingInputModule::Get().RegisterMessage(MessageDirection, MessageType, Message, Handler);
	}

	TFunction<void(FMemoryReader)> FPixelStreamingModule::FindMessageHandler(const FString& MessageType)
	{
		return IPixelStreamingInputModule::Get().FindMessageHandler(MessageType);
	}
	/**
	 * End deprecated methods
	 */
} // namespace UE::PixelStreaming

IMPLEMENT_MODULE(UE::PixelStreaming::FPixelStreamingModule, PixelStreaming)
