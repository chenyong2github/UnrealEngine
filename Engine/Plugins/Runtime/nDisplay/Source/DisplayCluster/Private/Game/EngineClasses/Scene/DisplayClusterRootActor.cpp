// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRootActor.h"

#include "Components/SceneComponent.h"
#include "Components/DisplayClusterOriginComponent.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterMeshComponent.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "Components/DisplayClusterXformComponent.h"
#include "Components/DisplayClusterSceneComponentSyncParent.h"
#include "Components/DisplayClusterPreviewComponent.h"
#include "Components/DisplayClusterSyncTickComponent.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationStrings.h"

#include "IDisplayClusterConfiguration.h"
#include "DisplayClusterConfigurationTypes.h"

#include "DisplayClusterPlayerInput.h"

#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#include "HAL/IConsoleManager.h"

#include "Misc/DisplayClusterAppExit.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"


ADisplayClusterRootActor::ADisplayClusterRootActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bExitOnEsc(true)
	, OperationMode(EDisplayClusterOperationMode::Disabled)
{
	// Root component
	RootComponent = CreateDefaultSubobject<UDisplayClusterOriginComponent>(TEXT("DisplayClusterOrigin"));
	// A helper component to trigger nDisplay Tick() during Tick phase
	SyncTickComponent = CreateDefaultSubobject<UDisplayClusterSyncTickComponent>(TEXT("DisplayClusterSyncTick"));

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = ETickingGroup::TG_PostUpdateWork;
	bFindCameraComponentWhenViewTarget = false;
	bReplicates = false;
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
}

void ADisplayClusterRootActor::InitializeFromConfig(const UDisplayClusterConfigurationData* ConfigData)
{
	// Clean up current hierarchy before building a new one
	CleanupHierarchy();

	if (ConfigData)
	{
		BuildHierarchy(ConfigData);

#if WITH_EDITOR
		if (GIsEditor)
		{
			SetPreviewNodeId(ADisplayClusterRootActor::PreviewNodeNone);
		}
#endif
	}
}

void ADisplayClusterRootActor::InitializeFromConfig(const FString& ConfigFile)
{
	// Clean up current hierarchy before building a new one
	CleanupHierarchy();

	if (!ConfigFile.IsEmpty())
	{
		// Update config data
		const UDisplayClusterConfigurationData* ConfigData = IDisplayClusterConfiguration::Get().LoadConfig(ConfigFile, this);
		if (ConfigData)
		{
			InitializeFromConfig(ConfigData);
		}
	}
}

void ADisplayClusterRootActor::InitializeRootActor()
{
	const UWorld* World = GetWorld();

	if (!World)
	{
		return;
	}
	
	bool bIsPIE = false;

#if WITH_EDITOR
	bIsPIE = World->IsPlayInEditor();
#endif

	// Packaged, PIE and -game runtime
	if (IsRunningGame() || bIsPIE)
	{
		IPDisplayClusterConfigManager* const ConfigMgr = (GDisplayCluster ? GDisplayCluster->GetPrivateConfigMgr() : nullptr);
		if (ConfigMgr)
		{
			const UDisplayClusterConfigurationData* ConfigData = ConfigMgr->GetConfig();
			if (ConfigData)
			{
				InitializeFromConfig(ConfigData);
			}
		}
	}
#if WITH_EDITOR
	// Initialize from file property by default in Editor
	else
	{
		InitializeFromConfig(PreviewConfigPath.FilePath);
	}
#endif
}

