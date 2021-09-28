// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingModule.h"
#include "FreezeFrame.h"
#include "Streamer.h"
#include "InputDevice.h"
#include "PixelStreamerInputComponent.h"
#include "PixelStreamerDelegates.h"
#include "SignallingServerConnection.h"
#include "PixelStreamingSettings.h"
#include "PixelStreamingStats.h"
#include "PixelStreamingPrivate.h"
#include "PlayerSession.h"
#include "PixelStreamingAudioSink.h"
#include "LatencyTester.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Texture2D.h"
#include "Slate/SceneViewport.h"

#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
#include "Windows/WindowsHWrapper.h"
#elif PLATFORM_LINUX
#include "CudaModule.h"
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
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Async/Async.h"
#include "Engine/Engine.h"

#if !UE_BUILD_SHIPPING
#	include "DrawDebugHelpers.h"
#endif

DEFINE_LOG_CATEGORY(PixelStreaming);

namespace
{
	

	#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
	// required for WMF video decoding
	// some Windows versions don't have Media Foundation preinstalled. We configure MF DLLs as delay-loaded and load them manually here
	// checking the result and avoiding error message box if failed
	bool LoadMediaFoundationDLLs()
	{
		// Ensure that all required modules are preloaded so they are not loaded just-in-time, causing a hitch.
		if (IsWindows8Plus())
		{
			return FPlatformProcess::GetDllHandle(TEXT("mf.dll"))
				&& FPlatformProcess::GetDllHandle(TEXT("mfplat.dll"))
				&& FPlatformProcess::GetDllHandle(TEXT("msmpeg2vdec.dll"))
				&& FPlatformProcess::GetDllHandle(TEXT("MSAudDecMFT.dll"));
		}
		else // Windows 7
		{
			return FPlatformProcess::GetDllHandle(TEXT("mf.dll"))
				&& FPlatformProcess::GetDllHandle(TEXT("mfplat.dll"))
				&& FPlatformProcess::GetDllHandle(TEXT("msmpeg2vdec.dll"))
				&& FPlatformProcess::GetDllHandle(TEXT("msmpeg2adec.dll"));
		}
	}
	#endif
}

void FPixelStreamingModule::InitStreamer()
{
	// Cap the engine framerate to what WebRTC
	GEngine->SetMaxFPS(PixelStreamingSettings::CVarPixelStreamingWebRTCMaxFps.GetValueOnAnyThread());

	FString StreamerId;
	FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingID="), StreamerId);

	FString SignallingServerURL;
	if (!FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingURL="), SignallingServerURL)) {

		FString SignallingServerIP;
		uint16 SignallingServerPort = 8888;
		if (!FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingIP="), SignallingServerIP) ||
			!FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingPort="), SignallingServerPort))
		{
			UE_LOG(PixelStreamer, Log, TEXT("PixelStreaming is disabled, provide `PixelStreamingIP` and `PixelStreamingPort` cmd-args to enable it"));
			return;
		}
		UE_LOG(PixelStreamer, Warning, TEXT("PixelStreamingIP and PixelStreamingPort are deprecated flags. Use PixelStreamingURL instead. eg. -PixelStreamingURL=ws://%s:%d"), *SignallingServerIP, SignallingServerPort);
		SignallingServerURL = FString::Printf(TEXT("ws://%s:%d"), *SignallingServerIP, SignallingServerPort);
	}

	UE_LOG(PixelStreamer, Log, TEXT("PixelStreaming endpoint ID: %s"), *StreamerId);

	if (GIsEditor)
	{
		FText TitleText = FText::FromString(TEXT("Pixel Streaming Plugin"));
		FString ErrorString = TEXT("Pixel Streaming Plugin is not supported in editor, but it was explicitly enabled by command-line arguments. Please remove `PixelStreamingIP` and `PixelStreamingPort` args from editor command line.");
		FText ErrorText = FText::FromString(ErrorString);
		FMessageDialog::Open(EAppMsgType::Ok, ErrorText, &TitleText);
		UE_LOG(PixelStreamer, Error, TEXT("%s"), *ErrorString);
		return;
	}

	// Check to see if we can use the Pixel Streaming plugin on this platform.
	// If not then we avoid setting up our delegates to prevent access to the
	// plugin. Note that Pixel Streaming is not currently performed in the
	// Editor.
	if (!CheckPlatformCompatibility())
	{
		return;
	}

	if (!ensure(GEngine != nullptr))
	{
		return;
	}

	// subscribe to engine delegates here for init / framebuffer creation / whatever
	// TODO check if there is a better callback to attach so that we can use with editor
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddRaw(this, &FPixelStreamingModule::OnBackBufferReady_RenderThread);
	}

	FGameModeEvents::GameModePostLoginEvent.AddRaw(this, &FPixelStreamingModule::OnGameModePostLogin);
	FGameModeEvents::GameModeLogoutEvent.AddRaw(this, &FPixelStreamingModule::OnGameModeLogout);

	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);

	FApp::SetUnfocusedVolumeMultiplier(1.0f);

	// Allow Pixel Streaming to broadcast to various delegates bound in the
	// application-specific blueprint.
	UPixelStreamerDelegates::CreateInstance();

	// Allow Pixel Streaming to be frozen and a freeze frame image to be used
	// instead of the video stream.
	UFreezeFrame::CreateInstance();
	verify(FModuleManager::Get().LoadModule(FName("ImageWrapper")));

	Streamer = MakeUnique<FStreamer>(SignallingServerURL, StreamerId);
}

