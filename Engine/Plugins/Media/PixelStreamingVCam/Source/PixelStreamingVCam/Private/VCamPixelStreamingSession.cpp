// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamPixelStreamingSession.h"
#include "VCamOutputComposure.h"
#include "PixelStreamingServers.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Containers/UnrealString.h"
#include "Async/Async.h"
#include "Modules/ModuleManager.h"
#include "Math/Vector4.h"
#include "Math/Matrix.h"
#include "Math/TransformNonVectorized.h"
#include "Serialization/MemoryReader.h"
#include "VCamPixelStreamingSubsystem.h"
#include "VCamPixelStreamingLiveLink.h"

DEFINE_LOG_CATEGORY(LogVCamOutputProvider);
namespace VCamPixelStreamingSession
{
	static const FName LevelEditorName(TEXT("LevelEditor"));
	static const FSoftClassPath EmptyUMGSoftClassPath(TEXT("/VCamCore/Assets/VCam_EmptyVisibleUMG.VCam_EmptyVisibleUMG_C"));
} // namespace VCamPixelStreamingSession

void UVCamPixelStreamingSession::Initialize()
{
	if (!bInitialized && (MediaOutput == nullptr))
	{
		MediaOutput = NewObject<UPixelStreamingMediaOutput>(GetTransientPackage(), UPixelStreamingMediaOutput::StaticClass());
	}
	Super::Initialize();
}

void UVCamPixelStreamingSession::Deinitialize()
{
	if (MediaOutput)
	{
		MediaOutput->ConditionalBeginDestroy();
	}
	MediaOutput = nullptr;
	Super::Deinitialize();
}

void UVCamPixelStreamingSession::Activate()
{
	if (UVCamPixelStreamingSubsystem* PixelStreamingSubsystem = UVCamPixelStreamingSubsystem::Get())
	{
		PixelStreamingSubsystem->RegisterActiveOutputProvider(this);
	}

	// If we don't have a UMG assigned, we still need to create an empty 'dummy' UMG in order to properly route the input back from the RemoteSession device
	if (UMGClass == nullptr)
	{
		bUsingDummyUMG = true;
		UMGClass = VCamPixelStreamingSession::EmptyUMGSoftClassPath.TryLoadClass<UUserWidget>();
	}

	if (!bInitialized)
	{
		UE_LOG(LogVCamOutputProvider, Warning, TEXT("Trying to start Pixel Streaming, but has not been initialized yet"));
		SetActive(false);
		return;
	}

	UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
	bOldThrottleCPUWhenNotForeground = Settings->bThrottleCPUWhenNotForeground;
	if (PreventEditorIdle)
	{
		Settings->bThrottleCPUWhenNotForeground = false;
		Settings->PostEditChange();
	}

	if (StartSignallingServer)
	{
		CirrusProcess = UE::PixelStreaming::Servers::MakeSignallingServer();
		UE::PixelStreaming::Servers::FLaunchArgs LaunchArgs;
		LaunchArgs.bEphemeral = false;
		LaunchArgs.ProcessArgs = FString::Printf(TEXT("--StreamerPort=%s --HttpPort=%s"), *FString::FromInt(PortNumber), *FString::FromInt(HttpPort));
		CirrusProcess->Launch(LaunchArgs);
	}

	MediaOutput->SetSignallingServerURL(FString::Printf(TEXT("%s:%s"), *IP, *FString::FromInt(PortNumber)));
	UE_LOG(LogVCamOutputProvider, Log, TEXT("Activating PixelStreaming VCam Session. Endpoint: %s:%s"), *IP, *FString::FromInt(PortNumber));

	MediaCapture = Cast<UPixelStreamingMediaCapture>(MediaOutput->CreateMediaCapture());

	FMediaCaptureOptions Options;
	Options.bResizeSourceBuffer = true;

	// If we are rendering from a ComposureOutputProvider, get the requested render target and use that instead of the viewport
	if (UVCamOutputComposure* ComposureProvider = Cast<UVCamOutputComposure>(GetOtherOutputProviderByIndex(FromComposureOutputProviderIndex)))
	{
		if (ComposureProvider->FinalOutputRenderTarget)
		{
			MediaCapture->CaptureTextureRenderTarget2D(ComposureProvider->FinalOutputRenderTarget, Options);
			UE_LOG(LogVCamOutputProvider, Log, TEXT("PixelStreaming set with ComposureRenderTarget"));
		}
		else
		{
			UE_LOG(LogVCamOutputProvider, Warning, TEXT("PixelStreaming Composure usage was requested, but the specified ComposureOutputProvider has no FinalOutputRenderTarget set"));
		}
	}
	else
	{
		TWeakPtr<FSceneViewport> SceneViewport = GetTargetSceneViewport();
		if (TSharedPtr<FSceneViewport> PinnedSceneViewport = SceneViewport.Pin())
		{
			MediaCapture->CaptureSceneViewport(PinnedSceneViewport, Options);
			UE_LOG(LogVCamOutputProvider, Log, TEXT("PixelStreaming set with viewport"));
		}
	}

	if(MediaOutput->GetStreamer())
	{
		MediaOutput->GetStreamer()->OnInputReceived.AddLambda([this](FPixelStreamingPlayerId PlayerId, uint8 Type, TArray<uint8> Data)
		{
			if(Type != static_cast<uint8>(UE::PixelStreaming::Protocol::EToStreamerMsg::ARKitTransform) || !EnableARKitTracking)
			{
				return;
			}

			// 1 byte for type, 16 floats for transform, 1 double for timestamp
			static constexpr int ExpectedDataSize = 1 + (16 * 4) + 8;
			if (Data.Num() != ExpectedDataSize)
			{
				UE_LOG(LogVCamOutputProvider, Warning, TEXT("ARKitTransform data received but with invalid size. Expects %d bytes but received %d"),
					ExpectedDataSize,
					Data.Num());

				return;
			}
			
			FMemoryReader Buffer(Data);
			
			uint8 MessageType;
			Buffer << MessageType;

			// The buffer contains the transform matrix stored as 16 floats
			FMatrix ARKitMatrix;
			for (int32 Row = 0; Row < 4; ++Row)
			{
				float Col0, Col1, Col2, Col3;
				Buffer << Col0 << Col1 << Col2 << Col3;
				ARKitMatrix.M[Row][0] = Col0;
				ARKitMatrix.M[Row][1] = Col1;
				ARKitMatrix.M[Row][2] = Col2;
				ARKitMatrix.M[Row][3] = Col3;
			}
			ARKitMatrix.DiagnosticCheckNaN();

			// 
			double Timestamp;
			Buffer << Timestamp;

			if(TSharedPtr<FPixelStreamingLiveLinkSource> LiveLinkSource = UVCamPixelStreamingSubsystem::Get()->LiveLinkSource)
			{
				LiveLinkSource->PushTransformForSubject(GetFName(), FTransform(ARKitMatrix));
			}

			/**
			 * Code to control the level editor viewport
			 */
			// for (FLevelEditorViewportClient* LevelVC : GEditor->GetLevelViewportClients())
			// {
			// 	if (LevelVC && LevelVC->IsPerspective())
			// 	{
			// 		LevelVC->SetViewLocation(Translation);
			// 		LevelVC->SetViewRotation(ModifiedRotation);
			// 	}
			// }
		});
	}
	Super::Activate();
}

