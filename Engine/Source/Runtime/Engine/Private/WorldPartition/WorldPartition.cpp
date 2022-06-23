// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	WorldPartition.cpp: UWorldPartition implementation
=============================================================================*/
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionLog.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartition/WorldPartitionLevelStreamingPolicy.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/WorldPartitionReplay.h"
#include "Algo/Accumulate.h"
#include "Algo/Transform.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/WorldSettings.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "UObject/UE5MainStreamObjectVersion.h"

#if WITH_EDITOR
#include "LevelUtils.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Selection.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "LevelEditorViewport.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/ScopeExit.h"
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"
#include "UnrealEdMisc.h"
#include "LocationVolume.h"
#include "WorldPartition/IWorldPartitionEditorModule.h"
#include "WorldPartition/WorldPartitionLevelHelper.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionEditorPerProjectUserSettings.h"
#include "WorldPartition/WorldPartitionMiniMap.h"
#include "WorldPartition/WorldPartitionMiniMapHelper.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterActorList.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#include "WorldPartition/WorldPartitionActorDescViewProxy.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "Modules/ModuleManager.h"
#include "GameDelegates.h"
#include "IDocumentation.h"

static int32 GLoadingRangeBugItGo = 12800;
static FAutoConsoleVariableRef CVarLoadingRangeBugItGo(
	TEXT("wp.Editor.LoadingRangeBugItGo"),
	GLoadingRangeBugItGo,
	TEXT("Loading range for BugItGo command."),
	ECVF_Default
);
#endif //WITH_EDITOR

#define LOCTEXT_NAMESPACE "WorldPartition"

#if WITH_EDITOR
TMap<FName, FString> GetDataLayersDumpString(const UWorldPartition* WorldPartition)
{
	TMap<FName, FString> DataLayersDumpString;
	const UDataLayerSubsystem* DataLayerSubsystem = UWorld::GetSubsystem<UDataLayerSubsystem>(WorldPartition->GetWorld());
	DataLayerSubsystem->ForEachDataLayer([&DataLayersDumpString](const UDataLayerInstance* DataLayer)
	{
		DataLayersDumpString.FindOrAdd(DataLayer->GetDataLayerFName()) = FString::Format(TEXT("{0}{1})"), { DataLayer->GetDataLayerShortName(), DataLayer->GetDataLayerFName().ToString() });
		return true;
	});
	
	return DataLayersDumpString;
}

FString GetActorDescDumpString(const FWorldPartitionActorDesc* ActorDesc, const TMap<FName, FString>& DataLayersDumpString)
{
	auto GetDataLayerString = [&DataLayersDumpString](const TArray<FName>& DataLayerNames)
	{
		if (DataLayerNames.IsEmpty())
		{
			return FString("None");
		}

		return FString::JoinBy(DataLayerNames, TEXT(", "), [&DataLayersDumpString](const FName& DataLayerName) 
		{ 
			if (const FString* DumpString = DataLayersDumpString.Find(DataLayerName))
			{
				return *DumpString;
			}
			return DataLayerName.ToString(); 
		});
	};

	check(ActorDesc);
	return FString::Printf(
		TEXT("%s DataLayerNames:%s") LINE_TERMINATOR, 
		*ActorDesc->ToString(FWorldPartitionActorDesc::EToStringMode::Full), 
		*GetDataLayerString(ActorDesc->GetDataLayerInstanceNames())
	);
}

static FAutoConsoleCommand DumpActorDesc(
	TEXT("wp.Editor.DumpActorDesc"),
	TEXT("Dump a specific actor descriptor on the console."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		TArray<FString> ActorPaths;
		if (Args.Num() > 0)
		{
			ActorPaths.Add(Args[0]);
		}
		else
		{
			for (FSelectionIterator SelectionIt(*GEditor->GetSelectedActors()); SelectionIt; ++SelectionIt)
			{
				if (const AActor* Actor = CastChecked<AActor>(*SelectionIt))
				{
					ActorPaths.Add(Actor->GetPathName());
				}
			}
		}

		if (!ActorPaths.IsEmpty())
		{
			if (UWorld* World = GEditor->GetEditorWorldContext().World())
			{
				if (!World->IsGameWorld())
				{
					if (UWorldPartition* WorldPartition = World->GetWorldPartition())
					{
						TMap<FName, FString> DataLayersDumpString = GetDataLayersDumpString(WorldPartition);
						for (const FString& ActorPath : ActorPaths)
						{
							if (const FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(ActorPath))
							{
								UE_LOG(LogWorldPartition, Log, TEXT("%s"), *GetActorDescDumpString(ActorDesc, DataLayersDumpString));
							}
						}
					}
				}
			}
		}
	})
);

static FAutoConsoleCommand DumpActorDescs(
	TEXT("wp.Editor.DumpActorDescs"),
	TEXT("Dump the list of actor descriptors in a CSV file."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() > 0)
		{
			if (UWorld* World = GEditor->GetEditorWorldContext().World())
			{
				if (!World->IsGameWorld())
				{
					if (UWorldPartition* WorldPartition = World->GetWorldPartition())
					{
						WorldPartition->DumpActorDescs(Args[0]);
					}
				}
			}
		}
	})
);

static FAutoConsoleCommand SetLogWorldPartitionVerbosity(
	TEXT("wp.Editor.SetLogWorldPartitionVerbosity"),
	TEXT("Change the WorldPartition verbosity log verbosity."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() == 1)
		{
			if (Args[0].Contains(TEXT("Verbose")))
			{
				LogWorldPartition.SetVerbosity(ELogVerbosity::Verbose);
			}
			else
			{
				LogWorldPartition.SetVerbosity(LogWorldPartition.GetCompileTimeVerbosity());
			}
		}
	})
);

