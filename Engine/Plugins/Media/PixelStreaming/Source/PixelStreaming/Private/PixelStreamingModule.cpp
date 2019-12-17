// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingModule.h"
#include "FreezeFrame.h"
#include "Player.h"
#include "Streamer.h"
#include "InputDevice.h"
#include "PixelStreamerInputComponent.h"
#include "PixelStreamerDelegates.h"
#include "SessionMonitorConnection.h"
#include "SignallingServerConnection.h"
#include "HUDStats.h"
#include "PixelStreamingPrivate.h"

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Texture2D.h"
#include "Slate/SceneViewport.h"
#include "Windows/WindowsHWrapper.h"
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
	TAutoConsoleVariable<int32> CVarFreezeFrameQuality(
		TEXT("PixelStreaming.FreezeFrameQuality"),
		100,
		TEXT("Compression quality of the freeze frame"),
		ECVF_Default
	);

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
}

void FPixelStreamingModule::InitStreamer()
{
	FString SignallingServerIP;
	uint16 SignallingServerPort = 8888;
	if (!FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingIP="), SignallingServerIP) ||
		!FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingPort="), SignallingServerPort))
	{
		UE_LOG(PixelStreamer, Log, TEXT("PixelStreaming is disabled, provide `PixelStreamingIP` and `PixelStreamingPort` cmd-args to enable it"));
		return;
	}

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

	uint16 SessionMonitorPort = 0;
	FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingSessionMonitorPort="), SessionMonitorPort);
	if (SessionMonitorPort)
	{
		SessionMonitorConnection = MakeUnique<FSessionMonitorConnection>(SessionMonitorPort);
	}

	Streamer = MakeUnique<FStreamer>(FString::Printf(TEXT("ws://%s:%d"), *SignallingServerIP, SignallingServerPort));
}

void FPixelStreamingModule::InitPlayer()
{
	check(!bPlayerInitialized);

	// Win7+ only
	if (!IsWindows7Plus())
	{
		UE_LOG(PixelPlayer, Log, TEXT("PixelStreamingPlayer plugin is incompatible with Windows prior to 7.0 version: %s"), *FPlatformMisc::GetOSVersion());
		return;
	}

	if (!LoadMediaFoundationDLLs())
	{
		UE_LOG(PixelPlayer, Log, TEXT("Can't load Media Foundation, %s"), *FPlatformMisc::GetOSVersion());
		return;
	}

	HRESULT Res = MFStartup(MF_VERSION);
	checkf(SUCCEEDED(Res), TEXT("MFStartup failed: %d"), Res);
	if (GIsClient)
	{
		bool bRes = FPlayer::CreateDXManagerAndDevice();
		if (!bRes)
		{
			UE_LOG(PixelPlayer, Warning, TEXT("Failed to create DXGI Manager and Device"));
		}
	}

	bPlayerInitialized = true;

}

/** IModuleInterface implementation */
void FPixelStreamingModule::StartupModule()
{
	// only D3D11/D3D12 is supported
	if (
		GDynamicRHI == nullptr ||
		!(GDynamicRHI->GetName() == FString(TEXT("D3D11")) || GDynamicRHI->GetName() == FString(TEXT("D3D12")))
		)
	{
		UE_LOG(PixelStreaming, Log, TEXT("Only D3D11/D3D12 Dynamic RHI is supported. Detected %s"), GDynamicRHI != nullptr ? GDynamicRHI->GetName() : TEXT("[null]"));
		return;
	}

	InitStreamer();
	InitPlayer();
}

void FPixelStreamingModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
		FSlateApplication::Get().GetRenderer()->OnPreResizeWindowBackBuffer().RemoveAll(this);
	}

	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);

	FPlayer::DestroyDXManagerAndDevice();
}