bool ADisplayClusterRootActor::BuildHierarchy(const UDisplayClusterConfigurationData* ConfigData)
{
	check(ConfigData);

	// Store new config data object
	CurrentConfigData = ConfigData;

	// Spawn all components
	SpawnComponents<UDisplayClusterXformComponent,  UDisplayClusterConfigurationSceneComponentXform> (ConfigData->Scene->Xforms,  XformComponents,  AllComponents);
	SpawnComponents<UDisplayClusterCameraComponent, UDisplayClusterConfigurationSceneComponentCamera>(ConfigData->Scene->Cameras, CameraComponents, AllComponents);
	SpawnComponents<UDisplayClusterScreenComponent, UDisplayClusterConfigurationSceneComponentScreen>(ConfigData->Scene->Screens, ScreenComponents, AllComponents);
	SpawnComponents<UDisplayClusterMeshComponent,   UDisplayClusterConfigurationSceneComponentMesh>  (ConfigData->Scene->Meshes,  MeshComponents,   AllComponents);

	ReregisterAllComponents();

	// Let the components apply their individual config parameters (in-Editor and before BeginPlay in gameplay)
	for (const TPair<FString, FDisplayClusterSceneComponentRef*>& Component : AllComponents)
	{
		UDisplayClusterSceneComponent* DisplayClusterSceneComponent = Cast<UDisplayClusterSceneComponent>(Component.Value->GetOrFindSceneComponent());
		if (DisplayClusterSceneComponent)
		{
			DisplayClusterSceneComponent->ApplyConfigurationData();
		}
	}

	// Check if default camera was specified in command line arguments
	FString DefaultCamId;
	if (FParse::Value(FCommandLine::Get(), DisplayClusterStrings::args::Camera, DefaultCamId))
	{
		DisplayClusterHelpers::str::TrimStringValue(DefaultCamId);
		UE_LOG(LogDisplayClusterGame, Log, TEXT("Default camera from command line arguments: %s"), *DefaultCamId);
		if (CameraComponents.Contains(DefaultCamId))
		{
			SetDefaultCamera(DefaultCamId);
		}
	}

	// If no default camera set, try to set the first one
	if (!DefaultCameraComponent.IsDefinedSceneComponent())
	{
		if (CameraComponents.Num() > 0)
		{
			// There is no guarantee that default camera is the first one listed in a config file
			SetDefaultCamera(CameraComponents.CreateConstIterator()->Key);
		}
		else
		{
			UE_LOG(LogDisplayClusterGame, Error, TEXT("No cameras found"));
			return false;
		}
	}

	return true;
}

void ADisplayClusterRootActor::CleanupHierarchy()
{
	{
		FScopeLock Lock(&InternalsSyncScope);

		// Delete all components except of the RootComponent
		TArray<USceneComponent*> ChildrenComponents;
		RootComponent->GetChildrenComponents(true, ChildrenComponents);
		for (USceneComponent* ChildComponent : ChildrenComponents)
		{
			ChildComponent->DestroyComponent();
		}

		// Clean containers. We store only pointers so there is no need to do any additional
		// operations. All components will be destroyed by the engine.
		XformComponents.Reset();
		CameraComponents.Reset();
		ScreenComponents.Reset();
		MeshComponents.Reset();
		AllComponents.Reset();

		// Invalidate current config as well
		CurrentConfigData = nullptr;
	}
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

void ADisplayClusterRootActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanupHierarchy();
	Super::EndPlay(EndPlayReason);
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

void ADisplayClusterRootActor::PostLoad()
{
	InitializeRootActor();
	Super::PostLoad();
}

void ADisplayClusterRootActor::PostActorCreated()
{
	InitializeRootActor();
	Super::PostActorCreated();
}

int32 ADisplayClusterRootActor::GetScreensAmount() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return ScreenComponents.Num();
}

UDisplayClusterScreenComponent* ADisplayClusterRootActor::GetScreenById(const FString& ScreenId) const
{
	FScopeLock Lock(&InternalsSyncScope);
	return GetTypedComponentById<UDisplayClusterScreenComponent>(ScreenId, ScreenComponents);
}

void ADisplayClusterRootActor::GetAllScreens(TMap<FString, UDisplayClusterScreenComponent*>& OutScreens) const
{
	FScopeLock Lock(&InternalsSyncScope);
	GetTypedComponents<UDisplayClusterScreenComponent>(OutScreens, ScreenComponents);
}

int32 ADisplayClusterRootActor::GetCamerasAmount() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return CameraComponents.Num();
}

UDisplayClusterCameraComponent* ADisplayClusterRootActor::GetCameraById(const FString& CameraId) const
{
	FScopeLock Lock(&InternalsSyncScope);
	return GetTypedComponentById<UDisplayClusterCameraComponent>(CameraId, CameraComponents);
}

void ADisplayClusterRootActor::GetAllCameras(TMap<FString, UDisplayClusterCameraComponent*>& OutCameras) const
{
	FScopeLock Lock(&InternalsSyncScope);
	GetTypedComponents<UDisplayClusterCameraComponent>(OutCameras, CameraComponents);
}

UDisplayClusterCameraComponent* ADisplayClusterRootActor::GetDefaultCamera() const
{
	return Cast<UDisplayClusterCameraComponent>(DefaultCameraComponent.GetOrFindSceneComponent());
}

