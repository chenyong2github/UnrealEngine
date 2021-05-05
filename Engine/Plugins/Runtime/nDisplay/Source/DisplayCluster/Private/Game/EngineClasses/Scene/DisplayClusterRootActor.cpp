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

#include "Components/DisplayClusterRootComponent.h"
#include "Components/DisplayClusterICVFX_CineCameraComponent.h"
#include "CineCameraComponent.h"
#include "Components/DisplayClusterICVFX_RefCineCameraComponent.h"
#include "Components/DisplayClusterSceneComponentSyncThis.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationStrings.h"

#include "IDisplayClusterConfiguration.h"
#include "DisplayClusterConfigurationTypes.h"

#include "DisplayClusterPlayerInput.h"
#include "Blueprints/DisplayClusterBlueprint.h"
#include "Blueprints/DisplayClusterBlueprintGeneratedClass.h"

#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#include "HAL/IConsoleManager.h"

#include "Misc/DisplayClusterAppExit.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"

#include "Render/Viewport/DisplayClusterViewportManager.h"

ADisplayClusterRootActor::ADisplayClusterRootActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bFollowLocalPlayerCamera(false)
	, bExitOnEsc(true)
	, OperationMode(EDisplayClusterOperationMode::Disabled)
{
	// Root component
	/*
	 * We HAVE to store the root component in our own UPROPERTY marked visible.
	 * Live link has a property which maintains a component reference. Live link sets this
	 * through their details panel automatically, which unreal validates in
	 * FComponentReferenceCustomization::IsComponentReferenceValid.
	 *
	 * Unreal won't allow native components that don't have CPF_Edit to be set. Luckily
	 * they search the owning class for a property containing the component.
	 */
	{
		DisplayClusterRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
		SetRootComponent(DisplayClusterRootComponent);
	}
	// A helper component to trigger nDisplay Tick() during Tick phase
	SyncTickComponent = CreateDefaultSubobject<UDisplayClusterSyncTickComponent>(TEXT("DisplayClusterSyncTick"));

	// A ICVFX Stage settings
	StageSettings = CreateDefaultSubobject<UDisplayClusterConfigurationICVFX_StageSettings>(TEXT("StageSettings"));

	// A render frame settings (allow control whole cluster rendering)
	RenderFrameSettings = CreateDefaultSubobject<UDisplayClusterConfigurationRenderFrame>(TEXT("RenderFrameSettings"));

	ViewportManager = MakeUnique<FDisplayClusterViewportManager>();

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = ETickingGroup::TG_PostUpdateWork;

#if WITH_EDITOR
	Constructor_Editor();
#endif

	bFindCameraComponentWhenViewTarget = false;
	bReplicates = false;
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
}

ADisplayClusterRootActor::~ADisplayClusterRootActor()
{
#if WITH_EDITOR
	Destructor_Editor();
#endif
}

bool ADisplayClusterRootActor::IsRunningGameOrPIE() const
{
	if (!IsRunningGame())
	{
#if WITH_EDITOR
		const UWorld* World = GetWorld();
		return World && World->IsPlayInEditor();
#else
		return true;
#endif
	}

	return true;
}