bool FPixelStreamingModule::CheckPlatformCompatibility() const
{
	bool bCompatible = true;

	bool bWin8OrHigher = FWindowsPlatformMisc::VerifyWindowsVersion(6, 2);
	if (!bWin8OrHigher)
	{
		FString ErrorString(TEXT("Failed to initialize Pixel Streaming plugin because minimum requirement is Windows 8"));
		FText ErrorText = FText::FromString(ErrorString);
		FText TitleText = FText::FromString(TEXT("Pixel Streaming Plugin"));
		FMessageDialog::Open(EAppMsgType::Ok, ErrorText, &TitleText);
		UE_LOG(PixelStreamer, Error, TEXT("%s"), *ErrorString);
		bCompatible = false;
	}
	else if (!FStreamer::CheckPlatformCompatibility())
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
	if (bCaptureNextBackBufferAndStream)
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
	InputDevice = MakeShareable(new FInputDevice(InMessageHandler, InputComponents));
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

void FPixelStreamingModule::FreezeFrame(UTexture2D* Texture)
{
	if (Texture)
	{
		// A frame is supplied so immediately read its data and send as a JPEG.
		FTexture2DRHIRef Texture2DRHI = Texture->Resource && Texture->Resource->TextureRHI ? Texture->Resource->TextureRHI->GetTexture2D() : nullptr;
		if (!Texture2DRHI)
		{
			UE_LOG(PixelStreamer, Error, TEXT("Attempting freeze frame with texture %s with no texture 2D RHI"), *Texture->GetName());
			return;
		}

		ENQUEUE_RENDER_COMMAND(ReadSurfaceCommand)([this, Texture2DRHI](FRHICommandListImmediate& RHICmdList)
		{
			TArray<FColor> Data;
			FIntRect Rect{ {0, 0}, Texture2DRHI->GetSizeXY() };
			RHICmdList.ReadSurfaceData(Texture2DRHI, Rect, Data, FReadSurfaceDataFlags());
			SendJpeg(MoveTemp(Data), Rect);
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
	if (FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingControlScheme="), PixelStreamingControlScheme))
	{
		JsonObject->SetStringField(TEXT("ControlScheme"), PixelStreamingControlScheme);
	}

	float PixelStreamingFastPan;
	if (FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingFastPan="), PixelStreamingFastPan))
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
	UWorld* NewPlayerWorld = NewPlayer->GetWorld();
	for (TObjectIterator<UPixelStreamerInputComponent> ObjIt; ObjIt; ++ObjIt)
	{
		UPixelStreamerInputComponent* InputComponent = *ObjIt;
		UWorld* InputComponentWorld = InputComponent->GetWorld();
		if (InputComponentWorld == NewPlayerWorld)
		{
			InputComponents.Push(InputComponent);
		}
	}
	if (InputComponents.Num() == 0)
	{
		UPixelStreamerInputComponent* InputComponent = NewObject<UPixelStreamerInputComponent>(NewPlayer);
		InputComponent->RegisterComponent();
		InputComponents.Push(InputComponent);
	}
	if (InputDevice.IsValid())
	{
		for (UPixelStreamerInputComponent* InputComponent : InputComponents)
		{
			InputDevice->AddInputComponent(InputComponent);
		}
	}
}

void FPixelStreamingModule::OnGameModeLogout(AGameModeBase* GameMode, AController* Exiting)
{
	for (UPixelStreamerInputComponent* InputComponent : InputComponents)
	{
		InputDevice->RemoveInputComponent(InputComponent);
	}
	InputComponents.Empty();
}

void FPixelStreamingModule::SendJpeg(TArray<FColor> RawData, const FIntRect& Rect)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
	bool bSuccess = ImageWrapper->SetRaw(RawData.GetData(), RawData.Num() * sizeof(FColor), Rect.Width(), Rect.Height(), ERGBFormat::BGRA, 8);
	if (bSuccess)
	{
		// Compress to a JPEG of the maximum possible quality.
		int32 Quality = CVarFreezeFrameQuality.GetValueOnAnyThread();
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
	if (SessionMonitorConnection)
	{
		HeartbeatCountdown -= DeltaTime;
		if (HeartbeatCountdown <= 0)
		{
			SessionMonitorConnection->Heartbeat();
			//  Hardcoded value for now
			HeartbeatCountdown = 2.0f;
		}
	}

	FHUDStats::Get().Tick();
}

TStatId FPixelStreamingModule::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FPixelStreamingModule, STATGROUP_Tickables);
}

TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> FPixelStreamingModule::CreatePlayer(IMediaEventSink& EventSink)
{
	return bPlayerInitialized ? MakeShareable(new FPlayer(EventSink)) : nullptr;
}

IMPLEMENT_MODULE(FPixelStreamingModule, PixelStreaming)