class FLoaderAdapterAlwaysLoadedActors : public FLoaderAdapterShape
{
public:
	FLoaderAdapterAlwaysLoadedActors(UWorld* InWorld)
		: FLoaderAdapterShape(InWorld, FBox(FVector(-HALF_WORLD_MAX, -HALF_WORLD_MAX, -HALF_WORLD_MAX), FVector(HALF_WORLD_MAX, HALF_WORLD_MAX, HALF_WORLD_MAX)), TEXT("Always Loaded"))
	{
		bIncludeSpatiallyLoadedActors = false;
		bIncludeNonSpatiallyLoadedActors = true;
	}
};
#endif

UWorldPartition::UWorldPartition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, EditorHash(nullptr)
	, AlwaysLoadedActors(nullptr)
	, PinnedActors(nullptr)
	, WorldPartitionEditor(nullptr)
	, bEnableStreaming(true)
	, bStreamingWasEnabled(true)
	, bShouldEnableStreamingWarned(false)
	, bShouldCheckEnableStreamingWarning(false)
	, bSupportsStreaming(true)
	, bCanBeUsedByLevelInstance(false)
	, bForceGarbageCollection(false)
	, bForceGarbageCollectionPurge(false)
	, bIsPIE(false)
#endif
	, InitState(EWorldPartitionInitState::Uninitialized)
	, StreamingPolicy(nullptr)
	, Replay(nullptr)
{
#if WITH_EDITOR
	WorldPartitionStreamingPolicyClass = UWorldPartitionLevelStreamingPolicy::StaticClass();
#endif
}

#if WITH_EDITOR
void UWorldPartition::OnGCPostReachabilityAnalysis()
{
	const TIndirectArray<FWorldContext>& WorldContextList = GEngine->GetWorldContexts();

	// Avoid running this process while a game world is live
	for (const FWorldContext& WorldContext : WorldContextList)
	{
		if (WorldContext.World() != nullptr && WorldContext.World()->IsGameWorld())
		{
			return;
		}
	}

	for (FRawObjectIterator It; It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(static_cast<UObject*>(It->Object)))
		{
			if (Actor->IsUnreachable() && !Actor->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists) && Actor->IsMainPackageActor())
			{
				ForEachObjectWithPackage(Actor->GetPackage(), [this, Actor](UObject* Object)
				{
					if (Object->HasAnyFlags(RF_Standalone))
					{
						UE_LOG(LogWorldPartition, Warning, TEXT("Actor %s is unreachable without properly detaching object %s in its package"), *Actor->GetPathName(), *Object->GetPathName());

						Object->ClearFlags(RF_Standalone);

						// Make sure we trigger a second GC at the next tick to properly destroy packages there were fixed in this pass
						bForceGarbageCollection = true;
						bForceGarbageCollectionPurge = true;
					}
					return true;
				}, false);
			}
		}
	}
}

void UWorldPartition::OnPackageDirtyStateChanged(UPackage* Package)
{
	if (AActor* Actor = AActor::FindActorInPackage(Package); ShouldHandleActorEvent(Actor))
	{
		if (FWorldPartitionHandle ActorHandle(this, Actor->GetActorGuid()); ActorHandle.IsValid())
		{
			if (Package->IsDirty())
			{
				DirtyActors.Add(ActorHandle.ToReference(), Actor);
			}
			else
			{
				// If we hold the last reference to that actor (or no reference are held at all), pin it to avoid unloading
				if (PinnedActors && (ActorHandle->GetHardRefCount() <= 1))
				{
					PinnedActors->AddActors({ ActorHandle });
				}

				DirtyActors.Remove(ActorHandle.ToReference());
			}
		}
	}
}

void UWorldPartition::OnPreBeginPIE(bool bStartSimulate)
{
	check(!bIsPIE);
	bIsPIE = true;

	OnBeginPlay();
}

void UWorldPartition::OnPrePIEEnded(bool bWasSimulatingInEditor)
{
	check(bIsPIE);
	bIsPIE = false;
}

void UWorldPartition::OnBeginPlay()
{
	GenerateStreaming();
	RuntimeHash->OnBeginPlay();
}

void UWorldPartition::OnCancelPIE()
{
	// No check here since CancelPIE can be called after PrePIEEnded
	bIsPIE = false;
	// Call OnEndPlay here since EndPlayMapDelegate is not called when cancelling PIE
	OnEndPlay();
}

void UWorldPartition::OnEndPlay()
{
	FlushStreaming();
	RuntimeHash->OnEndPlay();
}

FName UWorldPartition::GetWorldPartitionEditorName() const
{
	if (SupportsStreaming())
	{
		return EditorHash->GetWorldPartitionEditorName();
	}
	return NAME_None;
}
#endif

void UWorldPartition::Initialize(UWorld* InWorld, const FTransform& InTransform)
{
	UE_SCOPED_TIMER(TEXT("WorldPartition initialize"), LogWorldPartition, Display);
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartition::Initialize);

	check(!World || (World == InWorld));
	if (!ensure(!IsInitialized()))
	{
		return;
	}

	if (IsTemplate())
	{
		return;
	}

	check(InWorld);
	World = InWorld;

	if (!InTransform.Equals(FTransform::Identity))
	{
		InstanceTransform = InTransform;
	}

	check(InitState == EWorldPartitionInitState::Uninitialized);
	InitState = EWorldPartitionInitState::Initializing;

	UWorld* OuterWorld = GetTypedOuter<UWorld>();
	check(OuterWorld);

	RegisterDelegates();

	if (IsMainWorldPartition())
	{
		AWorldPartitionReplay::Initialize(World);
	}

