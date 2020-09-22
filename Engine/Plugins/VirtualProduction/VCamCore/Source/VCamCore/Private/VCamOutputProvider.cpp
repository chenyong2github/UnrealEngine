// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamOutputProvider.h"

#include "Channels/RemoteSessionImageChannel.h"
#include "Channels/RemoteSessionInputChannel.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameEngine.h"
#include "Widgets/SVirtualWindow.h"

#if WITH_EDITOR
#include "IAssetViewport.h"
#include "LevelEditor.h"
#include "Blueprint/UserWidget.h"
#include "SLevelViewport.h"
#endif

DEFINE_LOG_CATEGORY(LogVCamOutputProvider);

static const FName LevelEditorName(TEXT("LevelEditor"));

UVCamOutputProvider::UVCamOutputProvider()
{
	bIsActive = false;
}

UVCamOutputProvider::~UVCamOutputProvider()
{
	Destroy();
}

void UVCamOutputProvider::Initialize()
{
	MediaOutput = NewObject<URemoteSessionMediaOutput>(GetTransientPackage(), URemoteSessionMediaOutput::StaticClass());

	UMGWidget = NewObject<UVPFullScreenUserWidget>(GetTransientPackage(), UVPFullScreenUserWidget::StaticClass());
	UMGWidget->SetDisplayTypes(EVPWidgetDisplayType::PostProcess, EVPWidgetDisplayType::Viewport, EVPWidgetDisplayType::PostProcess);
	UMGWidget->PostProcessDisplayType.bReceiveHardwareInput = true;
}

void UVCamOutputProvider::Destroy()
{
	DestroyRemoteSession();
	MediaOutput = nullptr;

	if (UMGWidget && UMGWidget->IsDisplayed())
	{
		UMGWidget->Hide();
	}
}

void UVCamOutputProvider::Tick(const float DeltaTime)
{
	if (bIsActive && RemoteSessionHost.IsValid())
	{
		RemoteSessionHost->Tick(DeltaTime);
	}

	if (bIsActive && UMGWidget && UMGClass)
	{
		UMGWidget->Tick(DeltaTime);
	}
}

void UVCamOutputProvider::SetActive(const bool InActive)
{
	bIsActive = InActive;

	if (bIsActive)
	{
		CreateRemoteSession();
		CreateUMG();
	}
	else
	{
		DestroyRemoteSession();

		if (UMGWidget && UMGWidget->IsDisplayed())
		{
			UMGWidget->Hide();
		}
	}
}

void UVCamOutputProvider::SetTargetCamera(const UCineCameraComponent* InTargetCamera)
{
	TargetCamera = InTargetCamera;
}

void UVCamOutputProvider::SetUMGClass(const TSubclassOf<UUserWidget> InUMGClass)
{
	UMGClass = InUMGClass;
}

void UVCamOutputProvider::CreateUMG()
{
	if (!UMGWidget || !UMGClass)
	{
		UE_LOG(LogVCamOutputProvider, Warning, TEXT("CreateUMG widget or class not set - failed to create"));
		return;
	}

	UMGWidget->WidgetClass = UMGClass;
	UWorld* ActorWorld = nullptr;
	int32 WorldType = -1;

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		ActorWorld = Context.World();
		WorldType = (int32)Context.WorldType;
		if (ActorWorld)
		{
			// Prioritize PIE and Game modes if active
			if ((Context.WorldType == EWorldType::PIE) || (Context.WorldType == EWorldType::Game))
			{
				break;
			}
			else if (Context.WorldType == EWorldType::Editor)
			{
				// Only grab the Editor world if PIE and Game aren't available
			}
			else
			{
				// We don't want any of the other world types
				ActorWorld = nullptr;
			}
		}
	}

	if (ActorWorld)
	{
		if (WorldType > 3)	// EWorldType::PIE
		{
			UE_LOG(LogVCamOutputProvider, Log, TEXT("CreateUMG widget tried to use WorldType %d, which should never happen - report as a bug!!"), WorldType);
			return;
		}

		UMGWidget->Display(ActorWorld);
		UE_LOG(LogVCamOutputProvider, Log, TEXT("CreateUMG widget named %s from class %s in WorldType %d"), *UMGWidget->GetName(), *UMGWidget->WidgetClass->GetName(), WorldType);
	}
}

