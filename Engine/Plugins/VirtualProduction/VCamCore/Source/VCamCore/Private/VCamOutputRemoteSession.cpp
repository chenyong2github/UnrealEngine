// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamOutputRemoteSession.h"
#include "Channels/RemoteSessionImageChannel.h"
#include "Channels/RemoteSessionInputChannel.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameEngine.h"
#include "Widgets/SVirtualWindow.h"

#if WITH_EDITOR
#include "IAssetViewport.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#endif

namespace VCamOutputRemoteSession
{
	static const FName LevelEditorName(TEXT("LevelEditor"));
}

void UVCamOutputRemoteSession::InitializeSafe()
{
	if (!bInitialized && (MediaOutput == nullptr))
	{
		MediaOutput = NewObject<URemoteSessionMediaOutput>(GetTransientPackage(), URemoteSessionMediaOutput::StaticClass());
	}

	Super::InitializeSafe();
}

void UVCamOutputRemoteSession::Destroy()
{
	MediaOutput = nullptr;

	Super::Destroy();
}

void UVCamOutputRemoteSession::Tick(const float DeltaTime)
{
	if (bIsActive && RemoteSessionHost.IsValid())
	{
		RemoteSessionHost->Tick(DeltaTime);
	}

	Super::Tick(DeltaTime);
}

void UVCamOutputRemoteSession::SetActive(const bool InActive)
{
	if (InActive)
	{
		CreateRemoteSession();
	}
	else
	{
		DestroyRemoteSession();
	}

	Super::SetActive(InActive);
}

void UVCamOutputRemoteSession::CreateUMG()
{
	Super::CreateUMG();
}

void UVCamOutputRemoteSession::CreateRemoteSession()
{
	if (!bInitialized)
	{
		UE_LOG(LogVCamOutputProvider, Warning, TEXT("CreateRemoteSession has been called, but has not been initialized yet"));
		return;
	}

	if (!RemoteSessionHost.IsValid())
	{
		if (IRemoteSessionModule* RemoteSession = FModuleManager::LoadModulePtr<IRemoteSessionModule>("RemoteSession"))
		{
			TArray<FRemoteSessionChannelInfo> SupportedChannels;
			SupportedChannels.Emplace(FRemoteSessionInputChannel::StaticType(), ERemoteSessionChannelMode::Read);
			SupportedChannels.Emplace(FRemoteSessionImageChannel::StaticType(), ERemoteSessionChannelMode::Write);

			RemoteSessionHost = RemoteSession->CreateHost(MoveTemp(SupportedChannels), PortNumber);

			RemoteSessionHost->RegisterChannelChangeDelegate(FOnRemoteSessionChannelChange::CreateUObject(this, &UVCamOutputRemoteSession::OnRemoteSessionChannelChange));
			if (RemoteSessionHost.IsValid())
			{
				RemoteSessionHost->Tick(0.0f);

#if WITH_EDITOR
				if (bUseOverrideResolution)
				{
					for (const FWorldContext& Context : GEngine->GetWorldContexts())
					{
						if (Context.WorldType == EWorldType::Editor)
						{
							if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(VCamOutputRemoteSession::LevelEditorName))
							{
								TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule->GetFirstActiveLevelViewport();
								if (ActiveLevelViewport.IsValid())
								{
									ActiveLevelViewport->GetSharedActiveViewport()->SetFixedViewportSize(OverrideResolution.X, OverrideResolution.Y);
								}
							}
						}
					}
				}
#endif
			}
		}
	}
	else
	{
		UE_LOG(LogVCamOutputProvider, Warning, TEXT("CreateRemoteSession has been called, but there is already a valid RemoteSessionHost!"));
	}
}

