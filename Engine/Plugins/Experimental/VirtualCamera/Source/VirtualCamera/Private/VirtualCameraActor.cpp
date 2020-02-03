// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualCameraActor.h"

#include "CineCameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameEngine.h"
#include "Engine/GameViewportClient.h"
#include "Features/IModularFeatures.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "ILiveLinkClient.h"
#include "Channels/RemoteSessionImageChannel.h"
#include "Channels/RemoteSessionInputChannel.h"
#include "ImageProviders/RemoteSessionMediaOutput.h"
#include "RemoteSession.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"
#include "Slate/SceneViewport.h"
#include "UObject/ConstructorHelpers.h"
#include "VirtualCamera.h"
#include "VirtualCameraMovement.h"
#include "VirtualCameraSubsystem.h"
#include "VPFullScreenUserWidget.h"
#include "Widgets/SVirtualWindow.h"

#if WITH_EDITOR
#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Editor.h"
#include "EditorSupportDelegates.h"
#include "Editor/EditorEngine.h"
#include "IAssetViewport.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "SLevelViewport.h"
#endif

namespace
{
	static const FName AssetRegistryName(TEXT("AssetRegistry"));
	static const FName LevelEditorName(TEXT("LevelEditor"));
	static const TCHAR* DefaultCameraUMG = TEXT("/VirtualCamera/VirtualCameraUI.VirtualCameraUI_C");
	static const FName DefaultLiveLinkSubjectName(TEXT("CameraTransform"));
	static const FVector2D DefaultViewportResolution(1280, 720);

	void FindSceneViewport(TWeakPtr<SWindow>& OutInputWindow, TWeakPtr<FSceneViewport>& OutSceneViewport)
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
}

struct FVirtualCameraViewportSettings
{
	FIntPoint Size;
	FVector2D CameraPosition;
	TWeakObjectPtr<AActor> ActorLock;
	bool bRealTime;
	bool bDrawAxes;
	bool bDisableInput;
	bool bAllowCinematicControl;
};


AVirtualCameraActor::AVirtualCameraActor(const FObjectInitializer& ObjectInitializer)
	: LiveLinkSubject{ DefaultLiveLinkSubjectName, ULiveLinkTransformRole::StaticClass() }
	, ViewportResolution(DefaultViewportResolution)
	, RemoteSessionPort(IRemoteSessionModule::kDefaultPort)
	, ActorWorld(nullptr)
	, PreviousViewTarget(nullptr)
	, bIsStreaming(false)
	, ViewportSettingsBackup(nullptr)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// Create Components
	DefaultSceneRoot = CreateDefaultSubobject<USceneComponent>("DefaultSceneRoot");
	SetRootComponent(DefaultSceneRoot);

	RecordingCamera = CreateDefaultSubobject<UCineCameraComponent>("Recording Camera");
	RecordingCamera->SetupAttachment(DefaultSceneRoot);
	StreamedCamera = CreateDefaultSubobject<UCineCameraComponent>("Streamed Camera");
	StreamedCamera->SetupAttachment(DefaultSceneRoot);

	MovementComponent = CreateDefaultSubobject<UVirtualCameraMovement>("Movement Component");
	MediaOutput = CreateDefaultSubobject<URemoteSessionMediaOutput>("Media Output");
	CameraScreenWidget = CreateDefaultSubobject<UVPFullScreenUserWidget>("Camera UMG");
	CameraScreenWidget->SetDisplayTypes(EVPWidgetDisplayType::PostProcess, EVPWidgetDisplayType::Viewport, EVPWidgetDisplayType::PostProcess);
	CameraScreenWidget->PostProcessDisplayType.bReceiveHardwareInput = true;
	ConstructorHelpers::FClassFinder<UUserWidget> UMG_Finder(DefaultCameraUMG);
	if (UMG_Finder.Class)
	{
		CameraUMGclass = UMG_Finder.Class;
	}
}

