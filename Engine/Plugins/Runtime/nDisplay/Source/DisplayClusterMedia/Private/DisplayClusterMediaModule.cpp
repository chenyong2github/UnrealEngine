// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterMediaModule.h"
#include "DisplayClusterMediaLog.h"
#include "DisplayClusterMediaHelpers.h"

#include "IDisplayCluster.h"
#include "IDisplayClusterCallbacks.h"
#include "DisplayClusterEnums.h"
#include "DisplayClusterRootActor.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Components/DisplayClusterICVFXCameraComponent.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "Game/IDisplayClusterGameManager.h"

#include "Capture/DisplayClusterMediaCaptureCamera.h"
#include "Capture/DisplayClusterMediaCaptureNode.h"
#include "Capture/DisplayClusterMediaCaptureViewport.h"
#include "Input/DisplayClusterMediaInputNode.h"
#include "Input/DisplayClusterMediaInputViewport.h"
#include "Input/SharedMemoryMediaPlayerFactory.h"

#include "IMediaModule.h"
#include "Misc/CoreDelegates.h"


void FDisplayClusterMediaModule::StartupModule()
{
	UE_LOG(LogDisplayClusterMedia, Log, TEXT("Starting module 'DisplayClusterMedia'..."));

	IDisplayCluster::Get().GetCallbacks().OnDisplayClusterCustomPresentSet().AddRaw(this, &FDisplayClusterMediaModule::OnCustomPresentSet);
	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FDisplayClusterMediaModule::OnEnginePreExit);

	// Create Player Factories
	PlayerFactories.Add(MakeUnique<FSharedMemoryMediaPlayerFactory>());


	// register share memory player factory
	if (IMediaModule* MediaModule = FModuleManager::LoadModulePtr<IMediaModule>("Media"))
	{
		for (TUniquePtr<IMediaPlayerFactory>& Factory : PlayerFactories)
		{
			MediaModule->RegisterPlayerFactory(*Factory);
		}
	}
}

void FDisplayClusterMediaModule::ShutdownModule()
{
	UE_LOG(LogDisplayClusterMedia, Log, TEXT("Shutting down module 'DisplayClusterMedia'..."));

	// unregister share memory player factory
	if (IMediaModule* MediaModule = FModuleManager::LoadModulePtr<IMediaModule>("Media"))
	{
		for (TUniquePtr<IMediaPlayerFactory>& Factory : PlayerFactories)
		{
			MediaModule->UnregisterPlayerFactory(*Factory);
		}
	}

	IDisplayCluster::Get().GetCallbacks().OnDisplayClusterCustomPresentSet().RemoveAll(this);
	FCoreDelegates::OnEnginePreExit.RemoveAll(this);
}

void FDisplayClusterMediaModule::OnCustomPresentSet()
{
	// We initialize and start media when backbuffer is available. This CustomPresentSet event is a
	// sign that the backbuffer is already available. This allows us to prevent any potential problems
	// if any of the following is used under the hood, or will be used in the future:
	// - UMediaCapture::CaptureActiveSceneViewport
	// - UMediaCapture::CaptureSceneViewport
	InitializeMedia();
	StartCapture();
	PlayMedia();
}

void FDisplayClusterMediaModule::OnEnginePreExit()
{
	ReleaseMedia();
}