#if WITH_EDITOR
	const bool bIsGame = IsRunningGame();
	const bool bIsEditor = !World->IsGameWorld();
	const bool bIsCooking = IsRunningCookCommandlet();
	const bool bIsDedicatedServer = IsRunningDedicatedServer();
	const bool bPIEWorldTravel = (World->WorldType == EWorldType::PIE) && !StreamingPolicy;

	UE_LOG(LogWorldPartition, Log, TEXT("UWorldPartition::Initialize(IsEditor=%d, bPIEWorldTravel=%d IsGame=%d, IsCooking=%d)"), bIsEditor ? 1 : 0, bPIEWorldTravel ? 1 : 0, bIsGame ? 1 : 0, bIsCooking ? 1 : 0);

	if (bEnableStreaming)
	{
		bStreamingWasEnabled = true;
	}

	if (bIsGame || bIsCooking)
	{
		// Don't rely on the editor hash for cooking or -game
		EditorHash = nullptr;
		AlwaysLoadedActors = nullptr;
	}
	else if (bIsEditor)
	{
		CreateOrRepairWorldPartition(OuterWorld->GetWorldSettings());

		check(!StreamingPolicy);
		check(EditorHash);

		EditorHash->Initialize();

		AlwaysLoadedActors = new FLoaderAdapterAlwaysLoadedActors(OuterWorld);
		PinnedActors = new FLoaderAdapterActorList(OuterWorld);
	}

	check(RuntimeHash);
	RuntimeHash->SetFlags(RF_Transactional);

	if (bIsEditor || bIsGame || bPIEWorldTravel || bIsDedicatedServer)
	{
		UPackage* LevelPackage = OuterWorld->PersistentLevel->GetOutermost();

		// Duplicated worlds (ex: WorldPartitionRenameDuplicateBuilder) will not have a loaded path 
		const FName PackageName = LevelPackage->GetLoadedPath().GetPackageFName().IsNone() ? LevelPackage->GetFName() : LevelPackage->GetLoadedPath().GetPackageFName();

		// Currently known Instancing use cases:
		// - World Partition map template (New Level)
		// - PIE World Travel
		FString SourceWorldPath, RemappedWorldPath;
		const bool bIsInstanced = OuterWorld->GetSoftObjectPathMapping(SourceWorldPath, RemappedWorldPath);
	
		if (bIsInstanced)
		{
			InstancingContext.AddMapping(PackageName, LevelPackage->GetFName());
			InstancingSoftObjectPathFixupArchive.Reset(new FSoftObjectPathFixupArchive(SourceWorldPath, RemappedWorldPath));
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UActorDescContainer::Initialize);
			UActorDescContainer::Initialize(World, PackageName);
			check(bContainerInitialized);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UActorDescContainer::Hash);
			for (FActorDescList::TIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
			{
				if (bIsInstanced)
				{
					const FString LongActorPackageName = ActorDescIterator->GetActorPackage().ToString();
					const FString InstancedName = ULevel::GetExternalActorPackageInstanceName(LevelPackage->GetName(), LongActorPackageName);

					InstancingContext.AddMapping(*LongActorPackageName, *InstancedName);

					ActorDescIterator->TransformInstance(SourceWorldPath, RemappedWorldPath, GetInstanceTransform());
				}

				ActorDescIterator->bIsForcedNonSpatiallyLoaded = !IsStreamingEnabled();

				if (bIsEditor && !bIsCooking)
				{
					HashActorDesc(*ActorDescIterator);
				}
			}
		}
	}

	if (bIsEditor || bIsGame)
	{
		// Here we need to flush any async loading before starting any synchronous load (mixing synchronous and asynchronous load on the same package is not handled properly).
		// Also, this will ensure that FindObject will work on an external actor that was loading asynchronously.
		FlushAsyncLoading();

		// Make sure to preload only AWorldDataLayers actor first
		for (UActorDescContainer::TIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
		{
			if (ActorDescIterator->GetActorNativeClass()->IsChildOf<AWorldDataLayers>())
			{
				WorldDataLayersActor = FWorldPartitionReference(this, ActorDescIterator->GetGuid());
				break;
			}
		}
	}
	
	if (bIsEditor)
	{
		// Repair existing maps with no UWorld::WorldDataLayers actor.
		AWorldDataLayers* WorldDataLayers = OuterWorld->GetWorldDataLayers();
		if (!WorldDataLayers)
		{
			// WorldDataLayersActor is invalid here orelse its PostLoad would have set itself as the world's WorldDataLayers
			check(!WorldDataLayersActor.IsValid());
			WorldDataLayers = AWorldDataLayers::Create(OuterWorld);
			OuterWorld->SetWorldDataLayers(WorldDataLayers);
		}

		// Apply level transform on actors already part of the level
		if (!GetInstanceTransform().Equals(FTransform::Identity))
		{
			check(!OuterWorld->PersistentLevel->bAlreadyMovedActors);
			for (AActor* Actor : OuterWorld->PersistentLevel->Actors)
			{
				if (Actor)
				{
					FLevelUtils::FApplyLevelTransformParams TransformParams(Actor->GetLevel(), GetInstanceTransform());
					TransformParams.Actor = Actor;
					TransformParams.bDoPostEditMove = true;
					FLevelUtils::ApplyLevelTransform(TransformParams);
				}
			}
			// Flag Level's bAlreadyMovedActors to true so that ULevelStreaming::PrepareLoadedLevel won't reapply the same transform again.
			OuterWorld->PersistentLevel->bAlreadyMovedActors = true;
		}
	}

	if (bIsEditor && !bIsCooking)
	{
		// Load the always loaded cell, don't call LoadCells to avoid creating a transaction
		AlwaysLoadedActors->Load();

		// Load more cells depending on the user's settings
		// Skipped when running from a commandlet and for subpartitions
		if (IsMainWorldPartition() && !IsRunningCommandlet())
		{
			// Load last loaded regions
			if (GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetEnableLoadingOfLastLoadedRegions())
			{
				LoadLastLoadedRegions();
			}
		}
	}
#endif //WITH_EDITOR

	InitState = EWorldPartitionInitState::Initialized;

#if WITH_EDITOR
	if (!bIsEditor)
	{
		if (bIsGame || bPIEWorldTravel || bIsDedicatedServer)
		{
			if (bPIEWorldTravel)
			{
				check(!bIsPIE);
				bIsPIE = true;
			}

			if (StreamingPolicy)
			{
				UE_LOG(LogWorldPartition, Warning, TEXT("StreamingPolicy was set when initializing the world partition object"));
				StreamingPolicy = nullptr;
			}

			OnBeginPlay();
		}

		// Apply remapping of Persistent Level's SoftObjectPaths
		FWorldPartitionLevelHelper::RemapLevelSoftObjectPaths(OuterWorld->PersistentLevel, this);
	}
#endif

	FWorldPartitionEvents::BroadcastWorldPartitionInitialized(World, this);
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	OnWorldPartitionInitialized.Broadcast(this);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UWorldPartition::Uninitialize()
{
	if (IsInitialized())
	{
		check(World);

		InitState = EWorldPartitionInitState::Uninitializing;

		UnregisterDelegates();
		
		// Unload all loaded cells
		if (World->IsGameWorld())
		{
			UpdateStreamingState();
		}

#if WITH_EDITOR
		if (IsMainWorldPartition())
		{
			SavePerUserSettings();
		}

		if (World->IsGameWorld())
		{
			OnEndPlay();
		}
		
		if (AlwaysLoadedActors)
		{
			delete AlwaysLoadedActors;
			AlwaysLoadedActors = nullptr;
		}

		if (PinnedActors)
		{
			delete PinnedActors;
			PinnedActors = nullptr;
		}

		if (RegisteredEditorLoaderAdapters.Num())
		{
			for (UWorldPartitionEditorLoaderAdapter* RegisteredEditorLoaderAdapter : RegisteredEditorLoaderAdapters)
			{
				RegisteredEditorLoaderAdapter->Release();
			}

			RegisteredEditorLoaderAdapters.Empty();
		}

		WorldDataLayersActor = FWorldPartitionReference();

		DirtyActors.Empty();

		EditorHash = nullptr;
#endif		

		InitState = EWorldPartitionInitState::Uninitialized;

		FWorldPartitionEvents::BroadcastWorldPartitionUninitialized(World, this);
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		OnWorldPartitionUninitialized.Broadcast(this);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	Super::Uninitialize();
}

bool UWorldPartition::IsInitialized() const
{
	return InitState == EWorldPartitionInitState::Initialized;
}

bool UWorldPartition::CanStream() const
{
	// WorldPartition can't stream if not initialized or it it's part of a partitioned 
	// sub-level that was removed from its owning world.
	return IsInitialized() && (GetTypedOuter<UWorld>()->PersistentLevel->GetWorld() != nullptr);
}

bool UWorldPartition::IsMainWorldPartition() const
{
	check(World);
	return World == GetTypedOuter<UWorld>();
}

void UWorldPartition::OnPostBugItGoCalled(const FVector& Loc, const FRotator& Rot)
{
#if WITH_EDITOR
	if (GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetBugItGoLoadRegion())
	{
		const FVector LoadExtent(GLoadingRangeBugItGo, GLoadingRangeBugItGo, HALF_WORLD_MAX);
		const FBox LoadCellsBox(Loc - LoadExtent, Loc + LoadExtent);

		UWorldPartitionEditorLoaderAdapter* EditorLoaderAdapter = CreateEditorLoaderAdapter<FLoaderAdapterShape>(World, LoadCellsBox, TEXT("BugItGo"));
		EditorLoaderAdapter->GetLoaderAdapter()->Load();
	}
#endif
}

void UWorldPartition::RegisterDelegates()
{
	check(World); 

#if WITH_EDITOR
	if (GEditor && !IsTemplate() && !World->IsGameWorld())
	{
		if (IsMainWorldPartition())
		{
			FEditorDelegates::PreBeginPIE.AddUObject(this, &UWorldPartition::OnPreBeginPIE);
			FEditorDelegates::PrePIEEnded.AddUObject(this, &UWorldPartition::OnPrePIEEnded);
			FEditorDelegates::CancelPIE.AddUObject(this, &UWorldPartition::OnCancelPIE);
			FGameDelegates::Get().GetEndPlayMapDelegate().AddUObject(this, &UWorldPartition::OnEndPlay);
			FCoreUObjectDelegates::PostReachabilityAnalysis.AddUObject(this, &UWorldPartition::OnGCPostReachabilityAnalysis);
			GEditor->OnPostBugItGoCalled().AddUObject(this, &UWorldPartition::OnPostBugItGoCalled);
			GEditor->OnEditorClose().AddUObject(this, &UWorldPartition::SavePerUserSettings);

			if (!IsRunningCommandlet())
			{
				UPackage::PackageDirtyStateChangedEvent.AddUObject(this, &UWorldPartition::OnPackageDirtyStateChanged);
			}
		}
	}
#endif

	if (World->IsGameWorld())
	{
		if (IsMainWorldPartition())
		{
			World->OnWorldMatchStarting.AddUObject(this, &UWorldPartition::OnWorldMatchStarting);

#if !UE_BUILD_SHIPPING
			FCoreDelegates::OnGetOnScreenMessages.AddUObject(this, &UWorldPartition::GetOnScreenMessages);
#endif
		}
		else
		{
			FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UWorldPartition::OnLevelRemovedFromWorld);
		}
	}
}

void UWorldPartition::UnregisterDelegates()
{
	check(World);

#if WITH_EDITOR
	if (GEditor && !IsTemplate() && !World->IsGameWorld())
	{
		if (IsMainWorldPartition())
		{
			FEditorDelegates::PreBeginPIE.RemoveAll(this);
			FEditorDelegates::PrePIEEnded.RemoveAll(this);
			FEditorDelegates::CancelPIE.RemoveAll(this);
			FGameDelegates::Get().GetEndPlayMapDelegate().RemoveAll(this);

			if (!IsEngineExitRequested())
			{
				FCoreUObjectDelegates::PostReachabilityAnalysis.RemoveAll(this);
			}

			GEditor->OnPostBugItGoCalled().RemoveAll(this);
			GEditor->OnEditorClose().RemoveAll(this);

			if (!IsRunningCommandlet())
			{
				UPackage::PackageDirtyStateChangedEvent.RemoveAll(this);
			}
		}
	}
#endif

	if (World->IsGameWorld())
	{
		if (IsMainWorldPartition())
		{
			World->OnWorldMatchStarting.RemoveAll(this);

#if !UE_BUILD_SHIPPING
			FCoreDelegates::OnGetOnScreenMessages.RemoveAll(this);
#endif
		}
		else
		{
			FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
		}
	}
}

void UWorldPartition::OnLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld)
{
	check(!IsMainWorldPartition());
	if ((World == InWorld) && (InLevel == GetTypedOuter<UWorld>()->PersistentLevel))
	{
		check(!CanStream());
		UpdateStreamingState();
	}
}