void UVCamOutputRemoteSession::DestroyRemoteSession()
{
	if (RemoteSessionHost.IsValid())
	{
		TSharedPtr<FRemoteSessionInputChannel> InputChannel = RemoteSessionHost->GetChannel<FRemoteSessionInputChannel>();
		if (InputChannel)
		{
			InputChannel->GetOnRouteTouchDownToWidgetFailedDelegate()->RemoveAll(this);
		}

		RemoteSessionHost->Close();
		RemoteSessionHost.Reset();
		if (MediaCapture)
		{
			MediaCapture->StopCapture(true);
			MediaCapture = nullptr;
		}

		// If everything is being shut down somehow, get out now
		if (GEngine == nullptr)
		{
			return;
		}

#if WITH_EDITOR
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor)
			{
				if (FLevelEditorViewportClient* LevelViewportClient = GetLevelViewportClient())
				{
					LevelViewportClient->ViewFOV = LevelViewportClient->FOVAngle;
					GEditor->RemovePerspectiveViewRotation(true, true, false);
				}

				if (bUseOverrideResolution)
				{
					if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(VCamOutputRemoteSession::LevelEditorName))
					{
						TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule->GetFirstActiveLevelViewport();
						if (ActiveLevelViewport.IsValid())
						{
							ActiveLevelViewport->GetSharedActiveViewport()->SetFixedViewportSize(0, 0);
						}
					}
				}
			}
		}
#endif
	}
}

void UVCamOutputRemoteSession::FindSceneViewport(TWeakPtr<SWindow>& OutInputWindow, TWeakPtr<FSceneViewport>& OutSceneViewport) const
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor)
			{
				if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(VCamOutputRemoteSession::LevelEditorName))
				{
					TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditorModule->GetFirstActiveViewport();
					if (ActiveLevelViewport.IsValid())
					{
						OutSceneViewport = ActiveLevelViewport->GetSharedActiveViewport();
						OutInputWindow = FSlateApplication::Get().FindWidgetWindow(ActiveLevelViewport->AsWidget());
					}
				}
			}
			else if (Context.WorldType == EWorldType::PIE)
			{
				FSlatePlayInEditorInfo* SlatePlayInEditorSession = GEditor->SlatePlayInEditorMap.Find(Context.ContextHandle);
				if (SlatePlayInEditorSession)
				{
					if (SlatePlayInEditorSession->DestinationSlateViewport.IsValid())
					{
						TSharedPtr<IAssetViewport> DestinationLevelViewport = SlatePlayInEditorSession->DestinationSlateViewport.Pin();
						OutSceneViewport = DestinationLevelViewport->GetSharedActiveViewport();
						OutInputWindow = FSlateApplication::Get().FindWidgetWindow(DestinationLevelViewport->AsWidget());
					}
					else if (SlatePlayInEditorSession->SlatePlayInEditorWindowViewport.IsValid())
					{
						OutSceneViewport = SlatePlayInEditorSession->SlatePlayInEditorWindowViewport;
						OutInputWindow = SlatePlayInEditorSession->SlatePlayInEditorWindow;
					}
				}
			}
		}
	}
	else
#endif
	{
		UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);
		OutSceneViewport = GameEngine->SceneViewport;
		OutInputWindow = GameEngine->GameViewportWindow;
	}
}

void UVCamOutputRemoteSession::OnRemoteSessionChannelChange(IRemoteSessionRole* Role, TWeakPtr<IRemoteSessionChannel> Channel, ERemoteSessionChannelChange Change)
{
	TSharedPtr<IRemoteSessionChannel> PinnedChannel = Channel.Pin();
	if (PinnedChannel && Change == ERemoteSessionChannelChange::Created)
	{
		if (PinnedChannel->GetType() == FRemoteSessionImageChannel::StaticType())
		{
			OnImageChannelCreated(Channel);
		}
		else if (PinnedChannel->GetType() == FRemoteSessionInputChannel::StaticType())
		{
			OnInputChannelCreated(Channel);
		}
	}
}