void FDisplayClusterMediaModule::InitializeMedia()
{
	// Runtime only for now
	if (IDisplayCluster::Get().GetOperationMode() != EDisplayClusterOperationMode::Cluster)
	{
		UE_LOG(LogDisplayClusterMedia, Warning, TEXT("DisplayClusterMedia is available in 'cluster' operation mode only"));
		return;
	}

	// Instantiate latency queue
	FrameQueue.Init();

	// Parse DCRA configuration and initialize media
	if (const ADisplayClusterRootActor* const RootActor = IDisplayCluster::Get().GetGameMgr()->GetRootActor())
	{
		const FString ClusterNodeId = IDisplayCluster::Get().GetClusterMgr()->GetNodeId();
		if (const UDisplayClusterConfigurationClusterNode* const ClusterNode = RootActor->GetConfigData()->Cluster->GetNode(ClusterNodeId))
		{
			///////////////////////////////
			// Node backbuffer media setup
			{
				const FDisplayClusterConfigurationMedia& MediaSettings = ClusterNode->Media;

				if (MediaSettings.bEnable)
				{
					// Media input
					if (MediaSettings.MediaSource)
					{
						const FString MediaInputId = DisplayClusterMediaHelpers::MediaId::GenerateMediaId(
							DisplayClusterMediaHelpers::MediaId::EMediaDeviceType::Input,
							DisplayClusterMediaHelpers::MediaId::EMediaOwnerType::Backbuffer,
							*ClusterNodeId, *RootActor->GetName(), FString());

						UE_LOG(LogDisplayClusterMedia, Log, TEXT("Initializing backbuffer media input '%s'..."), *MediaInputId);

						InputNode = MakeShared<FDisplayClusterMediaInputNode>(
							MediaInputId, ClusterNodeId,
							MediaSettings.MediaSource);
					}

					// Media capture
					if (MediaSettings.MediaOutput)
					{
						const FString MediaCaptureId = DisplayClusterMediaHelpers::MediaId::GenerateMediaId(
							DisplayClusterMediaHelpers::MediaId::EMediaDeviceType::Output,
							DisplayClusterMediaHelpers::MediaId::EMediaOwnerType::Backbuffer,
							*ClusterNodeId, *RootActor->GetName(), FString());

						UE_LOG(LogDisplayClusterMedia, Log, TEXT("Initializing backbuffer media capture '%s'..."), *MediaCaptureId);

						CaptureNode = MakeShared<FDisplayClusterMediaCaptureNode>(
							MediaCaptureId, ClusterNodeId,
							MediaSettings.MediaOutput,
							MediaSettings.OutputSyncPolicy);
					}
				}
			}

			///////////////////////////////
			// Viewports media setup
			for (const TPair<FString, TObjectPtr<UDisplayClusterConfigurationViewport>>& ViewportIt : ClusterNode->Viewports)
			{
				if (const UDisplayClusterConfigurationViewport* const Viewport = ViewportIt.Value)
				{
					const FDisplayClusterConfigurationMedia& MediaSettings = Viewport->RenderSettings.Media;

					if (MediaSettings.bEnable)
					{
						// Media input
						if (MediaSettings.MediaSource)
						{
							const FString MediaInputId = DisplayClusterMediaHelpers::MediaId::GenerateMediaId(
								DisplayClusterMediaHelpers::MediaId::EMediaDeviceType::Input,
								DisplayClusterMediaHelpers::MediaId::EMediaOwnerType::Viewport,
								*ClusterNodeId, *RootActor->GetName(), ViewportIt.Key);

							UE_LOG(LogDisplayClusterMedia, Log, TEXT("Initializing viewport media input '%s' for viewport '%s'..."), *MediaInputId, *ViewportIt.Key);

							TSharedPtr<FDisplayClusterMediaInputViewport> NewViewportInput = MakeShared<FDisplayClusterMediaInputViewport>(
								MediaInputId, ClusterNodeId,
								ViewportIt.Key,
								MediaSettings.MediaSource);

							InputViewports.Emplace(MediaInputId, MoveTemp(NewViewportInput));
						}

						// Media capture
						if (MediaSettings.MediaOutput)
						{
							const FString MediaCaptureId = DisplayClusterMediaHelpers::MediaId::GenerateMediaId(
								DisplayClusterMediaHelpers::MediaId::EMediaDeviceType::Output,
								DisplayClusterMediaHelpers::MediaId::EMediaOwnerType::Viewport,
								*ClusterNodeId, *RootActor->GetName(), ViewportIt.Key);

							UE_LOG(LogDisplayClusterMedia, Log, TEXT("Initializing viewport capture '%s' for viewport '%s'..."), *MediaCaptureId, *ViewportIt.Key);

							TSharedPtr<FDisplayClusterMediaCaptureViewport> NewViewportCapture = MakeShared<FDisplayClusterMediaCaptureViewport>(
								MediaCaptureId, ClusterNodeId,
								ViewportIt.Key,
								MediaSettings.MediaOutput,
								MediaSettings.OutputSyncPolicy);

							CaptureViewports.Emplace(MediaCaptureId, MoveTemp(NewViewportCapture));
						}
					}
				}
			}

			///////////////////////////////
			// ICVFX media setup
			{
				// Get all ICVFX camera components
				TArray<UDisplayClusterICVFXCameraComponent*> ICVFXCameraComponents;
				RootActor->GetComponents(ICVFXCameraComponents);

				for (const UDisplayClusterICVFXCameraComponent* const ICVFXCameraComponent : ICVFXCameraComponents)
				{
					const FDisplayClusterConfigurationMediaICVFX& MediaSettings = ICVFXCameraComponent->CameraSettings.RenderSettings.Media;

					if (MediaSettings.bEnable)
					{
						const FString ICVFXCameraName = ICVFXCameraComponent->GetName();
						const FString ICVFXViewportId = DisplayClusterMediaHelpers::GenerateICVFXViewportName(ClusterNodeId, ICVFXCameraName);

						// Media input only
						if (UMediaSource* MediaSource = MediaSettings.GetMediaSource(ClusterNodeId))
						{
							const FString MediaInputId = DisplayClusterMediaHelpers::MediaId::GenerateMediaId(
								DisplayClusterMediaHelpers::MediaId::EMediaDeviceType::Input,
								DisplayClusterMediaHelpers::MediaId::EMediaOwnerType::ICVFXCamera,
								*ClusterNodeId, *RootActor->GetName(), ICVFXCameraName);

							UE_LOG(LogDisplayClusterMedia, Log, TEXT("Initializing ICVFX media input '%s' for camera '%s'..."), *MediaInputId, *ICVFXCameraName);

							TSharedPtr<FDisplayClusterMediaInputViewport> NewICVFXInput = MakeShared<FDisplayClusterMediaInputViewport>(
								MediaInputId, ClusterNodeId,
								ICVFXViewportId,
								MediaSource);

							InputViewports.Emplace(MediaInputId, MoveTemp(NewICVFXInput));
						}

						// Media capture only
						if (UMediaOutput* MediaOutput = MediaSettings.GetMediaOutput(ClusterNodeId))
						{
							const FString MediaCaptureId = DisplayClusterMediaHelpers::MediaId::GenerateMediaId(
								DisplayClusterMediaHelpers::MediaId::EMediaDeviceType::Output,
								DisplayClusterMediaHelpers::MediaId::EMediaOwnerType::ICVFXCamera,
								*ClusterNodeId, *RootActor->GetName(), ICVFXCameraName);

							UE_LOG(LogDisplayClusterMedia, Log, TEXT("Initializing ICVFX capture '%s' for camera '%s'..."), *MediaCaptureId, *ICVFXCameraName);

							TSharedPtr<FDisplayClusterMediaCaptureViewport> NewICVFXCapture = MakeShared<FDisplayClusterMediaCaptureCamera>(
								MediaCaptureId, ClusterNodeId,
								ICVFXCameraName, ICVFXViewportId,
								MediaOutput,
								MediaSettings.GetOutputSyncPolicy(ClusterNodeId));

							CaptureViewports.Emplace(MediaCaptureId, MoveTemp(NewICVFXCapture));
						}
					}
				}
			}
		}
	}
}