#if !UE_BUILD_SHIPPING
void UWorldPartition::GetOnScreenMessages(FCoreDelegates::FSeverityMessageMap& OutMessages)
{
	if (StreamingPolicy)
	{
		StreamingPolicy->GetOnScreenMessages(OutMessages);
	}
}
#endif

void UWorldPartition::OnWorldMatchStarting()
{
	check(GetWorld()->IsGameWorld());
	// Wait for any level streaming to complete
	GetWorld()->BlockTillLevelStreamingCompleted();
}

#if WITH_EDITOR
UWorldPartition* UWorldPartition::CreateOrRepairWorldPartition(AWorldSettings* WorldSettings, TSubclassOf<UWorldPartitionEditorHash> EditorHashClass, TSubclassOf<UWorldPartitionRuntimeHash> RuntimeHashClass)
{
	UWorld* World = WorldSettings->GetWorld();
	UWorldPartition* WorldPartition = WorldSettings->GetWorldPartition();

	if (!WorldPartition)
	{
		WorldPartition = NewObject<UWorldPartition>(WorldSettings);
		WorldSettings->SetWorldPartition(WorldPartition);

		// New maps should include GridSize in name
		WorldSettings->bIncludeGridSizeInNameForFoliageActors = true;
		WorldSettings->bIncludeGridSizeInNameForPartitionedActors = true;

		if (IWorldPartitionEditorModule* WorldPartitionEditorModulePtr = FModuleManager::GetModulePtr<IWorldPartitionEditorModule>("WorldPartitionEditor"))
		{
			WorldSettings->InstancedFoliageGridSize = WorldPartitionEditorModulePtr->GetInstancedFoliageGridSize();
			WorldSettings->DefaultPlacementGridSize = WorldPartitionEditorModulePtr->GetPlacementGridSize();
		}

		WorldSettings->MarkPackageDirty();

		WorldPartition->DefaultHLODLayer = UHLODLayer::GetEngineDefaultHLODLayersSetup();

		AWorldDataLayers* WorldDataLayers = World->GetWorldDataLayers();
		if (!WorldDataLayers)
		{
			WorldDataLayers = AWorldDataLayers::Create(World);
			World->SetWorldDataLayers(WorldDataLayers);
		}

		FWorldPartitionMiniMapHelper::GetWorldPartitionMiniMap(World, true);
	}

	if (!WorldPartition->EditorHash)
	{
		if (!EditorHashClass)
		{
			EditorHashClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.WorldPartitionEditorSpatialHash"));
		}

		WorldPartition->EditorHash = NewObject<UWorldPartitionEditorHash>(WorldPartition, EditorHashClass);
		WorldPartition->EditorHash->SetDefaultValues();
	}

	if (!WorldPartition->RuntimeHash)
	{
		if (!RuntimeHashClass)
		{
			RuntimeHashClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.WorldPartitionRuntimeSpatialHash"));
		}

		WorldPartition->RuntimeHash = NewObject<UWorldPartitionRuntimeHash>(WorldPartition, RuntimeHashClass, NAME_None, RF_Transactional);
		WorldPartition->RuntimeHash->SetDefaultValues();
	}

	World->PersistentLevel->bIsPartitioned = true;

	return WorldPartition;
}