/** IModuleInterface implementation */
void FPixelStreamingModule::StartupModule()
{
	// Pixel Streaming does not make sense without an RHI so we don't run in commandlets without one.
	if(IsRunningCommandlet() && !IsAllowCommandletRendering())
	{
		return;
	}

	// Initialise all settings from command line args etc
	PixelStreamingSettings::InitialiseSettings();

	// only D3D11/D3D12 is supported
	if (GDynamicRHI == nullptr ||
		!( GDynamicRHI->GetName() == FString(TEXT("D3D11")) || 
		   GDynamicRHI->GetName() == FString(TEXT("D3D12"))	||
		   GDynamicRHI->GetName() == FString(TEXT("Vulkan"))))
	{
		UE_LOG(PixelStreaming, Warning, TEXT("Only D3D11/D3D12/Vulkan Dynamic RHI is supported. Detected %s"), GDynamicRHI != nullptr ? GDynamicRHI->GetName() : TEXT("[null]"));
		return;
	}
	else if( GDynamicRHI->GetName() == FString(TEXT("D3D11")) || 
		   	 GDynamicRHI->GetName() == FString(TEXT("D3D12")) ||
			 GDynamicRHI->GetName() == FString(TEXT("Vulkan")))
	{
		// By calling InitStreamer post engine init we can use pixel streaming in standalone editor mode
		FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FPixelStreamingModule::InitStreamer);
	}
}

void FPixelStreamingModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
		FSlateApplication::Get().GetRenderer()->OnPreResizeWindowBackBuffer().RemoveAll(this);
	}

	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);
}

bool FPixelStreamingModule::CheckPlatformCompatibility() const
{
	bool bCompatible = true;

	#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
	bool bWin8OrHigher = FPlatformMisc::VerifyWindowsVersion(6, 2);
	if (!bWin8OrHigher)
	{
		FString ErrorString(TEXT("Failed to initialize Pixel Streaming plugin because minimum requirement is Windows 8"));
		FText ErrorText = FText::FromString(ErrorString);
		FText TitleText = FText::FromString(TEXT("Pixel Streaming Plugin"));
		FMessageDialog::Open(EAppMsgType::Ok, ErrorText, &TitleText);
		UE_LOG(PixelStreamer, Error, TEXT("%s"), *ErrorString);
		bCompatible = false;
	}
	#endif
	
	if (!FStreamer::CheckPlatformCompatibility())
	{
		FText TitleText = FText::FromString(TEXT("Pixel Streaming Plugin"));
		FString ErrorString = TEXT("No compatible GPU found, or failed to load their respective encoder libraries");
		FText ErrorText = FText::FromString(ErrorString);
		FMessageDialog::Open(EAppMsgType::Ok, ErrorText, &TitleText);
		UE_LOG(PixelStreamer, Error, TEXT("%s"), *ErrorString);
		bCompatible = false;
	}

	return bCompatible;
}