void UVCamOutputRemoteSession::OnImageChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance)
{
	TSharedPtr<FRemoteSessionImageChannel> ImageChannel = StaticCastSharedPtr<FRemoteSessionImageChannel>(Instance.Pin());
	if (ImageChannel)
	{
		ImageChannel->SetImageProvider(nullptr);
		MediaOutput->SetImageChannel(ImageChannel);
		MediaCapture = Cast<URemoteSessionMediaCapture>(MediaOutput->CreateMediaCapture());

		FMediaCaptureOptions Options;
		Options.bResizeSourceBuffer = true;

		if (bUseRenderTargetFromComposure)
		{
			if (ComposureRenderTarget)
			{
				MediaCapture->CaptureTextureRenderTarget2D(ComposureRenderTarget, Options);
				UE_LOG(LogVCamOutputProvider, Log, TEXT("ImageChannel callback - MediaCapture set with ComposureRenderTarget"));
			}
			else
			{
				UE_LOG(LogVCamOutputProvider, Warning, TEXT("ImageChannel callback - Composure usage was requested, but there is no ComposureRenderTarget set"));
			}
		}
		else
		{
			TWeakPtr<SWindow> InputWindow;
			TWeakPtr<FSceneViewport> SceneViewport;
			FindSceneViewport(InputWindow, SceneViewport);
			if (TSharedPtr<FSceneViewport> PinnedSceneViewport = SceneViewport.Pin())
			{
				MediaCapture->CaptureSceneViewport(PinnedSceneViewport, Options);
				UE_LOG(LogVCamOutputProvider, Log, TEXT("ImageChannel callback - MediaCapture set with viewport"));
			}
		}
	}
}

void UVCamOutputRemoteSession::OnInputChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance)
{
	TSharedPtr<FRemoteSessionInputChannel> InputChannel = StaticCastSharedPtr<FRemoteSessionInputChannel>(Instance.Pin());
	if (InputChannel)
	{
		// If we have a UMG, then use it
		if (UMGClass && UMGWidget)
		{
			TSharedPtr<SVirtualWindow> InputWindow = UMGWidget->PostProcessDisplayType.GetSlateWindow();
			InputChannel->SetPlaybackWindow(InputWindow, nullptr);
			InputChannel->TryRouteTouchMessageToWidget(true);
			InputChannel->GetOnRouteTouchDownToWidgetFailedDelegate()->AddUObject(this, &UVCamOutputRemoteSession::OnTouchEventOutsideUMG);

			UE_LOG(LogVCamOutputProvider, Log, TEXT("InputChannel callback - Routing input to active viewport with UMG"));
		}
		else
		{
			TWeakPtr<SWindow> InputWindow;
			TWeakPtr<FSceneViewport> SceneViewport;
			FindSceneViewport(InputWindow, SceneViewport);
			InputChannel->SetPlaybackWindow(InputWindow, nullptr);
			InputChannel->TryRouteTouchMessageToWidget(true);
			InputChannel->GetOnRouteTouchDownToWidgetFailedDelegate()->AddUObject(this, &UVCamOutputRemoteSession::OnTouchEventOutsideUMG);

			UE_LOG(LogVCamOutputProvider, Log, TEXT("InputChannel callback - Routing input to active viewport"));
		}
	}
}

void UVCamOutputRemoteSession::OnTouchEventOutsideUMG(const FVector2D& InViewportPosition)
{
	const float MaxFocusTraceDistance = 1000000.0f;
	FVector TraceDirection;
	FVector CameraWorldLocation;

	if (!DeprojectScreenToWorld(InViewportPosition, CameraWorldLocation, TraceDirection))
	{
		return;
	}

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (World)
		{
			FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(UpdateAutoFocus), true);
			const FVector TraceEnd = CameraWorldLocation + TraceDirection * MaxFocusTraceDistance;
			const bool bHit = World->LineTraceSingleByChannel(LastViewportTouchResult, CameraWorldLocation, TraceEnd, ECC_Visibility, TraceParams);
			if (bHit)
			{
#if WITH_EDITOR
				// @todo: This doesn't seem like the most efficient way to past click events to the editor viewport...
				if (Context.WorldType == EWorldType::Editor)
				{
					if (FLevelEditorViewportClient* LevelViewportClient = GetLevelViewportClient())
					{
						if (FViewport* ActiveViewport = GetActiveViewport())
						{
							FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
								ActiveViewport,
								LevelViewportClient->GetScene(),
								LevelViewportClient->EngineShowFlags)
								.SetRealtimeUpdate(true));
							FSceneView* View = LevelViewportClient->CalcSceneView(&ViewFamily);

							uint32 HitX = InViewportPosition.X;
							uint32 HitY = InViewportPosition.Y;
							HHitProxy* HitProxy = ActiveViewport->GetHitProxy(HitX, HitY);
							LevelViewportClient->ProcessClick(*View, HitProxy, EKeys::LeftMouseButton, EInputEvent::IE_Pressed, HitX, HitY);
						}
					}
				}
				else