AVirtualCameraActor::AVirtualCameraActor(FVTableHelper& Helper)
	: Super(Helper)
{
}

AVirtualCameraActor::~AVirtualCameraActor() = default;

void AVirtualCameraActor::Destroyed()
{
	if (CameraScreenWidget && CameraScreenWidget->IsDisplayed())
	{
		CameraScreenWidget->Hide();
	}

	if (RemoteSessionHost && RemoteSessionHost->IsConnected())
	{
		RemoteSessionHost->Close();
	}
}

#if WITH_EDITOR
bool AVirtualCameraActor::ShouldTickIfViewportsOnly() const
{
	return true;
}
#endif

bool AVirtualCameraActor::StartStreaming()
{
	ActorWorld = GetWorld();
	if (!ActorWorld)
	{
		return false;
	}

	if (UVirtualCameraSubsystem* SubSystem = GEngine->GetEngineSubsystem<UVirtualCameraSubsystem>())
	{
		SubSystem->SetVirtualCameraController(this);
	}

#if WITH_EDITOR
	if (ActorWorld->WorldType == EWorldType::Editor)
	{
		ViewportSettingsBackup = MakeUnique<FVirtualCameraViewportSettings>();

		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorName);
		TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
		if (ActiveLevelViewport.IsValid())
		{
			ActiveLevelViewport->GetSharedActiveViewport()->SetFixedViewportSize(ViewportResolution.X, ViewportResolution.Y);

			FLevelEditorViewportClient& LevelViewportClient = ActiveLevelViewport->GetLevelViewportClient();
			ViewportSettingsBackup->ActorLock = LevelViewportClient.GetActiveActorLock();
			LevelViewportClient.SetActorLock(this);

			ViewportSettingsBackup->bDrawAxes = LevelViewportClient.bDrawAxes;
			ViewportSettingsBackup->bDisableInput = LevelViewportClient.bDisableInput;
			ViewportSettingsBackup->bAllowCinematicControl = LevelViewportClient.AllowsCinematicControl();

			LevelViewportClient.SetRealtime(true);
			LevelViewportClient.bDrawAxes = false;
			LevelViewportClient.bDisableInput = true;
			LevelViewportClient.SetAllowCinematicControl(false);
		
			// add event listeners to stop streaming when necessary
			LevelEditorModule.OnMapChanged().AddUObject(this, &AVirtualCameraActor::OnMapChanged);
			GEditor->OnBlueprintPreCompile().AddUObject(this, &AVirtualCameraActor::OnBlueprintPreCompile);
			FEditorSupportDelegates::PrepareToCleanseEditorObject.AddUObject(this, &AVirtualCameraActor::OnPrepareToCleanseEditorObject);
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryName);
			AssetRegistryModule.Get().OnAssetRemoved().AddUObject(this, &AVirtualCameraActor::OnAssetRemoved);
			FEditorDelegates::OnAssetsCanDelete.AddUObject(this, &AVirtualCameraActor::OnAssetsCanDelete);
		}
	}
	else
#endif
	{
		APlayerController* PlayerController = ActorWorld->GetGameInstance()->GetFirstLocalPlayerController();
		if (!PlayerController)
		{
			return false;
		}

		PreviousViewTarget = PlayerController->GetViewTarget();

		FViewTargetTransitionParams TransitionParams;
		PlayerController->SetViewTarget(this, TransitionParams);
	}

	if (CameraUMGclass)
	{
		CameraScreenWidget->WidgetClass = CameraUMGclass;
		CameraScreenWidget->Display(ActorWorld);
	}

	if (IRemoteSessionModule* RemoteSession = FModuleManager::LoadModulePtr<IRemoteSessionModule>("RemoteSession"))
	{
		TArray<FRemoteSessionChannelInfo> SupportedChannels;
		SupportedChannels.Emplace(FRemoteSessionInputChannel::StaticType(), ERemoteSessionChannelMode::Read, FOnRemoteSessionChannelCreated::CreateUObject(this, &AVirtualCameraActor::OnInputChannelCreated));
		SupportedChannels.Emplace(FRemoteSessionImageChannel::StaticType(), ERemoteSessionChannelMode::Write, FOnRemoteSessionChannelCreated::CreateUObject(this, &AVirtualCameraActor::OnImageChannelCreated));

		if (RemoteSessionHost = RemoteSession->CreateHost(MoveTemp(SupportedChannels), RemoteSessionPort))
		{
			RemoteSessionHost->Tick(0.0f);
		}
	}

	SetActorTickEnabled(true);

	bIsStreaming = true;
	return true;
}