#endif

const TArray<FWorldPartitionStreamingSource>& UWorldPartition::GetStreamingSources() const
{
	if (StreamingPolicy && GetWorld()->IsGameWorld())
	{
		return StreamingPolicy->GetStreamingSources();
	}

	static TArray<FWorldPartitionStreamingSource> EmptyStreamingSources;
	return EmptyStreamingSources;
}

bool UWorldPartition::IsSimulating()
{
#if WITH_EDITOR
	return GEditor && GEditor->bIsSimulatingInEditor && GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->IsSimulateInEditorViewport();
#else
	return false;
#endif
}

#if WITH_EDITOR
void UWorldPartition::OnActorDescAdded(FWorldPartitionActorDesc* NewActorDesc)
{
	Super::OnActorDescAdded(NewActorDesc);

	NewActorDesc->bIsForcedNonSpatiallyLoaded = !IsStreamingEnabled();

	HashActorDesc(NewActorDesc);

	if (WorldPartitionEditor)
	{
		WorldPartitionEditor->Refresh();
	}
}

void UWorldPartition::OnActorDescRemoved(FWorldPartitionActorDesc* ActorDesc)
{
	Super::OnActorDescRemoved(ActorDesc);
	
	UnhashActorDesc(ActorDesc);
		
	if (WorldPartitionEditor)
	{
		WorldPartitionEditor->Refresh();
	}
}

void UWorldPartition::OnActorDescUpdating(FWorldPartitionActorDesc* ActorDesc)
{
	UnhashActorDesc(ActorDesc);
}

void UWorldPartition::OnActorDescUpdated(FWorldPartitionActorDesc* ActorDesc)
{
	HashActorDesc(ActorDesc);

	if (WorldPartitionEditor)
	{
		WorldPartitionEditor->Refresh();
	}
}