void UVCamOutputProvider::CreateRemoteSession()
{
	if (!RemoteSessionHost.IsValid())
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
#if WITH_EDITOR
			if (Context.WorldType == EWorldType::Editor)
			{
				FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorName);
				TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
				if (ActiveLevelViewport.IsValid())
				{
					FLevelEditorViewportClient& LevelViewportClient = ActiveLevelViewport->GetLevelViewportClient();
					if (TargetCamera)
					{
						Backup_ActorLock = LevelViewportClient.GetActiveActorLock();

						LevelViewportClient.SetActorLock(TargetCamera.Get()->GetOwner());
						LevelViewportClient.AddRealtimeOverride(true, NSLOCTEXT("VCamCore", "RealtimeOverrideMessage_VCamCore", "VCamCore"));
					}
					else
					{
						UE_LOG(LogVCamOutputProvider, Warning, TEXT("CreateRemoteSession has been called, but there is no TargetCamera set!"));
					}
				}
			}
			else
#endif
			{
				UWorld* ActorWorld = Context.World();
				if (ActorWorld)
				{
					APlayerController* PlayerController = ActorWorld->GetGameInstance()->GetFirstLocalPlayerController(ActorWorld);
					if (PlayerController)
					{
						if (TargetCamera)
						{
							Backup_ViewTarget = PlayerController->GetViewTarget();
							PlayerController->SetViewTarget(TargetCamera.Get()->GetOwner());
						}
						else
						{
							UE_LOG(LogVCamOutputProvider, Warning, TEXT("CreateRemoteSession has been called, but there is no TargetCamera set!"));
						}
					}
					else
					{
						UE_LOG(LogVCamOutputProvider, Warning, TEXT("CreateRemoteSession has been called, but there is no PlayerController found!"));
					}
				}
			}
		}

		if (IRemoteSessionModule* RemoteSession = FModuleManager::LoadModulePtr<IRemoteSessionModule>("RemoteSession"))
		{
			TArray<FRemoteSessionChannelInfo> SupportedChannels;
			SupportedChannels.Emplace(FRemoteSessionInputChannel::StaticType(), ERemoteSessionChannelMode::Read, FOnRemoteSessionChannelCreated::CreateUObject(this, &UVCamOutputProvider::OnInputChannelCreated));
			SupportedChannels.Emplace(FRemoteSessionImageChannel::StaticType(), ERemoteSessionChannelMode::Write, FOnRemoteSessionChannelCreated::CreateUObject(this, &UVCamOutputProvider::OnImageChannelCreated));

			RemoteSessionHost = RemoteSession->CreateHost(MoveTemp(SupportedChannels), RemoteSessionPort);
			if (RemoteSessionHost.IsValid())
			{
				RemoteSessionHost->Tick(0.0f);
			}
		}
	}
	else
	{
		UE_LOG(LogVCamOutputProvider, Warning, TEXT("CreateRemoteSession has been called, but there is already a valid RemoteSessionHost!"));
	}
}

void UVCamOutputProvider::DestroyRemoteSession()
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

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
#if WITH_EDITOR
			if (Context.WorldType == EWorldType::Editor)
			{
				FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorName);
				TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
				if (ActiveLevelViewport.IsValid())
				{
					FLevelEditorViewportClient& LevelViewportClient = ActiveLevelViewport->GetLevelViewportClient();
					LevelViewportClient.SetActorLock(Backup_ActorLock.Get());
					Backup_ActorLock.Reset();
					LevelViewportClient.UpdateViewForLockedActor();

					LevelViewportClient.ViewFOV = LevelViewportClient.FOVAngle;
					GEditor->RemovePerspectiveViewRotation(true, true, false);
					LevelViewportClient.RemoveRealtimeOverride(NSLOCTEXT("VCamCore", "RealtimeOverrideMessage_VCamCore", "VCamCore"));
				}
			}
			else
#endif
			{
				UWorld* ActorWorld = Context.World();
				if (ActorWorld)
				{
					APlayerController* PlayerController = ActorWorld->GetGameInstance()->GetFirstLocalPlayerController(ActorWorld);
					if (PlayerController)
					{
						PlayerController->SetViewTarget(Backup_ViewTarget.Get());
						Backup_ViewTarget.Reset();
					}
				}
			}
		}
	}
	else
	{
		UE_LOG(LogVCamOutputProvider, Warning, TEXT("DestroyRemoteSession has been called, but there isn't a valid RemoteSessionHost!"));
	}
}