#endif
				{
					LastViewportTouchResult.GetActor()->NotifyActorOnClicked();
				}
			}
		}
	}
}

bool UVCamOutputRemoteSession::DeprojectScreenToWorld(const FVector2D& InScreenPosition, FVector& OutWorldPosition, FVector& OutWorldDirection) const
{
	bool bSuccess = false;

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
		{
			APlayerController* PC = Context.OwningGameInstance->GetFirstLocalPlayerController(Context.World());
			if (PC)
			{
				bSuccess |= PC->DeprojectScreenPositionToWorld(InScreenPosition.X, InScreenPosition.Y, OutWorldPosition, OutWorldDirection);
				break;
			}
		}
#if WITH_EDITOR
		else if (Context.WorldType == EWorldType::Editor)
		{
			if (FLevelEditorViewportClient* LevelViewportClient = GetLevelViewportClient())
			{
				if (FViewport* ActiveViewport = GetActiveViewport())
				{
					FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
						ActiveViewport,
						LevelViewportClient->GetScene(),
						LevelViewportClient->EngineShowFlags)
						.SetRealtimeUpdate(true));
					FSceneView* View = LevelViewportClient->CalcSceneView(&ViewFamily);

					const FIntPoint ViewportSize = ActiveViewport->GetSizeXY();
					const FIntRect ViewRect = FIntRect(0, 0, ViewportSize.X, ViewportSize.Y);
					const FMatrix InvViewProjectionMatrix = View->ViewMatrices.GetInvViewProjectionMatrix();
					FSceneView::DeprojectScreenToWorld(InScreenPosition, ViewRect, InvViewProjectionMatrix, OutWorldPosition, OutWorldDirection);
					bSuccess = true;
				}
			}
		}
#endif
	}

	if (!bSuccess)
	{
		OutWorldPosition = FVector::ZeroVector;
		OutWorldDirection = FVector::ZeroVector;
	}
	return bSuccess;
}

#if WITH_EDITOR
FLevelEditorViewportClient* UVCamOutputRemoteSession::GetLevelViewportClient() const
{
	FLevelEditorViewportClient* OutClient = nullptr;
	if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(VCamOutputRemoteSession::LevelEditorName))
	{
		TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule->GetFirstActiveLevelViewport();
		if (ActiveLevelViewport.IsValid())
		{
			OutClient = &ActiveLevelViewport->GetLevelViewportClient();
		}
	}

	return OutClient;
}

FViewport* UVCamOutputRemoteSession::GetActiveViewport() const
{
	FViewport* OutViewport = nullptr;
	if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(VCamOutputRemoteSession::LevelEditorName))
	{
		TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditorModule->GetFirstActiveViewport();
		if (ActiveLevelViewport.IsValid())
		{
			OutViewport = ActiveLevelViewport->GetActiveViewport();
		}
	}

	return OutViewport;
}

void UVCamOutputRemoteSession::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* Property = PropertyChangedEvent.MemberProperty;

	if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		static FName NAME_PortNumber = GET_MEMBER_NAME_CHECKED(UVCamOutputRemoteSession, PortNumber);

		if (Property->GetFName() == NAME_PortNumber)
		{
			if (bIsActive)
			{
				SetActive(false);
				SetActive(true);
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