bool UWorldPartition::GetInstancingContext(const FLinkerInstancingContext*& OutInstancingContext, FSoftObjectPathFixupArchive*& OutSoftObjectPathFixupArchive) const
{
	if (InstancingContext.IsInstanced())
	{
		OutInstancingContext = &InstancingContext;
		OutSoftObjectPathFixupArchive = InstancingSoftObjectPathFixupArchive.Get();
		return true;
	}
	return false;
}
#endif

const FTransform& UWorldPartition::GetInstanceTransform() const
{
	return InstanceTransform.IsSet() ? InstanceTransform.GetValue() : Super::GetInstanceTransform();
}

#if WITH_EDITOR
bool UWorldPartition::SupportsStreaming() const
{
	return bSupportsStreaming && (World ? World->GetWorldSettings()->SupportsWorldPartitionStreaming() : false);
}

bool UWorldPartition::IsStreamingEnabled() const
{
	return bEnableStreaming && SupportsStreaming();
}

void UWorldPartition::SetEnableStreaming(bool bInEnableStreaming)
{
	if (bEnableStreaming != bInEnableStreaming)
	{
		Modify();
		bEnableStreaming = bInEnableStreaming;
		OnEnableStreamingChanged();
	}
}

bool UWorldPartition::CanBeUsedByLevelInstance() const
{
	return bCanBeUsedByLevelInstance && !IsStreamingEnabled();
}

void UWorldPartition::SetCanBeUsedByLevelInstance(bool bInCanBeUsedByLevelInstance)
{
	// Only allowed to be called when WorldPartition is not initialized
	check(!IsInitialized());

	if (bCanBeUsedByLevelInstance != bInCanBeUsedByLevelInstance)
	{
		Modify();
		bCanBeUsedByLevelInstance = bInCanBeUsedByLevelInstance;
		if (bCanBeUsedByLevelInstance)
		{
			bEnableStreaming = false;
		}
	}
}

void UWorldPartition::OnEnableStreamingChanged()
{
	for (UActorDescContainer::TIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
	{
		UnhashActorDesc(*ActorDescIterator);
		ActorDescIterator->bIsForcedNonSpatiallyLoaded = !IsStreamingEnabled();
		HashActorDesc(*ActorDescIterator);
	}

	FLoaderAdapterAlwaysLoadedActors* OldAlwaysLoadedActors = AlwaysLoadedActors;

	AlwaysLoadedActors = new FLoaderAdapterAlwaysLoadedActors(GetTypedOuter<UWorld>());
	AlwaysLoadedActors->Load();

	OldAlwaysLoadedActors->Unload();
	delete OldAlwaysLoadedActors;

	if (WorldPartitionEditor)
	{
		WorldPartitionEditor->Reconstruct();
	}
}

void UWorldPartition::HashActorDesc(FWorldPartitionActorDesc* ActorDesc)
{
	check(ActorDesc);
	check(EditorHash);

	FWorldPartitionHandle ActorHandle(this, ActorDesc->GetGuid());
	EditorHash->HashActor(ActorHandle);

	bShouldCheckEnableStreamingWarning = true;
}

void UWorldPartition::UnhashActorDesc(FWorldPartitionActorDesc* ActorDesc)
{
	check(ActorDesc);
	check(EditorHash);

	FWorldPartitionHandle ActorHandle(this, ActorDesc->GetGuid());
	EditorHash->UnhashActor(ActorHandle);
}
#endif

void UWorldPartition::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);

	Super::Serialize(Ar);

	if (Ar.GetPortFlags() & PPF_DuplicateForPIE)
	{
		Ar << StreamingPolicy;
	}
	else if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::WorldPartitionSerializeStreamingPolicyOnCook)
	{
		bool bCooked = Ar.IsCooking();
		Ar << bCooked;

		if (bCooked)
		{
			Ar << StreamingPolicy;
		}
	}
}

bool UWorldPartition::ResolveSubobject(const TCHAR* SubObjectPath, UObject*& OutObject, bool bLoadIfExists)
{
	if (GetWorld())
	{
		if (GetWorld()->IsGameWorld())
		{
			if (StreamingPolicy)
			{
				if (UObject* SubObject = StreamingPolicy->GetSubObject(SubObjectPath))
				{
					OutObject = SubObject;
					return true;
				}
				else
				{
					OutObject = nullptr;
				}
			}
		}
#if WITH_EDITOR
		else
		{
			// Support for subobjects such as Actor.Component
			FString SubObjectName;
			FString SubObjectContext;	
			if (!FString(SubObjectPath).Split(TEXT("."), &SubObjectContext, &SubObjectName))
			{
				SubObjectName = SubObjectPath;
			}

			if (const FWorldPartitionActorDesc* ActorDesc = GetActorDesc(SubObjectName))
			{
				if (bLoadIfExists)
				{
					LoadedSubobjects.Emplace(this, ActorDesc->GetGuid());
				}

				OutObject = StaticFindObject(UObject::StaticClass(), GetWorld()->PersistentLevel, SubObjectPath);
				return true;
			}
		}
#endif
	}

	return false;
}

void UWorldPartition::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
#if WITH_EDITOR
	// We need to keep all dirty actors alive, mainly for deleted actors. Normally, these actors are only referenced
	// by the transaction buffer, but we clear it when unloading regions, etc. and we don't want these actors to die.
	// Also, we must avoid reporting these references when not collecting garbage, as code such as package deletion
	// will skip packages with actors still referenced (via GatherObjectReferencersForDeletion).
	if (IsGarbageCollecting())
	{
		UWorldPartition* This = CastChecked<UWorldPartition>(InThis);
	
		Collector.AllowEliminatingReferences(false);
		for (auto& [ActorReference, Actor] : This->DirtyActors)
		{
			Collector.AddReferencedObject(Actor);
		}
		Collector.AllowEliminatingReferences(true);
	}
