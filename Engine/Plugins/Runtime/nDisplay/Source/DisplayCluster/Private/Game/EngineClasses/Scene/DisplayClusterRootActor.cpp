// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRootActor.h"

#include "Components/SceneComponent.h"
#include "Components/DisplayClusterOriginComponent.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "Components/DisplayClusterSceneComponentSyncParent.h"
#include "Components/DisplayClusterPreviewComponent.h"
#include "Components/DisplayClusterSyncTickComponent.h"
#include "Components/DisplayClusterICVFXCameraComponent.h"
#include "Components/DisplayClusterSceneComponentSyncThis.h"
#include "CineCameraComponent.h"

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
#include "Game/EngineClasses/Scene/DisplayClusterRootActorInitializer.h"


ADisplayClusterRootActor::ADisplayClusterRootActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, OperationMode(EDisplayClusterOperationMode::Disabled)
{
	/*
	 * Origin component
	 *
	 * We HAVE to store the origin (root) component in our own UPROPERTY marked visible.
	 * Live link has a property which maintains a component reference. Live link sets this
	 * through their details panel automatically, which unreal validates in
	 * FComponentReferenceCustomization::IsComponentReferenceValid.
	 *
	 * Unreal won't allow native components that don't have CPF_Edit to be set. Luckily
	 * they search the owning class for a property containing the component.
	 */
	{
		DisplayClusterRootComponent = CreateDefaultSubobject<UDisplayClusterOriginComponent>(TEXT("RootComponent"));
		SetRootComponent(DisplayClusterRootComponent);
	}

	// A helper component to trigger nDisplay Tick() during Tick phase
	SyncTickComponent = CreateDefaultSubobject<UDisplayClusterSyncTickComponent>(TEXT("DisplayClusterSyncTick"));

	// Default nDisplay camera
	DefaultViewPoint = CreateDefaultSubobject<UDisplayClusterCameraComponent>(TEXT("DefaultViewPoint"));
	DefaultViewPoint->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	DefaultViewPoint->SetRelativeLocation(FVector(0.f, 0.f, 50.f));

	ViewportManager = MakeUnique<FDisplayClusterViewportManager>();

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = ETickingGroup::TG_PostUpdateWork;

	bFindCameraComponentWhenViewTarget = false;
	bReplicates = false;
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

#if WITH_EDITOR
	Constructor_Editor();
#endif
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

const FDisplayClusterConfigurationICVFX_StageSettings& ADisplayClusterRootActor::GetStageSettings() const
{
	check(CurrentConfigData);

	return CurrentConfigData->StageSettings;
}

const FDisplayClusterConfigurationRenderFrame& ADisplayClusterRootActor::GetRenderFrameSettings() const 
{ 
	check(CurrentConfigData);

	return CurrentConfigData->RenderFrameSettings;
}

void ADisplayClusterRootActor::InitializeFromConfig(UDisplayClusterConfigurationData* ConfigData)
{
	if (ConfigData)
	{
		// Store new config data object
		UpdateConfigDataInstance(ConfigData, true);

		BuildHierarchy();

#if WITH_EDITOR
		if (GIsEditor && GetWorld())
		{
			UpdatePreviewComponents();
		}
#endif
	}
}

void ADisplayClusterRootActor::OverrideFromConfig(UDisplayClusterConfigurationData* ConfigData)
{
	check(ConfigData);
	check(ConfigData->Scene);
	check(ConfigData->Cluster);

	// Override base types and structures
	CurrentConfigData->Meta = ConfigData->Meta;
	CurrentConfigData->Info = ConfigData->Info;
	CurrentConfigData->CustomParameters = ConfigData->CustomParameters;
	CurrentConfigData->Diagnostics = ConfigData->Diagnostics;
	CurrentConfigData->bFollowLocalPlayerCamera = ConfigData->bFollowLocalPlayerCamera;
	CurrentConfigData->bExitOnEsc = ConfigData->bExitOnEsc;

	// Override Scene but without changing its name
	{
		FName SceneName = NAME_None;

		if (CurrentConfigData->Scene)
		{
			SceneName = CurrentConfigData->Scene->GetFName();

			const FName DeadName = MakeUniqueObjectName(CurrentConfigData, UDisplayClusterConfigurationScene::StaticClass(), "DEAD_DisplayClusterConfigurationScene");
			CurrentConfigData->Scene->Rename(*DeadName.ToString());
		}

		CurrentConfigData->Scene = DuplicateObject(ConfigData->Scene, CurrentConfigData, SceneName);
	}

	// Override Cluster but without changing its name
	{
		FName ClusterName = NAME_None;

		if (CurrentConfigData->Cluster)
		{
			ClusterName = CurrentConfigData->Cluster->GetFName();

			const FName DeadName = MakeUniqueObjectName(CurrentConfigData, UDisplayClusterConfigurationCluster::StaticClass(), "DEAD_DisplayClusterConfigurationCluster");
			CurrentConfigData->Cluster->Rename(*DeadName.ToString());
		}

		CurrentConfigData->Cluster = DuplicateObject(ConfigData->Cluster, CurrentConfigData, ClusterName);
	}

	// There is no sense to call BuildHierarchy because it works for non-BP root actors.
	// On the other hand, OverwriteFromConfig method is called for BP root actors only by nature.

	// And update preview stuff in Editor
#if WITH_EDITOR
	if (GIsEditor && GetWorld())
	{
		UpdatePreviewComponents();
	}
#endif
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

			if (CurrentConfigData->Scene == nullptr)
			{
				CurrentConfigData->Scene = NewObject<UDisplayClusterConfigurationScene>(
					CurrentConfigData,
					UDisplayClusterConfigurationScene::StaticClass(),
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

bool ADisplayClusterRootActor::IsInnerFrustumEnabled(const FString& InnerFrustumID) const
{
	// add more GUI rules here
	// Inner Frustum Enabled
	//  Camera_1  [ ]
	//  Camera_2  [X]
	//  Camera_3  [X]

	return true;
}

int ADisplayClusterRootActor::GetInnerFrustumPriority(const FString& InnerFrustumID) const
{
	int Order = 100000;
	for (const FDisplayClusterComponentRef& It : InnerFrustumPriority)
	{
		if (It.Name.Compare(InnerFrustumID, ESearchCase::IgnoreCase) == 0)
		{
			return Order;
		}
		Order--;
	}

	return -1;
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

	OutPrimitives.Empty();

	if (CurrentConfigData)
	{
		// Add warp meshes assigned in the configuration into hide lists
		TArray<FString> WarpMeshNames;
		CurrentConfigData->GetReferencedMeshNames(WarpMeshNames);
		if (WarpMeshNames.Num() > 0)
		{
			GetTypedPrimitives<UStaticMeshComponent>(OutPrimitives, &WarpMeshNames);
		}
	}

#if WITH_EDITOR

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
			BuildHierarchy();

#if WITH_EDITOR
			UpdatePreviewComponents();
#endif
			return;
		}
	}
#if WITH_EDITOR
	// Initialize from file property by default in Editor
	else
	{
		if (CurrentConfigData)
		{
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
		// Temporary solution. The whole initialization stuff has been moved to a separate initialization class. Since
		// it won't be possible to configure any components in a config file, and the proper asset initialization will
		// be performed on the configurator side, the DCRA won't need to have any custom logic around the components.
		TUniquePtr<FDisplayClusterRootActorInitializer> Initializer = MakeUnique<FDisplayClusterRootActorInitializer>();
		Initializer->Initialize(this, CurrentConfigData);
	}

	return true;
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

void ADisplayClusterRootActor::Tick(float DeltaSeconds)
{
	// Update saved DeltaSeconds for root actor
	LastDeltaSecondsValue = DeltaSeconds;

	if (OperationMode == EDisplayClusterOperationMode::Cluster ||
		OperationMode == EDisplayClusterOperationMode::Editor)
	{
		UWorld* const CurWorld = GetWorld();
		if (CurWorld && CurrentConfigData)
		{
			APlayerController* const CurPlayerController = CurWorld->GetFirstPlayerController();
			if (CurPlayerController)
			{
				// Depending on the flag state the DCRA follows or not the current player's camera
				if (CurrentConfigData->bFollowLocalPlayerCamera)
				{
					APlayerCameraManager* const CurPlayerCameraManager = CurPlayerController->PlayerCameraManager;
					if (CurPlayerCameraManager)
					{
						SetActorLocationAndRotation(CurPlayerCameraManager->GetCameraLocation(), CurPlayerCameraManager->GetCameraRotation());
					}
				}

				if (CurrentConfigData->bExitOnEsc)
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

	ViewportManager.Reset();

	Super::BeginDestroy();
}

void ADisplayClusterRootActor::RerunConstructionScripts()
{
	IDisplayClusterConfiguration& Config = IDisplayClusterConfiguration::Get();
	if (!Config.IsTransactingSnapshot())
	{
		Super::RerunConstructionScripts();
#if WITH_EDITOR
		RerunConstructionScripts_Editor();
#endif
	}
}

UDisplayClusterCameraComponent* ADisplayClusterRootActor::GetDefaultCamera() const
{
	return DefaultViewPoint;
}

bool ADisplayClusterRootActor::SetReplaceTextureFlagForAllViewports(bool bReplace)
{
	IDisplayCluster& Display = IDisplayCluster::Get();

	UDisplayClusterConfigurationData* ConfigData = GetConfigData();

	if (!ConfigData)
	{
		UE_LOG(LogDisplayClusterGame, Warning, TEXT("RootActor's ConfigData was null"));
		return false;
	}

	const FString NodeId = Display.GetClusterMgr()->GetNodeId();
	const UDisplayClusterConfigurationClusterNode* Node = ConfigData->GetClusterNode(NodeId);

	if (Node)
	{
		for (const TPair<FString, UDisplayClusterConfigurationViewport*>& ViewportItem : Node->Viewports)
		{
			if (ViewportItem.Value)
			{
				ViewportItem.Value->RenderSettings.Replace.bAllowReplace = bReplace;
			}
		}
	}
	else if (ConfigData->Cluster)
	{
		for (const TPair<FString, UDisplayClusterConfigurationClusterNode*>& NodeItem : ConfigData->Cluster->Nodes)
		{
			if (!NodeItem.Value)
			{
				continue;
			}

			for (const TPair<FString, UDisplayClusterConfigurationViewport*>& ViewportItem : NodeItem.Value->Viewports)
			{
				if (ViewportItem.Value)
				{
					ViewportItem.Value->RenderSettings.Replace.bAllowReplace = bReplace;
				}
			}
		}
	}
	else
	{
		UE_LOG(LogDisplayClusterGame, Warning, TEXT("ConfigData's Cluster was null"));
		return false;
	}

	return true;
}
