// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingModule.h"
#include "Streamer.h"
#include "InputDevice.h"
#include "PixelStreamingInputComponent.h"
#include "PixelStreamingDelegates.h"
#include "SignallingServerConnection.h"
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

#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
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
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Async/Async.h"
#include "Engine/Engine.h"

#if !UE_BUILD_SHIPPING
	#include "DrawDebugHelpers.h"
#endif

DEFINE_LOG_CATEGORY(LogPixelStreaming);

IPixelStreamingModule* UE::PixelStreaming::FPixelStreamingModule::PixelStreamingModule = nullptr;

namespace
{

#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
	// required for WMF video decoding
	// some Windows versions don't have Media Foundation preinstalled. We configure MF DLLs as delay-loaded and load them manually here
	// checking the result and avoiding error message box if failed
	bool LoadMediaFoundationDLLs()
	{
		// Ensure that all required modules are preloaded so they are not loaded just-in-time, causing a hitch.
		if (IsWindows8OrGreater())
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
} // namespace

void UE::PixelStreaming::FPixelStreamingModule::InitStreamer()
{
	FString StreamerId;
	FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingID="), StreamerId);

	FString SignallingServerURL;
	if (!FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingURL="), SignallingServerURL))
	{

		FString SignallingServerIP;
		uint16 SignallingServerPort = 8888;
		if (!FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingIP="), SignallingServerIP) || !FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingPort="), SignallingServerPort))
		{
			UE_LOG(LogPixelStreaming, Log, TEXT("PixelStreaming is disabled, provide `PixelStreamingIP` and `PixelStreamingPort` cmd-args to enable it"));
			return;
		}
		UE_LOG(LogPixelStreaming, Warning, TEXT("PixelStreamingIP and PixelStreamingPort are deprecated flags. Use PixelStreamingURL instead. eg. -PixelStreamingURL=ws://%s:%d"), *SignallingServerIP, SignallingServerPort);
		SignallingServerURL = FString::Printf(TEXT("ws://%s:%d"), *SignallingServerIP, SignallingServerPort);
	}

	UE_LOG(LogPixelStreaming, Log, TEXT("PixelStreaming endpoint ID: %s"), *StreamerId);

	if (GIsEditor)
	{
		FText TitleText = FText::FromString(TEXT("Pixel Streaming Plugin"));
		FString ErrorString = TEXT("Pixel Streaming Plugin is not supported in editor, but it was explicitly enabled by command-line arguments. Please remove `PixelStreamingIP` and `PixelStreamingPort` args from editor command line.");
		FText ErrorText = FText::FromString(ErrorString);
		FMessageDialog::Open(EAppMsgType::Ok, ErrorText, &TitleText);
		UE_LOG(LogPixelStreaming, Error, TEXT("%s"), *ErrorString);
		return;
	}

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

	// subscribe to engine delegates here for init / framebuffer creation / whatever
	// TODO check if there is a better callback to attach so that we can use with editor
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddRaw(this, &UE::PixelStreaming::FPixelStreamingModule::OnBackBufferReady_RenderThread);
	}

	FGameModeEvents::GameModePostLoginEvent.AddRaw(this, &UE::PixelStreaming::FPixelStreamingModule::OnGameModePostLogin);
	FGameModeEvents::GameModeLogoutEvent.AddRaw(this, &UE::PixelStreaming::FPixelStreamingModule::OnGameModeLogout);

	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);

	FApp::SetUnfocusedVolumeMultiplier(1.0f);

	// Allow Pixel Streaming to broadcast to various delegates bound in the
	// application-specific blueprint.
	UPixelStreamingDelegates::CreateInstance();

	verify(FModuleManager::Get().LoadModule(FName("ImageWrapper")));

	Streamer = MakeUnique<UE::PixelStreaming::FStreamer>(SignallingServerURL, StreamerId);

	// Streamer has been created, so module is now "ready" for external use.
	ReadyEvent.Broadcast(*this);
}

/** IModuleInterface implementation */
void UE::PixelStreaming::FPixelStreamingModule::StartupModule()
{
	// Pixel Streaming does not make sense without an RHI so we don't run in commandlets without one.
	if (IsRunningCommandlet() && !IsAllowCommandletRendering())
	{
		return;
	}

	// Initialise all settings from command line args etc
	UE::PixelStreaming::Settings::InitialiseSettings();

	// only D3D11/D3D12 is supported
	if (GDynamicRHI == nullptr || !(GDynamicRHI->GetName() == FString(TEXT("D3D11")) || GDynamicRHI->GetName() == FString(TEXT("D3D12")) || GDynamicRHI->GetName() == FString(TEXT("Vulkan"))))
	{
		UE_LOG(LogPixelStreaming, Warning, TEXT("Only D3D11/D3D12/Vulkan Dynamic RHI is supported. Detected %s"), GDynamicRHI != nullptr ? GDynamicRHI->GetName() : TEXT("[null]"));
		return;
	}
	else if (GDynamicRHI->GetName() == FString(TEXT("D3D11")) || GDynamicRHI->GetName() == FString(TEXT("D3D12")) || GDynamicRHI->GetName() == FString(TEXT("Vulkan")))
	{
		// By calling InitStreamer post engine init we can use pixel streaming in standalone editor mode
		FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &UE::PixelStreaming::FPixelStreamingModule::InitStreamer);
	}
}