#endif

	Super::AddReferencedObjects(InThis, Collector);
}

void UWorldPartition::Tick(float DeltaSeconds)
{
#if WITH_EDITOR
	if (EditorHash)
	{
		EditorHash->Tick(DeltaSeconds);
	}

	if (bForceGarbageCollection)
	{
		GEngine->ForceGarbageCollection(bForceGarbageCollectionPurge);

		bForceGarbageCollection = false;
		bForceGarbageCollectionPurge = false;
	}

	if (bShouldCheckEnableStreamingWarning)
	{
		bShouldCheckEnableStreamingWarning = false;

		if (!IsStreamingEnabled() && SupportsStreaming() && !bStreamingWasEnabled && !bShouldEnableStreamingWarned)
		{
			FBox AllActorsBounds(ForceInit);
			for (UActorDescContainer::TConstIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
			{
				if (ActorDescIterator->GetIsSpatiallyLoadedRaw())
				{
					AllActorsBounds += ActorDescIterator->GetBounds();

					// Warn the user if the world becomes larger that 4km in any axis
					if (AllActorsBounds.GetSize().GetMax() >= 400000.0f)
					{
						bShouldEnableStreamingWarned = true;

						if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("WorldPartitionShouldEnableStreaming", "The size of your world has grown enough to justify enabling streaming. Enable streaming now?")) == EAppReturnType::Yes)
						{
							SetEnableStreaming(true);
							bStreamingWasEnabled = true;
							if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("WorldPartitionEnableStreamingDialolg", "Please refer to our documentation for how to set up streaming.\n\nWould you like to open it now? ")) == EAppReturnType::Yes)
							{
								IDocumentation::Get()->Open(TEXT("world-partition-in-unreal-engine"), FDocumentationSourceInfo(TEXT("worldpartition")));
							}
						}

						break;
					}
				}
			}
		}
	}
#endif
}

void UWorldPartition::UpdateStreamingState()
{
	if (GetWorld()->IsGameWorld())
	{
		StreamingPolicy->UpdateStreamingState();
	}
}

bool UWorldPartition::CanAddLoadedLevelToWorld(class ULevel* InLevel) const
{
	if (GetWorld()->IsGameWorld())
	{
		return StreamingPolicy->CanAddLoadedLevelToWorld(InLevel);
	}
	return true;
}

bool UWorldPartition::IsStreamingCompleted(EWorldPartitionRuntimeCellState QueryState, const TArray<FWorldPartitionStreamingQuerySource>& QuerySources, bool bExactState) const
{
	if (GetWorld()->IsGameWorld())
	{
		return StreamingPolicy->IsStreamingCompleted(QueryState, QuerySources, bExactState);
	}

	return false;
}

bool UWorldPartition::CanDrawRuntimeHash() const
{
	return GetWorld()->IsGameWorld() || (UWorldPartition::IsSimulating() && StreamingPolicy);
}

FVector2D UWorldPartition::GetDrawRuntimeHash2DDesiredFootprint(const FVector2D& CanvasSize)
{
	check(CanDrawRuntimeHash());
	return StreamingPolicy->GetDrawRuntimeHash2DDesiredFootprint(CanvasSize);
}

void UWorldPartition::DrawRuntimeHash2D(class UCanvas* Canvas, const FVector2D& PartitionCanvasSize, const FVector2D& Offset)
{
	check(CanDrawRuntimeHash());
	StreamingPolicy->DrawRuntimeHash2D(Canvas, PartitionCanvasSize, Offset);
}

void UWorldPartition::DrawRuntimeHash3D()
{
	check(CanDrawRuntimeHash());
	StreamingPolicy->DrawRuntimeHash3D();
}

void UWorldPartition::DrawRuntimeCellsDetails(class UCanvas* Canvas, FVector2D& Offset)
{
	StreamingPolicy->DrawRuntimeCellsDetails(Canvas, Offset);
}

void UWorldPartition::DrawStreamingStatusLegend(class UCanvas* Canvas, FVector2D& Offset)
{
	StreamingPolicy->DrawStreamingStatusLegend(Canvas, Offset);
}

EWorldPartitionStreamingPerformance UWorldPartition::GetStreamingPerformance() const
{
	return StreamingPolicy->GetStreamingPerformance();
}

#if WITH_EDITOR
void UWorldPartition::DrawRuntimeHashPreview()
{
	RuntimeHash->DrawPreview();
}

bool UWorldPartition::PopulateGeneratorPackageForCook(const TArray<ICookPackageSplitter::FGeneratedPackageForPreSave>& InGeneratedPackages, TArray<UPackage*>& OutModifiedPackages)
{
	check(RuntimeHash);
	return RuntimeHash->PopulateGeneratorPackageForCook(InGeneratedPackages, OutModifiedPackages);
}

bool UWorldPartition::PopulateGeneratedPackageForCook(UPackage* InPackage, const FString& InPackageRelativePath, TArray<UPackage*>& OutModifiedPackages)
{
	check(RuntimeHash);
	return RuntimeHash->PopulateGeneratedPackageForCook(InPackage, InPackageRelativePath, OutModifiedPackages);
}

TArray<FBox> UWorldPartition::GetUserLoadedEditorRegions() const
{
	TArray<FBox> Result;

	for (UWorldPartitionEditorLoaderAdapter* EditorLoaderAdapter : RegisteredEditorLoaderAdapters)
	{
		IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter = EditorLoaderAdapter->GetLoaderAdapter();
		check(LoaderAdapter);
		if (LoaderAdapter->IsLoaded() && LoaderAdapter->GetUserCreated())
		{
			Result.Add(*LoaderAdapter->GetBoundingBox());
		}
	}

	return Result;
}