bool AVirtualCameraActor::StopStreaming()
{
	RemoteSessionHost.Reset();

	CameraScreenWidget->Hide();
	if (MediaCapture)
	{
		MediaCapture->StopCapture(true);
	}

#if WITH_EDITOR
	if (ActorWorld && ActorWorld->WorldType == EWorldType::Editor)
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorName);
		TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
		if (ActiveLevelViewport.IsValid())
		{
			// restore FOV
			GCurrentLevelEditingViewportClient->ViewFOV = GCurrentLevelEditingViewportClient->FOVAngle;

			FLevelEditorViewportClient& LevelViewportClient = ActiveLevelViewport->GetLevelViewportClient();
			LevelViewportClient.SetActorLock(ViewportSettingsBackup->ActorLock.Get());
			GCurrentLevelEditingViewportClient->UpdateViewForLockedActor();

			// remove roll and pitch from camera when unbinding from actors
			GEditor->RemovePerspectiveViewRotation(true, true, false);

			LevelViewportClient.SetRealtime(ViewportSettingsBackup->bRealTime);
			LevelViewportClient.bDrawAxes = ViewportSettingsBackup->bDrawAxes;
			LevelViewportClient.bDisableInput = ViewportSettingsBackup->bDisableInput;
			LevelViewportClient.SetAllowCinematicControl(ViewportSettingsBackup->bAllowCinematicControl);

			// unlock viewport resize
			ActiveLevelViewport->GetSharedActiveViewport()->SetFixedViewportSize(0, 0);
		
			// remove event listeners
			FEditorDelegates::OnAssetsCanDelete.RemoveAll(this);
			LevelEditorModule.OnMapChanged().RemoveAll(this);
			if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>(AssetRegistryName))
			{
				AssetRegistryModule->Get().OnAssetRemoved().RemoveAll(this);
			}
			FEditorSupportDelegates::PrepareToCleanseEditorObject.RemoveAll(this);
			GEditor->OnBlueprintPreCompile().RemoveAll(this);
		}

		ViewportSettingsBackup.Reset();
	}
	else
#endif
	{
		if (PreviousViewTarget)
		{
			APlayerController* PlayerController = ActorWorld->GetGameInstance()->GetFirstLocalPlayerController();
			if (!PlayerController)
			{
				return false;
			}
			FViewTargetTransitionParams TransitionParams;
			PlayerController->SetViewTarget(PreviousViewTarget, TransitionParams);
		}
	}

	SetActorTickEnabled(false);

	bIsStreaming = false;
	return true;
}

bool AVirtualCameraActor::IsStreaming() const
{
	return bIsStreaming;
}

void AVirtualCameraActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bIsStreaming)
	{
		return;
	}

	if (RemoteSessionHost)
	{
		RemoteSessionHost->Tick(DeltaSeconds);
	}

	if (CameraScreenWidget && CameraUMGclass)
	{
		CameraScreenWidget->Tick(DeltaSeconds);
	}

	FMinimalViewInfo ViewInfo;
	CalcCamera(DeltaSeconds, ViewInfo);

	ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	FLiveLinkSubjectFrameData SubjectData;
	const bool bHasValidData = LiveLinkClient.EvaluateFrame_AnyThread(LiveLinkSubject.Subject, LiveLinkSubject.Role, SubjectData);
	if (bHasValidData)
	{
		const FLiveLinkTransformFrameData& TransformFrameData = *SubjectData.FrameData.Cast<FLiveLinkTransformFrameData>();
		FVirtualCameraTransform CameraTransform = FVirtualCameraTransform{TransformFrameData.Transform};

		// execute delegates that want to manipulate camera transform before it is set onto the root
#if WITH_EDITOR
		if (GIsEditor && OnPreSetVirtualCameraTransform.IsBound())
		{
			FEditorScriptExecutionGuard ScriptGuard;
			CameraTransform = OnPreSetVirtualCameraTransform.Execute(CameraTransform);
		}
#else
		if (OnPreSetVirtualCameraTransform.IsBound())
		{
			CameraTransform = OnPreSetVirtualCameraTransform.Execute(CameraTransform);
		}
#endif

		MovementComponent->SetLocalTransform(CameraTransform.Transform);
		RootComponent->SetWorldTransform(MovementComponent->GetTransform());
	}

	if (OnVirtualCameraUpdated.IsBound())
	{
		FEditorScriptExecutionGuard ScriptGuard;
		OnVirtualCameraUpdated.Broadcast(DeltaSeconds);
	}
}

void AVirtualCameraActor::BeginPlay()
{
	Super::BeginPlay();
	StartStreaming();
}

void AVirtualCameraActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	StopStreaming();
}

void AVirtualCameraActor::OnImageChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance, const FString& Type, ERemoteSessionChannelMode Mode)
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
		}
	}
}

void AVirtualCameraActor::OnInputChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance, const FString& Type, ERemoteSessionChannelMode Mode)
{
	TSharedPtr<FRemoteSessionInputChannel> InputChannel = StaticCastSharedPtr<FRemoteSessionInputChannel>(Instance.Pin());
	if (InputChannel)
	{
		TSharedPtr<SVirtualWindow> InputWindow = CameraScreenWidget->PostProcessDisplayType.GetSlateWindow();
		InputChannel->SetPlaybackWindow(InputWindow, nullptr);
		InputChannel->TryRouteTouchMessageToWidget(true);
	}
}

void AVirtualCameraActor::OnMapChanged(UWorld* World, EMapChangeType ChangeType)
{
	if (World == ActorWorld && ChangeType == EMapChangeType::TearDownWorld)
	{
		StopStreaming();
	}
}

void AVirtualCameraActor::OnBlueprintPreCompile(UBlueprint* Blueprint)
{
	if (Blueprint && CameraUMGclass && Blueprint->GeneratedClass == CameraUMGclass)
	{
		StopStreaming();
	}
}

void AVirtualCameraActor::OnPrepareToCleanseEditorObject(UObject* Object)
{
	if (Object == CameraScreenWidget || Object == CameraUMGclass || Object == ActorWorld || Object == MediaCapture)
	{
		StopStreaming();
	}
}

void AVirtualCameraActor::OnAssetRemoved(const FAssetData& AssetData)
{
	if (AssetData.GetPackage() == CameraUMGclass->GetOutermost())
	{
		StopStreaming();
	}
}

void AVirtualCameraActor::OnAssetsCanDelete(const TArray<UObject*>& InAssetsToDelete, FCanDeleteAssetResult& CanDeleteResult)
{
	if (CameraUMGclass)
	{
		for (UObject* Obj : InAssetsToDelete)
		{
			if (CameraUMGclass->GetOutermost() == Obj->GetOutermost())
			{
				UE_LOG(LogVirtualCamera, Warning, TEXT("Asset '%s' can't be deleted because it is currently used by the Virtual Camera Stream."), *Obj->GetPathName());
				CanDeleteResult.Set(false);
				break;
			}
		}
	}
}