void UVCamOutputProvider::FindSceneViewport(TWeakPtr<SWindow>& OutInputWindow, TWeakPtr<FSceneViewport>& OutSceneViewport)
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor)
			{
				if (FModuleManager::Get().IsModuleLoaded(LevelEditorName))
				{
					FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorName);
					TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveViewport();
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

void UVCamOutputProvider::OnImageChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance, const FString& Type, ERemoteSessionChannelMode Mode)
{
	TSharedPtr<FRemoteSessionImageChannel> ImageChannel = StaticCastSharedPtr<FRemoteSessionImageChannel>(Instance.Pin());
	if (ImageChannel)
	{
		ImageChannel->SetImageProvider(nullptr);
		MediaOutput->SetImageChannel(ImageChannel);
		MediaCapture = Cast<URemoteSessionMediaCapture>(MediaOutput->CreateMediaCapture());

		TWeakPtr<SWindow> InputWindow;
		TWeakPtr<FSceneViewport> SceneViewport;
		FindSceneViewport(InputWindow, SceneViewport);
		if (TSharedPtr<FSceneViewport> PinnedSceneViewport = SceneViewport.Pin())
		{
			MediaCapture->CaptureSceneViewport(PinnedSceneViewport, FMediaCaptureOptions());
			UE_LOG(LogVCamOutputProvider, Log, TEXT("ImageChannel callback - MediaCapture set"));
		}
	}
}

void UVCamOutputProvider::OnInputChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance, const FString& Type, ERemoteSessionChannelMode Mode)
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
			InputChannel->GetOnRouteTouchDownToWidgetFailedDelegate()->AddUObject(this, &UVCamOutputProvider::OnTouchEventOutsideUMG);

			UE_LOG(LogVCamOutputProvider, Log, TEXT("InputChannel callback - Routing input to active viewport with UMG"));
		}
		else
		{
			TWeakPtr<SWindow> InputWindow;
			TWeakPtr<FSceneViewport> SceneViewport;
			FindSceneViewport(InputWindow, SceneViewport);
			InputChannel->SetPlaybackWindow(InputWindow, nullptr);
			InputChannel->TryRouteTouchMessageToWidget(true);
			InputChannel->GetOnRouteTouchDownToWidgetFailedDelegate()->AddUObject(this, &UVCamOutputProvider::OnTouchEventOutsideUMG);

			UE_LOG(LogVCamOutputProvider, Log, TEXT("InputChannel callback - Routing input to active viewport"));
		}
	}
}

void UVCamOutputProvider::OnTouchEventOutsideUMG(const FVector2D& InViewportPosition)
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
					FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorName);
					TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
					if (ActiveLevelViewport.IsValid() && ActiveLevelViewport->GetActiveViewport())
					{
						FViewport* ActiveViewport = ActiveLevelViewport->GetActiveViewport();
						FLevelEditorViewportClient& LevelViewportClient = ActiveLevelViewport->GetLevelViewportClient();
						FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
                            ActiveViewport,
                            LevelViewportClient.GetScene(),
                            LevelViewportClient.EngineShowFlags)
                            .SetRealtimeUpdate(true));
						FSceneView* View = LevelViewportClient.CalcSceneView(&ViewFamily);

						uint32 HitX = InViewportPosition.X;
						uint32 HitY = InViewportPosition.Y;
						HHitProxy* HitProxy = ActiveViewport->GetHitProxy(HitX,HitY);
						LevelViewportClient.ProcessClick(*View, HitProxy, EKeys::LeftMouseButton, EInputEvent::IE_Pressed, HitX, HitY);
					}
				}
				else
#endif
				{
					LastViewportTouchResult.GetHitObjectHandle().FetchActor()->NotifyActorOnClicked();
				}
			}
		}
	}
}

bool UVCamOutputProvider::DeprojectScreenToWorld(const FVector2D& InScreenPosition, FVector& OutWorldPosition, FVector& OutWorldDirection)
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
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorName);
			TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
			if (ActiveLevelViewport.IsValid() && ActiveLevelViewport->GetActiveViewport())
			{
				FViewport* ActiveViewport = ActiveLevelViewport->GetActiveViewport();
				FLevelEditorViewportClient& LevelViewportClient = ActiveLevelViewport->GetLevelViewportClient();
				FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
					ActiveViewport,
					LevelViewportClient.GetScene(),
					LevelViewportClient.EngineShowFlags)
					.SetRealtimeUpdate(true));
				FSceneView* View = LevelViewportClient.CalcSceneView(&ViewFamily);

				const FIntPoint ViewportSize = ActiveViewport->GetSizeXY();
				const FIntRect ViewRect = FIntRect(0, 0, ViewportSize.X, ViewportSize.Y);
				const FMatrix InvViewProjectionMatrix = View->ViewMatrices.GetInvViewProjectionMatrix();
				FSceneView::DeprojectScreenToWorld(InScreenPosition, ViewRect, InvViewProjectionMatrix, OutWorldPosition, OutWorldDirection);
				bSuccess = true;
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