void UWorldPartition::SavePerUserSettings()
{
	check(IsMainWorldPartition());

	if (GIsEditor && !World->IsGameWorld() && !IsRunningCommandlet() && !IsEngineExitRequested())
	{
		GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->SetEditorLoadedRegions(GetWorld(), GetUserLoadedEditorRegions());

		TArray<FName> EditorLoadedLocationVolumes;
		for (UActorDescContainer::TConstIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
		{
			if (ALocationVolume* LocationVolume = Cast<ALocationVolume>(ActorDescIterator->GetActor()))
			{
				check(LocationVolume->GetClass()->ImplementsInterface(UWorldPartitionActorLoaderInterface::StaticClass()));

				IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter = Cast<IWorldPartitionActorLoaderInterface>(LocationVolume)->GetLoaderAdapter();
				check(LoaderAdapter);

				if (LoaderAdapter->IsLoaded() && LoaderAdapter->GetUserCreated())
				{
					EditorLoadedLocationVolumes.Add(LocationVolume->GetFName());
				}
			}
		}
		GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->SetEditorLoadedLocationVolumes(GetWorld(), EditorLoadedLocationVolumes);
	}
}

void UWorldPartition::DumpActorDescs(const FString& Path)
{
	if (FArchive* LogFile = IFileManager::Get().CreateFileWriter(*Path))
	{
		TArray<const FWorldPartitionActorDesc*> ActorDescs;
		TMap<FName, FString> DataLayersDumpString = GetDataLayersDumpString(this);
		for (UActorDescContainer::TConstIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
		{
			ActorDescs.Add(*ActorDescIterator);
		}
		ActorDescs.Sort([](const FWorldPartitionActorDesc& A, const FWorldPartitionActorDesc& B)
		{
			return A.GetBounds().GetExtent().GetMax() < B.GetBounds().GetExtent().GetMax();
		});
		for (const FWorldPartitionActorDesc* ActorDescIterator : ActorDescs)
		{
			FString LineEntry = GetActorDescDumpString(ActorDescIterator, DataLayersDumpString);
			LogFile->Serialize(TCHAR_TO_ANSI(*LineEntry), LineEntry.Len());
		}

		LogFile->Close();
		delete LogFile;
	}
}

void UWorldPartition::AppendAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	static const FName NAME_LevelIsPartitioned(TEXT("LevelIsPartitioned"));
	OutTags.Add(FAssetRegistryTag(NAME_LevelIsPartitioned, TEXT("1"), FAssetRegistryTag::TT_Hidden));

	if (!IsStreamingEnabled())
	{
		static const FName NAME_LevelHasStreamingDisabled(TEXT("LevelHasStreamingDisabled"));
		OutTags.Add(FAssetRegistryTag(NAME_LevelHasStreamingDisabled, TEXT("1"), FAssetRegistryTag::TT_Hidden));
	}

	if (CanBeUsedByLevelInstance())
	{
		static const FName NAME_PartitionedLevelCanBeUsedByLevelInstance(TEXT("PartitionedLevelCanBeUsedByLevelInstance"));
		OutTags.Add(FAssetRegistryTag(NAME_PartitionedLevelCanBeUsedByLevelInstance, TEXT("1"), FAssetRegistryTag::TT_Hidden));
	}
}

void UWorldPartition::PinActors(const TArray<FGuid>& ActorGuids)
{
	if (PinnedActors)
	{
		PinnedActors->AddActors(ActorGuids);
	}
}

void UWorldPartition::UnpinActors(const TArray<FGuid>& ActorGuids)
{
	if (PinnedActors)
	{
		PinnedActors->RemoveActors(ActorGuids);
	}
}

bool UWorldPartition::IsActorPinned(const FGuid& ActorGuid) const
{
	if (PinnedActors)
	{
		return PinnedActors->ContainsActor(ActorGuid);
	}
	return false;
}

void UWorldPartition::LoadLastLoadedRegions(const TArray<FBox>& EditorLastLoadedRegions)
{
	for (const FBox& EditorLastLoadedRegion : EditorLastLoadedRegions)
	{
		UWorldPartitionEditorLoaderAdapter* EditorLoaderAdapter = CreateEditorLoaderAdapter<FLoaderAdapterShape>(World, EditorLastLoadedRegion, TEXT("Last Loaded Region"));
		IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter = EditorLoaderAdapter->GetLoaderAdapter();
		check(LoaderAdapter);
		LoaderAdapter->SetUserCreated(true);
		LoaderAdapter->Load();
	}
}

void UWorldPartition::LoadLastLoadedRegions()
{
	check(IsMainWorldPartition());

	TArray<FBox> EditorLastLoadedRegions = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetEditorLoadedRegions(World);
	LoadLastLoadedRegions(EditorLastLoadedRegions);

	TArray<FName> EditorLoadedLocationVolumes = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetEditorLoadedLocationVolumes(World);
	for (const FName& EditorLoadedLocationVolume : EditorLoadedLocationVolumes)
	{
		if (ALocationVolume* LocationVolume = FindObject<ALocationVolume>(World->PersistentLevel, *EditorLoadedLocationVolume.ToString()))
		{
			LocationVolume->bIsAutoLoad = true;
		}
	}
}

void UWorldPartition::OnWorldRenamed()
{
	UActorDescContainer::OnWorldRenamed();

	// World was renamed so existing context is invalid.
	InstancingContext = FLinkerInstancingContext();
	InstancingSoftObjectPathFixupArchive.Reset();
}

void UWorldPartition::RemapSoftObjectPath(FSoftObjectPath& ObjectPath)
{
	StreamingPolicy->RemapSoftObjectPath(ObjectPath);
}

FBox UWorldPartition::GetEditorWorldBounds() const
{
	check(EditorHash);
	return EditorHash->GetEditorWorldBounds();
}

FBox UWorldPartition::GetRuntimeWorldBounds() const
{
	check(EditorHash);
	return EditorHash->GetRuntimeWorldBounds();
}
#endif

#undef LOCTEXT_NAMESPACE