void ADisplayClusterRootActor::SetDefaultCamera(const FString& CameraId)
{
	FScopeLock Lock(&InternalsSyncScope);

	UDisplayClusterCameraComponent* NewDefaultCamera = GetCameraById(CameraId);
	if (NewDefaultCamera)
	{
		DefaultCameraComponent.SetSceneComponent(NewDefaultCamera);
	}
}

int32 ADisplayClusterRootActor::GetMeshesAmount() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return MeshComponents.Num();
}

UDisplayClusterMeshComponent* ADisplayClusterRootActor::GetMeshById(const FString& MeshId) const
{
	FScopeLock Lock(&InternalsSyncScope);
	return GetTypedComponentById<UDisplayClusterMeshComponent>(MeshId, MeshComponents);
}

void ADisplayClusterRootActor::GetAllMeshes(TMap<FString, UDisplayClusterMeshComponent*>& OutMeshes) const
{
	FScopeLock Lock(&InternalsSyncScope);
	GetTypedComponents<UDisplayClusterMeshComponent>(OutMeshes, MeshComponents);
}

int32 ADisplayClusterRootActor::GetXformsAmount() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return XformComponents.Num();
}

UDisplayClusterXformComponent* ADisplayClusterRootActor::GetXformById(const FString& XformId) const
{
	FScopeLock Lock(&InternalsSyncScope);
	return GetTypedComponentById<UDisplayClusterXformComponent>(XformId, XformComponents);
}

void ADisplayClusterRootActor::GetAllXforms(TMap<FString, UDisplayClusterXformComponent*>& OutXforms) const
{
	FScopeLock Lock(&InternalsSyncScope);
	GetTypedComponents<UDisplayClusterXformComponent>(OutXforms, XformComponents);
}

int32 ADisplayClusterRootActor::GetComponentsAmount() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return AllComponents.Num();
}

UDisplayClusterSceneComponent* ADisplayClusterRootActor::GetComponentById(const FString& ComponentId) const
{
	FScopeLock Lock(&InternalsSyncScope);
	return GetTypedComponentById<UDisplayClusterSceneComponent>(ComponentId, AllComponents);
}

void ADisplayClusterRootActor::GetAllComponents(TMap<FString, UDisplayClusterSceneComponent*>& OutComponents) const
{
	FScopeLock Lock(&InternalsSyncScope);
	GetTypedComponents<UDisplayClusterSceneComponent>(OutComponents, AllComponents);
}

template <typename TComp, typename TCfgData>
void ADisplayClusterRootActor::SpawnComponents(const TMap<FString, TCfgData*>& InConfigData, TMap<FString, FDisplayClusterSceneComponentRef*>& OutTypedMap, TMap<FString, FDisplayClusterSceneComponentRef*>& OutAllMap)
{
	for (const auto& it : InConfigData)
	{
		if (!OutAllMap.Contains(it.Key))
		{
			TComp* NewComponent = NewObject<TComp>(this, FName(*it.Key));
			if (NewComponent)
			{
				NewComponent->SetFlags(EObjectFlags::RF_DuplicateTransient | RF_Transient | RF_TextExportTransient);
				NewComponent->SetConfigParameters(it.Value);
				NewComponent->AttachToComponent(RootComponent, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));

				// Save references
				FDisplayClusterSceneComponentRef* NewComponentRef = new FDisplayClusterSceneComponentRef(NewComponent);
				OutAllMap.Emplace(it.Key, NewComponentRef);
				OutTypedMap.Emplace(it.Key, NewComponentRef);
			}
		}
	}
}

template <typename TComp>
void ADisplayClusterRootActor::GetTypedComponents(TMap<FString, TComp*>& OutTypedMap, const TMap<FString, FDisplayClusterSceneComponentRef*>& InTypedMap) const
{
	for (const TPair<FString, FDisplayClusterSceneComponentRef*>& Component : InTypedMap)
	{
		TComp* DisplayClusterSceneComponent = Cast<TComp>(Component.Value->GetOrFindSceneComponent());
		if (DisplayClusterSceneComponent)
		{
			OutTypedMap.Add(Component.Key, DisplayClusterSceneComponent);
		}
	}
}

template <typename TComp>
TComp* ADisplayClusterRootActor::GetTypedComponentById(const FString& ComponentId, const TMap<FString, FDisplayClusterSceneComponentRef*>& InTypedMap) const
{
	if (InTypedMap.Contains(ComponentId))
	{
		return Cast<TComp>(InTypedMap[ComponentId]->GetOrFindSceneComponent());
	}

	return nullptr;
}