void FPixelStreamingModule::UpdateViewport(FSceneViewport* Viewport)
{
	FRHIViewport* const ViewportRHI = Viewport->GetViewportRHI().GetReference();
}

void FPixelStreamingModule::OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer)
{
	// enable streaming explicitly by providing `PixelStreamingIP` and `PixelStreamingPort` cmd-args
	if (!Streamer)
	{
		return;
	}

	check(IsInRenderingThread());

	if (!bFrozen)
	{
		Streamer->OnFrameBufferReady(BackBuffer);
	}

	// Check to see if we have been instructed to capture the back buffer as a
	// freeze frame.
	if (bCaptureNextBackBufferAndStream && Streamer->IsStreaming())
	{
		bCaptureNextBackBufferAndStream = false;

		// Read the data out of the back buffer and send as a JPEG.
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		FIntRect Rect(0, 0, BackBuffer->GetSizeX(), BackBuffer->GetSizeY());
		TArray<FColor> Data;

		RHICmdList.ReadSurfaceData(BackBuffer, Rect, Data, FReadSurfaceDataFlags());
		SendJpeg(MoveTemp(Data), Rect);
	}
}

TSharedPtr<class IInputDevice> FPixelStreamingModule::CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	InputDevice = MakeShareable(new FInputDevice(InMessageHandler));
	return InputDevice;
}

FInputDevice& FPixelStreamingModule::GetInputDevice()
{
	return *InputDevice;
}

TSharedPtr<FInputDevice> FPixelStreamingModule::GetInputDevicePtr()
{
	return InputDevice;
}

void FPixelStreamingModule::AddInputComponent(UPixelStreamerInputComponent* InInputComponent)
{
	this->InputComponents.Add(InInputComponent);
}

void FPixelStreamingModule::RemoveInputComponent(UPixelStreamerInputComponent* InInputComponent)
{
	this->InputComponents.Remove(InInputComponent);
}

const TArray<UPixelStreamerInputComponent*> FPixelStreamingModule::GetInputComponents()
{
	return this->InputComponents;
}

void FPixelStreamingModule::FreezeFrame(UTexture2D* Texture)
{
	if (Texture)
	{		
		ENQUEUE_RENDER_COMMAND(ReadSurfaceCommand)([this, Texture](FRHICommandListImmediate& RHICmdList)
		{
			// A frame is supplied so immediately read its data and send as a JPEG.
			FTexture2DRHIRef Texture2DRHI = (Texture->Resource && Texture->Resource->TextureRHI) ? Texture->Resource->TextureRHI->GetTexture2D() : nullptr;
			if (!Texture2DRHI)
			{
				UE_LOG(PixelStreamer, Error, TEXT("Attempting freeze frame with texture %s with no texture 2D RHI"), *Texture->GetName());
				return;
			}
			uint32 Width = Texture2DRHI->GetSizeX();
			uint32 Height = Texture2DRHI->GetSizeY();
			// Create empty texture
			FRHIResourceCreateInfo CreateInfo(TEXT("FreezeFrameTexture"));
			FTexture2DRHIRef DestTexture = GDynamicRHI->RHICreateTexture2D(Width, Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_RenderTargetable, ERHIAccess::Present, CreateInfo);
			// Copy freeze frame texture to empty texture
			CopyTexture(Texture2DRHI, DestTexture);

			TArray<FColor> Data;
			FIntRect Rect(0, 0, Width, Height);
			RHICmdList.ReadSurfaceData(DestTexture, Rect, Data, FReadSurfaceDataFlags());
			this->SendJpeg(MoveTemp(Data), Rect);
		});
	}
	else
	{
		// A frame is not supplied, so we need to capture the back buffer at
		// the next opportunity, and send as a JPEG.
		bCaptureNextBackBufferAndStream = true;
	}

	// Stop streaming.
	bFrozen = true;
}

void FPixelStreamingModule::UnfreezeFrame()
{
	Streamer->SendUnfreezeFrame();
	
	// Resume streaming.
	bFrozen = false;
}