void ADisplayClusterRootActor::InitializeFromConfig(UDisplayClusterConfigurationData* ConfigData)
{
	// Clean up current hierarchy before building a new one
	CleanupHierarchy();

	if (ConfigData)
	{
		// Store new config data object
		UpdateConfigDataInstance(ConfigData);

		BuildHierarchy();

#if WITH_EDITOR
		if (GIsEditor && GetWorld())
		{
			UpdatePreviewComponents();
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
		UDisplayClusterConfigurationData* ConfigData = IDisplayClusterConfiguration::Get().LoadConfig(ConfigFile, this);
		if (ConfigData)
		{
			InitializeFromConfig(ConfigData);
		}
	}
}

void ADisplayClusterRootActor::ApplyConfigDataToComponents()
{
	TArray<UDisplayClusterSceneComponent*> DCSComponents;
	GetComponents<UDisplayClusterSceneComponent>(DCSComponents);

	for (UDisplayClusterSceneComponent* SceneComponent : DCSComponents)
	{
		SceneComponent->ApplyConfigurationData();
	}
}

UDisplayClusterConfigurationViewport* ADisplayClusterRootActor::GetViewportConfiguration(const FString& ClusterNodeID, const FString& ViewportID)
{
	if (CurrentConfigData)
	{
		return CurrentConfigData->GetViewportConfiguration(ClusterNodeID, ViewportID);
	}

	return nullptr;
}

void ADisplayClusterRootActor::UpdateConfigDataInstance(UDisplayClusterConfigurationData* ConfigDataTemplate, bool bForceRecreate)
{
	if (ConfigDataTemplate == nullptr)
	{
		CurrentConfigData = nullptr;
		ConfigDataName = TEXT("");
	}
	else
	{
		if (CurrentConfigData == nullptr)
		{
			// Only create config data once. Do not create in constructor as default sub objects or individual properties won't sync
			// properly with instanced values.

			const EObjectFlags CommonFlags = RF_Public | RF_Transactional;
			
			CurrentConfigData = NewObject<UDisplayClusterConfigurationData>(
				this,
				UDisplayClusterConfigurationData::StaticClass(),
				NAME_None,
				IsTemplate() ? RF_ArchetypeObject | CommonFlags : CommonFlags,
				ConfigDataTemplate);
			
			if (CurrentConfigData->Cluster == nullptr)
			{
				CurrentConfigData->Cluster = NewObject<UDisplayClusterConfigurationCluster>(
					CurrentConfigData,
					UDisplayClusterConfigurationCluster::StaticClass(),
					NAME_None,
					IsTemplate() ? RF_ArchetypeObject | CommonFlags : CommonFlags);
			}
		}
		else if (bForceRecreate)
		{
			UEngine::FCopyPropertiesForUnrelatedObjectsParams Params;
			Params.bAggressiveDefaultSubobjectReplacement = true;
			Params.bNotifyObjectReplacement = false;
			Params.bDoDelta = false;
			UEngine::CopyPropertiesForUnrelatedObjects(ConfigDataTemplate, CurrentConfigData, Params);
		}

		ConfigDataName = CurrentConfigData->GetFName();
	}
}

UDisplayClusterConfigurationData* ADisplayClusterRootActor::GetDefaultConfigDataFromAsset() const
{
	UClass* CurrentClass = GetClass();
	while (CurrentClass)
	{
		if (UObject* FoundTemplate = Cast<UObject>(CurrentClass->GetDefaultSubobjectByName(ConfigDataName)))
		{
			return Cast<UDisplayClusterConfigurationData>(FoundTemplate);
		}
		CurrentClass = CurrentClass->GetSuperClass();
	}

	return nullptr;
}

UDisplayClusterConfigurationData* ADisplayClusterRootActor::GetConfigData() const
{
	return CurrentConfigData;
}

template <typename TComp>
void ImplCollectChildrenVisualizationComponent(TSet<FPrimitiveComponentId>& OutPrimitives, TComp* pComp)
{
#if WITH_EDITOR
	USceneComponent* SceneComp = Cast<USceneComponent>(pComp);
	if (SceneComp)
	{
		TArray<USceneComponent*> Childrens;
		SceneComp->GetChildrenComponents(false, Childrens);
		for (USceneComponent* ChildIt : Childrens)
		{
			// Hide attached visualization components
			if (ChildIt->IsVisualizationComponent() || ChildIt->bHiddenInGame)
			{
				UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(ChildIt);
				if (PrimComp)
				{
					OutPrimitives.Add(PrimComp->ComponentId);
				}
			}
		}
	}
#endif
}

template <typename TComp>
void ADisplayClusterRootActor::GetTypedPrimitives(TSet<FPrimitiveComponentId>& OutPrimitives, const TArray<FString>* InCompNames, bool bCollectChildrenVisualizationComponent) const
{
	TArray<TComp*> TypedComponents;
	GetComponents<TComp>(TypedComponents, true);

	for (TComp*& CompIt : TypedComponents)
	{
		if (CompIt)
		{
			if (InCompNames != nullptr)
			{
				// add only comp from names list
				for (const FString& NameIt : (*InCompNames))
				{
					if (CompIt->GetName() == NameIt)
					{
						UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(CompIt);
						if (PrimComp)
						{
							OutPrimitives.Add(PrimComp->ComponentId);
						}

						if (bCollectChildrenVisualizationComponent)
						{
							ImplCollectChildrenVisualizationComponent(OutPrimitives, CompIt);
						}
						break;
					}
				}
			}
			else
			{
				UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(CompIt);
				if (PrimComp)
				{
					OutPrimitives.Add(PrimComp->ComponentId);
				}

				if (bCollectChildrenVisualizationComponent)
				{
					ImplCollectChildrenVisualizationComponent(OutPrimitives, CompIt);
				}
			}

		}
	}
}

bool ADisplayClusterRootActor::FindPrimitivesByName(const TArray<FString>& InNames, TSet<FPrimitiveComponentId>& OutPrimitives)
{
	GetTypedPrimitives<UActorComponent>(OutPrimitives, &InNames, false);

	return true;
}

// Gather components no rendered in game
bool ADisplayClusterRootActor::GetHiddenInGamePrimitives(TSet<FPrimitiveComponentId>& OutPrimitives)
{
	check(IsInGameThread());
	FScopeLock Lock(&InternalsSyncScope);

	OutPrimitives.Empty();

#if WITH_EDITOR

	if (CurrentConfigData)
	{
		//@todo: Add more rules to hide components used in config
		// Hide all static meshes assigned into configuration:
		TArray<FString> WarpMeshNames;
		CurrentConfigData->GetReferencedMeshNames(WarpMeshNames);
		if (WarpMeshNames.Num() > 0)
		{
			GetTypedPrimitives<UStaticMeshComponent>(OutPrimitives, &WarpMeshNames);
		}
	}

	// Hide all visualization components from RootActor
	{
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		for (UPrimitiveComponent* CompIt : PrimitiveComponents)
		{
			if (CompIt->IsVisualizationComponent() || CompIt->bHiddenInGame)
			{
				OutPrimitives.Add(CompIt->ComponentId);
			}

			ImplCollectChildrenVisualizationComponent(OutPrimitives, CompIt);
		}
	}

	// Hide all visualization components from preview scene
	UWorld* CurrentWorld = GetWorld();
	if (CurrentWorld)
	{
		// Iterate over all actors, looking for editor components.
		for (const TWeakObjectPtr<AActor>& WeakActor : FActorRange(CurrentWorld))
		{
			if (AActor* Actor = WeakActor.Get())
			{
				// do not render hiiden in game actors on preview
				bool bActorHideInGame = Actor->IsHidden();

				TArray<UPrimitiveComponent*> PrimitiveComponents;
				Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
				for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
				{
					if (PrimComp->IsVisualizationComponent() || bActorHideInGame || PrimComp->bHiddenInGame)
					{
						OutPrimitives.Add(PrimComp->ComponentId);
					}

					ImplCollectChildrenVisualizationComponent(OutPrimitives, PrimComp);
				}
			}
		}
	}
#endif

	return OutPrimitives.Num() > 0;
}

void ADisplayClusterRootActor::InitializeRootActor()
{
	const UWorld* World = GetWorld();

	if (!World)
	{
		return;
	}
	
	bool bIsPIE = false;

	if (!CurrentConfigData && !ConfigDataName.IsNone())
	{
		// Attempt load from embedded data.
		UpdateConfigDataInstance(GetDefaultConfigDataFromAsset());
	}

	if (ViewportManager.IsValid() == false)
	{
		ViewportManager = MakeUnique<FDisplayClusterViewportManager>();
	}

	// Packaged, PIE and -game runtime
	if (IsRunningGameOrPIE())
	{
		if (CurrentConfigData)
		{
			ResetHierarchyMap();
			BuildHierarchy();

#if WITH_EDITOR
			UpdatePreviewComponents();
#endif
			return;
		}
		
		IPDisplayClusterConfigManager* const ConfigMgr = (GDisplayCluster ? GDisplayCluster->GetPrivateConfigMgr() : nullptr);
		if (ConfigMgr)
		{
			UDisplayClusterConfigurationData* ConfigData = ConfigMgr->GetConfig();
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
		UpdatePreviewConfiguration_Editor();

		if (CurrentConfigData)
		{
			ResetHierarchyMap();
			BuildHierarchy();
			UpdatePreviewComponents();
			return;
		}
	}
#endif
}

bool ADisplayClusterRootActor::BuildHierarchy()
{
	check(CurrentConfigData);
	check(IsInGameThread());

	if (!IsBlueprint())
	{
		// Spawn all components
		SpawnComponents<UDisplayClusterXformComponent, UDisplayClusterConfigurationSceneComponentXform>(CurrentConfigData->Scene->Xforms, XformComponents, AllComponents);
		SpawnComponents<UDisplayClusterCameraComponent, UDisplayClusterConfigurationSceneComponentCamera>(CurrentConfigData->Scene->Cameras, CameraComponents, AllComponents);
		SpawnComponents<UDisplayClusterScreenComponent, UDisplayClusterConfigurationSceneComponentScreen>(CurrentConfigData->Scene->Screens, ScreenComponents, AllComponents);
		SpawnComponents<UDisplayClusterMeshComponent, UDisplayClusterConfigurationSceneComponentMesh>(CurrentConfigData->Scene->Meshes, MeshComponents, AllComponents);

		if (GetWorld())
		{
			// Reregister only needed during an active world. This could be assembled during new asset import when there isn't a world.
			ReregisterAllComponents();
		}
	}
	
	ApplyConfigDataToComponents();
	
	// Check if default camera was specified in command line arguments
	FString DefaultCamId;
	if (FParse::Value(FCommandLine::Get(), DisplayClusterStrings::args::Camera, DefaultCamId))
	{
		DisplayClusterHelpers::str::TrimStringValue(DefaultCamId);
		UE_LOG(LogDisplayClusterGame, Log, TEXT("Default camera from command line arguments: %s"), *DefaultCamId);
		if (GetCameraById(DefaultCamId))
		{
			SetDefaultCamera(DefaultCamId);
		}
	}

	// If no default camera set, try to set the first one
	if (DefaultCameraComponent.IsDefinedSceneComponent() == false)
	{
		TMap<FString, UDisplayClusterCameraComponent*> Cameras;
		GetTypedComponents<UDisplayClusterCameraComponent>(Cameras, CameraComponents);
		if (Cameras.Num() > 0)
		{
			// There is no guarantee that default camera is the first one listed in a config file
			SetDefaultCamera(Cameras.CreateConstIterator()->Key);
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
		ResetHierarchyMap();

		// Invalidate current config as well
		UpdateConfigDataInstance(nullptr);
	}
}

void ADisplayClusterRootActor::ResetHierarchyMap()
{
	XformComponents.Reset();
	CameraComponents.Reset();
	ScreenComponents.Reset();
	MeshComponents.Reset();
	AllComponents.Reset();
}

bool ADisplayClusterRootActor::IsBlueprint() const
{
	for (UClass* Class = GetClass(); Class; Class = Class->GetSuperClass())
	{
		if (Cast<UBlueprintGeneratedClass>(Class) != nullptr || Cast<UDynamicClass>(Class) != nullptr)
		{
			return true;
		}
	}
	
	return false;
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
			UDisplayClusterConfigurationData* ConfigData = ConfigMgr->GetConfig();
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

	InitializeRootActor();
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
				// Depending on the flag state the DCRA follows or not the current player's camera
				if (bFollowLocalPlayerCamera)
				{
					APlayerCameraManager* const CurPlayerCameraManager = CurPlayerController->PlayerCameraManager;
					if (CurPlayerCameraManager)
					{
						SetActorLocationAndRotation(CurPlayerCameraManager->GetCameraLocation(), CurPlayerCameraManager->GetCameraRotation());
					}
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

#if WITH_EDITOR
	// Tick editor preview
	Tick_Editor(DeltaSeconds);
#endif

	Super::Tick(DeltaSeconds);
}

void ADisplayClusterRootActor::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	PostLoad_Editor();
#endif

	InitializeRootActor();
}

void ADisplayClusterRootActor::PostActorCreated()
{
	Super::PostActorCreated();
	InitializeRootActor();
}

void ADisplayClusterRootActor::BeginDestroy()
{
#if WITH_EDITOR
	BeginDestroy_Editor();
#endif

	if (ViewportManager.IsValid())
	{
		// remove runtime data, when actor destroyed
		ViewportManager.Reset();
	}

	ResetHierarchyMap();
	Super::BeginDestroy();
}

void ADisplayClusterRootActor::Destroyed()
{
#if WITH_EDITOR
	Destroyed_Editor();
#endif

	if (ViewportManager.IsValid())
	{
		// remove runtime data, when actor destroyed
		ViewportManager.Reset();
	}

	ResetHierarchyMap();
	Super::Destroyed();
}

void ADisplayClusterRootActor::RerunConstructionScripts()
{
	Super::RerunConstructionScripts();

#if WITH_EDITOR
	RerunConstructionScripts_Editor();
#endif
}

int32 ADisplayClusterRootActor::GetScreensAmount() const
{
	FScopeLock Lock(&InternalsSyncScope);
	TMap<FString, UDisplayClusterScreenComponent*> OutScreens;
	GetTypedComponents<UDisplayClusterScreenComponent>(OutScreens, ScreenComponents);
	return OutScreens.Num();
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
	TMap<FString, UDisplayClusterCameraComponent*> OutCameras;
	GetTypedComponents<UDisplayClusterCameraComponent>(OutCameras, CameraComponents);
	return OutCameras.Num();
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
	check(IsInGameThread());

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
	TMap<FString, UDisplayClusterMeshComponent*> OutMeshes;
	GetTypedComponents<UDisplayClusterMeshComponent>(OutMeshes, MeshComponents);
	return OutMeshes.Num();
}

UStaticMeshComponent* ADisplayClusterRootActor::GetMeshById(const FString& MeshId) const
{
	TArray<UStaticMeshComponent*> MeshComps;
	GetComponents<UStaticMeshComponent>(MeshComps);
	for (UStaticMeshComponent* Comp : MeshComps)
	{
		if (Comp->GetName() == MeshId)
		{
			return Comp;
		}
	}

	return nullptr;
}

void ADisplayClusterRootActor::GetAllMeshes(TMap<FString, UDisplayClusterMeshComponent*>& OutMeshes) const
{
	FScopeLock Lock(&InternalsSyncScope);
	GetTypedComponents<UDisplayClusterMeshComponent>(OutMeshes, MeshComponents);
}

int32 ADisplayClusterRootActor::GetXformsAmount() const
{
	FScopeLock Lock(&InternalsSyncScope);
	TMap<FString, UDisplayClusterXformComponent*> OutXforms;
	GetTypedComponents<UDisplayClusterXformComponent>(OutXforms, XformComponents);
	return OutXforms.Num();
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
	TMap<FString, UDisplayClusterSceneComponent*> OutComponents;
	GetTypedComponents<UDisplayClusterSceneComponent>(OutComponents, AllComponents);
	return OutComponents.Num();
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
			// Check if this component is already stored on the BP.
			TComp* OurComponent = Cast<TComp>(GetComponentById(it.Key));
			if (!OurComponent)
			{
				// Create it now.
				TComp* NewComponent = NewObject<TComp>(this, FName(*it.Key));
#if !WITH_EDITOR
				NewComponent->SetFlags(EObjectFlags::RF_DuplicateTransient | RF_Transient | RF_TextExportTransient);
#endif
				NewComponent->AttachToComponent(RootComponent, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));

				OurComponent = NewComponent;
			}
			
			OurComponent->SetConfigParameters(it.Value);

			// Save references
			FDisplayClusterSceneComponentRef* NewComponentRef = new FDisplayClusterSceneComponentRef(OurComponent);
			OutAllMap.Emplace(it.Key, NewComponentRef);
			OutTypedMap.Emplace(it.Key, NewComponentRef);
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

	// Search components included in the BP.
	TArray<TComp*> NativeComps;
	GetComponents<TComp>(NativeComps);
	for (TComp* Comp : NativeComps)
	{
		if (!OutTypedMap.Contains(Comp->GetName()))
		{
			OutTypedMap.Add(Comp->GetName(), Comp);
		}
	}
}

template <typename TComp>
TComp* ADisplayClusterRootActor::GetTypedComponentById(const FString& ComponentId, const TMap<FString, FDisplayClusterSceneComponentRef*>& InTypedMap) const
{
	if (InTypedMap.Contains(ComponentId))
	{
		if (TComp* FoundComp = Cast<TComp>(InTypedMap[ComponentId]->GetOrFindSceneComponent()))
		{
			return FoundComp;
		}
	}

	// Search components included in the BP.
	TArray<TComp*> NativeComps;
	GetComponents<TComp>(NativeComps);
	for (TComp* Comp : NativeComps)
	{
		if (Comp->GetName() == ComponentId)
		{
			return Comp;
		}
	}

	return nullptr;
}