void UVCamPixelStreamingSession::StopSignallingServer()
{
	if (CirrusProcess.IsValid())
	{
		CirrusProcess->Stop();
	}
}

void UVCamPixelStreamingSession::Deactivate()
{
	if (UVCamPixelStreamingSubsystem* PixelStreamingSubsystem = UVCamPixelStreamingSubsystem::Get())
	{
		PixelStreamingSubsystem->UnregisterActiveOutputProvider(this);
	}

	if (MediaCapture)
	{

		if (MediaOutput && MediaOutput->GetStreamer())
		{
			// Shutting streamer down before closing signalling server prevents an ugly websocket disconnect showing in the log
			MediaOutput->GetStreamer()->StopStreaming();
			StopSignallingServer();
		}

		MediaCapture->StopCapture(true);
		MediaCapture = nullptr;
	}
	else
	{
		// There is not media capture we defensively clean up the signalling server if it exists.
		StopSignallingServer();
	}

	Super::Deactivate();
	if (bUsingDummyUMG)
	{
		UMGClass = nullptr;
		bUsingDummyUMG = false;
	}

	UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
	Settings->bThrottleCPUWhenNotForeground = bOldThrottleCPUWhenNotForeground;
	Settings->PostEditChange();
}

void UVCamPixelStreamingSession::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
}

#if WITH_EDITOR
void UVCamPixelStreamingSession::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* Property = PropertyChangedEvent.MemberProperty;
	if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		static FName NAME_IP = GET_MEMBER_NAME_CHECKED(UVCamPixelStreamingSession, IP);
		static FName NAME_PortNumber = GET_MEMBER_NAME_CHECKED(UVCamPixelStreamingSession, PortNumber);
		static FName NAME_FromComposureOutputProviderIndex = GET_MEMBER_NAME_CHECKED(UVCamPixelStreamingSession, FromComposureOutputProviderIndex);
		if ((Property->GetFName() == NAME_IP) || (Property->GetFName() == NAME_PortNumber) || (Property->GetFName() == NAME_FromComposureOutputProviderIndex))
		{
			SetActive(!bIsActive);
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

IMPLEMENT_MODULE(FDefaultModuleImpl, PixelStreamingVCam)
