// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRootActor.h"

#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#include "HAL/IConsoleManager.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationStrings.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterAppExit.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"

#include "Components/DisplayClusterRootComponent.h"
#include "Components/DisplayClusterSceneComponentSyncParent.h"
#include "DisplayClusterPlayerInput.h"


ADisplayClusterRootActor::ADisplayClusterRootActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bExitOnEsc(true)
	, bShowProjectionScreens(false)
	, ProjectionScreensMaterial(nullptr)
{
	// Root component
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));

	// DisplayCluster sync
	DisplayClusterRootComponent = CreateDefaultSubobject<UDisplayClusterRootComponent>(TEXT("DisplayClusterRoot"));
	DisplayClusterRootComponent->AttachToComponent(RootComponent, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = ETickingGroup::TG_PostUpdateWork;
	bFindCameraComponentWhenViewTarget = false;
	bReplicates = false;
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
}

void ADisplayClusterRootActor::BeginPlay()
{
	Super::BeginPlay();

	// Store current operation mode
	OperationMode = GDisplayCluster->GetOperationMode();
	if (OperationMode == EDisplayClusterOperationMode::Cluster)
	{
		FString SyncPolicyType;

		// Read native input synchronization settings
		IPDisplayClusterConfigManager* const ConfigMgr = GDisplayCluster->GetPrivateConfigMgr();
		if (ConfigMgr)
		{
			const UDisplayClusterConfigurationData* ConfigData = ConfigMgr->GetConfig();
			if (ConfigData)
			{
				SyncPolicyType = ConfigData->Cluster->Sync.InputSyncPolicy.Type;
				UE_LOG(LogDisplayClusterGame, Log, TEXT("Native input sync policy: %s"), *SyncPolicyType);
			}
		}

		// Optionally activate native input synchronization
		if (SyncPolicyType.Equals(DisplayClusterConfigurationStrings::config::cluster::input_sync::InputSyncPolicyReplicateMaster, ESearchCase::IgnoreCase))
		{
			APlayerController* const PlayerController = GetWorld()->GetFirstPlayerController();
			if (PlayerController)
			{
				PlayerController->PlayerInput = NewObject<UDisplayClusterPlayerInput>(PlayerController);
			}
		}
	}
}

void ADisplayClusterRootActor::Tick(float DeltaSeconds)
{
	if (OperationMode == EDisplayClusterOperationMode::Cluster ||
		OperationMode == EDisplayClusterOperationMode::Editor)
	{
		UWorld* const CurWorld = GetWorld();
		if (CurWorld)
		{
			APlayerController* const CurPlayerController = CurWorld->GetFirstPlayerController();
			if (CurPlayerController)
			{
				APlayerCameraManager* const CurPlayerCameraManager = CurPlayerController->PlayerCameraManager;
				if (CurPlayerCameraManager)
				{
					SetActorLocationAndRotation(CurPlayerCameraManager->GetCameraLocation(), CurPlayerCameraManager->GetCameraRotation());
				}

				if (bExitOnEsc)
				{
					if (CurPlayerController->WasInputKeyJustPressed(EKeys::Escape))
					{
						FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::NormalSoft, FString("Exit on ESC requested"));
					}
				}
			}
		}
	}

	// Show 'not supported' warning if instanced stereo is used
	if (OperationMode != EDisplayClusterOperationMode::Disabled)
	{
		static const TConsoleVariableData<int32>* const InstancedStereoCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.InstancedStereo"));
		if (InstancedStereoCVar)
		{
			const bool bIsInstancedStereoRequested = (InstancedStereoCVar->GetValueOnGameThread() != 0);
			if (bIsInstancedStereoRequested)
			{
				UE_LOG(LogDisplayClusterGame, Error, TEXT("Instanced stereo was requested. nDisplay doesn't support instanced stereo so far."));
				GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Red, TEXT("nDisplay doesn't support instanced stereo"));
			}
		}
	}

	Super::Tick(DeltaSeconds);
}