void UE::PixelStreaming::FPixelStreamingModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
		FSlateApplication::Get().GetRenderer()->OnPreResizeWindowBackBuffer().RemoveAll(this);
	}

	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);
}

IPixelStreamingModule* UE::PixelStreaming::FPixelStreamingModule::GetModule()
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

bool UE::PixelStreaming::FPixelStreamingModule::IsPlatformCompatible() const
{
	bool bCompatible = true;

#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
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

	if (!UE::PixelStreaming::FStreamer::IsPlatformCompatible())
	{
		FText TitleText = FText::FromString(TEXT("Pixel Streaming Plugin"));
		FString ErrorString = TEXT("No compatible GPU found, or failed to load their respective encoder libraries");
		FText ErrorText = FText::FromString(ErrorString);
		FMessageDialog::Open(EAppMsgType::Ok, ErrorText, &TitleText);
		UE_LOG(LogPixelStreaming, Error, TEXT("%s"), *ErrorString);
		bCompatible = false;
	}

	return bCompatible;
}

void UE::PixelStreaming::FPixelStreamingModule::UpdateViewport(FSceneViewport* Viewport)
{
	FRHIViewport* const ViewportRHI = Viewport->GetViewportRHI().GetReference();
}

void UE::PixelStreaming::FPixelStreamingModule::OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer)
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

TSharedPtr<class IInputDevice> UE::PixelStreaming::FPixelStreamingModule::CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	InputDevice = MakeShareable(new UE::PixelStreaming::FInputDevice(InMessageHandler));
	return InputDevice;
}

IPixelStreamingModule::FReadyEvent& UE::PixelStreaming::FPixelStreamingModule::OnReady()
{
	return ReadyEvent;
}

bool UE::PixelStreaming::FPixelStreamingModule::IsReady()
{
	return Streamer.IsValid();
}

IInputDevice& UE::PixelStreaming::FPixelStreamingModule::GetInputDevice()
{
	return *InputDevice;
}

TSharedPtr<UE::PixelStreaming::FInputDevice> UE::PixelStreaming::FPixelStreamingModule::GetInputDevicePtr()
{
	return InputDevice;
}

void UE::PixelStreaming::FPixelStreamingModule::AddInputComponent(UPixelStreamingInput* InInputComponent)
{
	InputComponents.Add(InInputComponent);
}

void UE::PixelStreaming::FPixelStreamingModule::RemoveInputComponent(UPixelStreamingInput* InInputComponent)
{
	InputComponents.Remove(InInputComponent);
}

const TArray<UPixelStreamingInput*> UE::PixelStreaming::FPixelStreamingModule::GetInputComponents()
{
	return InputComponents;
}