void FDisplayClusterMediaModule::ReleaseMedia()
{
	StopCapture();
	StopMedia();

	CaptureViewports.Reset();
	CaptureNode.Reset();

	InputViewports.Reset();
	InputNode.Reset();

	FrameQueue.Release();
}

void FDisplayClusterMediaModule::StartCapture()
{
	// Start viewports capture
	for (TPair<FString, TSharedPtr<FDisplayClusterMediaCaptureViewport>>& Capture : CaptureViewports)
	{
		Capture.Value->StartCapture();
	}

	// Start backbuffer capture
	if (CaptureNode)
	{
		CaptureNode->StartCapture();
	}
}

void FDisplayClusterMediaModule::StopCapture()
{
	// Stop viewports capture
	for (TPair<FString, TSharedPtr<FDisplayClusterMediaCaptureViewport>>& Capture : CaptureViewports)
	{
		Capture.Value->StopCapture();
	}

	// Stop backbuffer capture
	if (CaptureNode)
	{
		CaptureNode->StopCapture();
	}
}

void FDisplayClusterMediaModule::PlayMedia()
{
	// Start playback on viewports
	for (TPair<FString, TSharedPtr<FDisplayClusterMediaInputViewport>>& MediaInput : InputViewports)
	{
		MediaInput.Value->Play();
	}

	// Start playback to the backbuffer
	if (InputNode)
	{
		InputNode->Play();
	}
}

void FDisplayClusterMediaModule::StopMedia()
{
	// Stop playback on viewports
	for (TPair<FString, TSharedPtr<FDisplayClusterMediaInputViewport>>& MediaInput : InputViewports)
	{
		MediaInput.Value->Stop();
	}

	// Stop playback to the backbuffer
	if (InputNode)
	{
		InputNode->Stop();
	}
}


IMPLEMENT_MODULE(FDisplayClusterMediaModule, DisplayClusterMedia);