void FPixelStreamingModule::AddPlayerConfig(TSharedRef<FJsonObject>& JsonObject)
{
	checkf(InputDevice.IsValid(), TEXT("No Input Device available when populating Player Config"));

	JsonObject->SetBoolField(TEXT("FakingTouchEvents"), InputDevice->IsFakingTouchEvents());

	FString PixelStreamingControlScheme;
	if (PixelStreamingSettings::GetControlScheme(PixelStreamingControlScheme))
	{
		JsonObject->SetStringField(TEXT("ControlScheme"), PixelStreamingControlScheme);
	}

	float PixelStreamingFastPan;
	if (PixelStreamingSettings::GetFastPan(PixelStreamingFastPan))
	{
		JsonObject->SetNumberField(TEXT("FastPan"), PixelStreamingFastPan);
	}
}

void FPixelStreamingModule::SendResponse(const FString& Descriptor)
{
	Streamer->SendPlayerMessage(PixelStreamingProtocol::EToPlayerMsg::Response, Descriptor);
}

void FPixelStreamingModule::SendCommand(const FString& Descriptor)
{
	Streamer->SendPlayerMessage(PixelStreamingProtocol::EToPlayerMsg::Command, Descriptor);
}

void FPixelStreamingModule::OnGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	
}

void FPixelStreamingModule::OnGameModeLogout(AGameModeBase* GameMode, AController* Exiting)
{
	
}

void FPixelStreamingModule::SendJpeg(TArray<FColor> RawData, const FIntRect& Rect)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
	bool bSuccess = ImageWrapper->SetRaw(RawData.GetData(), RawData.Num() * sizeof(FColor), Rect.Width(), Rect.Height(), ERGBFormat::BGRA, 8);
	if (bSuccess)
	{
		// Compress to a JPEG of the maximum possible quality.
		int32 Quality = PixelStreamingSettings::CVarPixelStreamingFreezeFrameQuality.GetValueOnAnyThread();
		const TArray64<uint8>& JpegBytes = ImageWrapper->GetCompressed(Quality);
		Streamer->SendFreezeFrame(JpegBytes);
	}
	else
	{
		UE_LOG(PixelStreamer, Error, TEXT("JPEG image wrapper failed to accept frame data"));
	}
}

bool FPixelStreamingModule::IsTickableWhenPaused() const
{
	return true;
}

bool FPixelStreamingModule::IsTickableInEditor() const
{
	return true;
}

void FPixelStreamingModule::Tick(float DeltaTime)
{
	FPixelStreamingStats::Get().Tick();

	// If we are running a latency test then check if we have timing results and if we do transmit them
	if(FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::RESULTS_READY)
	{
		FString LatencyResults;
		FPlayerId LatencyTestPlayerId;
		bool bEnded = FLatencyTester::End(LatencyResults, LatencyTestPlayerId);
		if(bEnded)
		{
			Streamer->SendMessage(LatencyTestPlayerId, PixelStreamingProtocol::EToPlayerMsg::LatencyTest, LatencyResults);
		}
	}

	// Send video encoder averaged QP approx every 1 second
	if(this->Streamer.IsValid() && this->Streamer->IsStreaming() && this->Streamer->GetNumPlayers() > 0)
	{
		double Now = FPlatformTime::Seconds();
		if (Now - LastVideoEncoderQPReportTime > 1)
		{
			Streamer->SendLatestQPAllPlayers();
			LastVideoEncoderQPReportTime = FPlatformTime::Seconds();
		}
	}
	
}

TStatId FPixelStreamingModule::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FPixelStreamingModule, STATGROUP_Tickables);
}

IPixelStreamingAudioSink* FPixelStreamingModule::GetPeerAudioSink(FPlayerId PlayerId)
{
	if(!this->Streamer.IsValid())
	{
		return nullptr;
	}

	return this->Streamer->GetAudioSink(PlayerId);
}

IPixelStreamingAudioSink* FPixelStreamingModule::GetUnlistenedAudioSink()
{
	if(!this->Streamer.IsValid())
	{
		return nullptr;
	}

	return this->Streamer->GetUnlistenedAudioSink();
}

IMPLEMENT_MODULE(FPixelStreamingModule, PixelStreaming)