void UE::PixelStreaming::FPixelStreamingModule::FreezeFrame(UTexture2D* Texture)
{
	if (Texture)
	{
		ENQUEUE_RENDER_COMMAND(ReadSurfaceCommand)
		([this, Texture](FRHICommandListImmediate& RHICmdList) {
			// A frame is supplied so immediately read its data and send as a JPEG.
			FTexture2DRHIRef Texture2DRHI = (Texture->GetResource() && Texture->GetResource()->TextureRHI) ? Texture->GetResource()->TextureRHI->GetTexture2D() : nullptr;
			if (!Texture2DRHI)
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Attempting freeze frame with texture %s with no texture 2D RHI"), *Texture->GetName());
				return;
			}
			uint32 Width = Texture2DRHI->GetSizeX();
			uint32 Height = Texture2DRHI->GetSizeY();

			FTexture2DRHIRef DestTexture = UE::PixelStreaming::CreateTexture(Width, Height);

			FGPUFenceRHIRef CopyFence = GDynamicRHI->RHICreateGPUFence(*FString::Printf(TEXT("FreezeFrameFence")));

			// Copy freeze frame texture to empty texture
			UE::PixelStreaming::CopyTexture(Texture2DRHI, DestTexture, CopyFence);

			TArray<FColor> Data;
			FIntRect Rect(0, 0, Width, Height);
			RHICmdList.ReadSurfaceData(DestTexture, Rect, Data, FReadSurfaceDataFlags());
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

void UE::PixelStreaming::FPixelStreamingModule::UnfreezeFrame()
{
	if (!Streamer.IsValid())
	{
		return;
	}

	Streamer->SendUnfreezeFrame();

	// Resume streaming.
	bFrozen = false;
}

void UE::PixelStreaming::FPixelStreamingModule::KickPlayer(FPixelStreamingPlayerId PlayerId)
{
	if(Streamer)
	{
		Streamer->KickPlayer(PlayerId);
	}
}

void UE::PixelStreaming::FPixelStreamingModule::AddPlayerConfig(TSharedRef<FJsonObject>& JsonObject)
{
	checkf(InputDevice.IsValid(), TEXT("No Input Device available when populating Player Config"));

	JsonObject->SetBoolField(TEXT("FakingTouchEvents"), InputDevice->IsFakingTouchEvents());

	FString PixelStreamingControlScheme;
	if (UE::PixelStreaming::Settings::GetControlScheme(PixelStreamingControlScheme))
	{
		JsonObject->SetStringField(TEXT("ControlScheme"), PixelStreamingControlScheme);
	}

	float PixelStreamingFastPan;
	if (UE::PixelStreaming::Settings::GetFastPan(PixelStreamingFastPan))
	{
		JsonObject->SetNumberField(TEXT("FastPan"), PixelStreamingFastPan);
	}
}

void UE::PixelStreaming::FPixelStreamingModule::SendResponse(const FString& Descriptor)
{
	if (!Streamer.IsValid())
	{
		return;
	}

	Streamer->SendPlayerMessage(UE::PixelStreaming::Protocol::EToPlayerMsg::Response, Descriptor);
}

void UE::PixelStreaming::FPixelStreamingModule::SendCommand(const FString& Descriptor)
{
	if (!Streamer.IsValid())
	{
		return;
	}

	Streamer->SendPlayerMessage(UE::PixelStreaming::Protocol::EToPlayerMsg::Command, Descriptor);
}

void UE::PixelStreaming::FPixelStreamingModule::OnGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
}

void UE::PixelStreaming::FPixelStreamingModule::OnGameModeLogout(AGameModeBase* GameMode, AController* Exiting)
{
}

void UE::PixelStreaming::FPixelStreamingModule::SendJpeg(TArray<FColor> RawData, const FIntRect& Rect)
{
	if (!Streamer.IsValid())
	{
		return;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
	bool bSuccess = ImageWrapper->SetRaw(RawData.GetData(), RawData.Num() * sizeof(FColor), Rect.Width(), Rect.Height(), ERGBFormat::BGRA, 8);
	if (bSuccess)
	{
		// Compress to a JPEG of the maximum possible quality.
		int32 Quality = UE::PixelStreaming::Settings::CVarPixelStreamingFreezeFrameQuality.GetValueOnAnyThread();
		const TArray64<uint8>& JpegBytes = ImageWrapper->GetCompressed(Quality);
		Streamer->SendFreezeFrame(JpegBytes);
	}
	else
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("JPEG image wrapper failed to accept frame data"));
	}
}

void UE::PixelStreaming::FPixelStreamingModule::SendFileData(TArray<uint8>& ByteData, FString& MimeType, FString& FileExtension)
{
	Streamer->SendFileData(ByteData, MimeType, FileExtension);
}

bool UE::PixelStreaming::FPixelStreamingModule::IsTickableWhenPaused() const
{
	return true;
}

bool UE::PixelStreaming::FPixelStreamingModule::IsTickableInEditor() const
{
	return true;
}

void UE::PixelStreaming::FPixelStreamingModule::Tick(float DeltaTime)
{
}

TStatId UE::PixelStreaming::FPixelStreamingModule::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FPixelStreamingModule, STATGROUP_Tickables);
}

IPixelStreamingAudioSink* UE::PixelStreaming::FPixelStreamingModule::GetPeerAudioSink(FPixelStreamingPlayerId PlayerId)
{
	if (!Streamer.IsValid())
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Cannot get audio sink when streamer does not yet exist."));
		return nullptr;
	}

	return Streamer->GetAudioSink(PlayerId);
}

IPixelStreamingAudioSink* UE::PixelStreaming::FPixelStreamingModule::GetUnlistenedAudioSink()
{
	if (!Streamer.IsValid())
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Cannot get audio sink when streamer does not yet exist."));
		return nullptr;
	}

	return Streamer->GetUnlistenedAudioSink();
}

IMPLEMENT_MODULE(UE::PixelStreaming::FPixelStreamingModule, PixelStreaming)
