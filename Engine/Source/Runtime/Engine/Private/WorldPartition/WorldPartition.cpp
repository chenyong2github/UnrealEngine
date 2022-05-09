// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	WorldPartition.cpp: UWorldPartition implementation
=============================================================================*/
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionLog.h"
#include "WorldPartition/WorldPartitionEditorCell.h"
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
#include "WorldPartition/WorldPartitionActorDescViewProxy.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "Modules/ModuleManager.h"
#include "GameDelegates.h"

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
		DataLayersDumpString.FindOrAdd(DataLayer->GetDataLayerFName()) = FString::Format(TEXT("{0}({1})"), { DataLayer->GetDataLayerShortName(), DataLayer->GetDataLayerFName().ToString() });
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

		return FString::JoinBy(DataLayerNames, TEXT(", "), 
			[&DataLayersDumpString](const FName& DataLayerName) 
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
		TEXT("%s, %s, %s, %s, %s, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %s") LINE_TERMINATOR,
		*ActorDesc->GetGuid().ToString(),
		*ActorDesc->GetNativeClass().ToString(),
		*ActorDesc->GetActorName().ToString(),
		*ActorDesc->GetActorPackage().ToString(),
		*ActorDesc->GetActorLabel().ToString(),
		ActorDesc->GetBounds().GetCenter().X,
		ActorDesc->GetBounds().GetCenter().Y,
		ActorDesc->GetBounds().GetCenter().Z,
		ActorDesc->GetBounds().GetExtent().X,
		ActorDesc->GetBounds().GetExtent().Y,
		ActorDesc->GetBounds().GetExtent().Z,
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
						UE_LOG(LogWorldPartition, Log, TEXT("Guid, Class, Name, Package, BVCenterX, BVCenterY, BVCenterZ, BVExtentX, BVExtentY, BVExtentZ, DataLayers"));
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

// Helper class to avoid sending global events until all cells updates are processed.
struct FWorldPartionCellUpdateContext
{
	FWorldPartionCellUpdateContext(UWorldPartition* InWorldPartition)
		: WorldPartition(InWorldPartition)
		, bCancelled(false)
	{
		WorldPartition->OnCancelWorldPartitionUpdateEditorCells.AddRaw(this, &FWorldPartionCellUpdateContext::OnCancelUpdateEditorCells);

		UpdatesInProgress++;
	}

	void OnCancelUpdateEditorCells(UWorldPartition* InWorldPartition)
	{
		if (WorldPartition == InWorldPartition)
		{
			bCancelled = true;
		}
	}

	~FWorldPartionCellUpdateContext()
	{
		WorldPartition->OnCancelWorldPartitionUpdateEditorCells.RemoveAll(this);

		UpdatesInProgress--;
		if (UpdatesInProgress == 0 && !bCancelled)
		{
			// @todo_ow: Once Metadata is removed from external actor's package, testing WorldPartition->IsInitialized() won't be necessary anymore.
			if (WorldPartition->IsInitialized())
			{
				if (!IsRunningCommandlet())
				{
					GEngine->BroadcastLevelActorListChanged();
					GEditor->NoteSelectionChange();

					if (WorldPartition->WorldPartitionEditor)
					{
						WorldPartition->WorldPartitionEditor->Refresh();
					}

					GEditor->ResetTransaction(LOCTEXT("LoadingEditorCellsResetTrans", "Editor Cells Loading State Changed"));
				}
			}
		}
	}

	static int32 UpdatesInProgress;
	UWorldPartition* WorldPartition;
	bool bCancelled;
};

int32 FWorldPartionCellUpdateContext::UpdatesInProgress = 0;

#endif

UWorldPartition::UWorldPartition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, EditorHash(nullptr)
	, WorldPartitionEditor(nullptr)
	, bEnableStreaming(true)
	, bStreamingWasEnabled(true)
	, bShouldEnableStreamingWarned(false)
	, bShouldCheckEnableStreamingWarning(false)
	, bCanBeUsedByLevelInstance(false)
	, bForceGarbageCollection(false)
	, bForceGarbageCollectionPurge(false)
	, bIsPIE(false)
#endif
	, InitState(EWorldPartitionInitState::Uninitialized)
	, StreamingPolicy(nullptr)
#if !UE_BUILD_SHIPPING
	, Replay(nullptr)
#endif
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
	RuntimeHash->FlushStreaming();
	RuntimeHash->OnEndPlay();

	StreamingPolicy = nullptr;
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
	}
	else if (bIsEditor)
	{
		CreateOrRepairWorldPartition(OuterWorld->GetWorldSettings());

		check(!StreamingPolicy);
		check(EditorHash);

		EditorHash->Initialize();
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

	if (bIsEditor)
	{
		// Here we need to flush any async loading before starting any synchronous load (mixing synchronous and asynchronous load on the same package is not handled properly).
		// Also, this will ensure that FindObject will work on an external actor that was loading asynchronously.
		FlushAsyncLoading();

		// Make sure to preload only AWorldDataLayers actor first (ShouldActorBeLoadedByEditorCells requires it)
		for (UActorDescContainer::TIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
		{
			if (ActorDescIterator->GetActorNativeClass()->IsChildOf<AWorldDataLayers>())
			{
				WorldDataLayersActor = FWorldPartitionReference(this, ActorDescIterator->GetGuid());
				break;
			}
		}
		
		// Repair existing maps with no UWorld::WorldDataLayers actor.
		AWorldDataLayers* WorldDataLayers = World->GetWorldDataLayers();
		if (!WorldDataLayers)
		{
			// WorldDataLayersActor is invalid here orelse its PostLoad would have set itself as the world's WorldDataLayers
			check(!WorldDataLayersActor.IsValid());
			WorldDataLayers = AWorldDataLayers::Create(World);
			World->SetWorldDataLayers(WorldDataLayers);
		}

		// Apply level transform on actors already part of the level
		if (GetInstanceTransformPtr())
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
		UpdateLoadingEditorCell(EditorHash->GetAlwaysLoadedCell(), true, false);

		// Load more cells depending on the user's settings
		// Skipped when running from a commandlet
		if (!IsRunningCommandlet())
		{
			// Load last loaded cells
			if (GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetEnableLoadingOfLastLoadedCells())
			{
				TArray<FName> EditorGridLastLoadedCells = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetEditorGridLoadedCells(InWorld);

				LoadEditorCells(EditorGridLastLoadedCells, /*bIsFromUserChange=*/true);
			}
		}
		
		if (GEditor)
		{
			GEditor->OnEditorClose().AddUObject(this, &UWorldPartition::SavePerUserSettings);
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
		SavePerUserSettings();

		if (GEditor && !World->IsGameWorld())
		{
			GEditor->OnEditorClose().RemoveAll(this);
		}

		if (World->IsGameWorld())
		{
			OnEndPlay();
		}
		else
		{
			if (!IsEngineExitRequested())
			{
				// Unload all Editor cells
				if (EditorHash)
				{
					// @todo_ow: Once Metadata is removed from external actor's package, this won't be necessary anymore.
					EditorHash->ForEachCell([this](UWorldPartitionEditorCell* Cell)
					{
						UpdateLoadingEditorCell(Cell, /*bShouldBeLoaded*/false, /*bIsFromUserChange*/false);
					});
				}
			}
		}

		WorldDataLayersActor = FWorldPartitionReference();

		PinnedActors.Empty();
		PinnedActorRefs.Empty();

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

bool UWorldPartition::IsMainWorldPartition() const
{
	check(World);
	return World == GetTypedOuter<UWorld>();
}

const FTransform& UWorldPartition::GetInstanceTransform() const
{
	if (const FTransform* TransformPtr = GetInstanceTransformPtr())
	{
		return *TransformPtr;
	}
	return FTransform::Identity;
}

const FTransform* UWorldPartition::GetInstanceTransformPtr() const
{
	return InstanceTransform.IsSet() ? &InstanceTransform.GetValue() : nullptr;
}

void UWorldPartition::OnPostBugItGoCalled(const FVector& Loc, const FRotator& Rot)
{
#if WITH_EDITOR
	if (GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetBugItGoLoadCells())
{
		const FVector LoadExtent(GLoadingRangeBugItGo, GLoadingRangeBugItGo, WORLDPARTITION_MAX);
		const FBox LoadCellsBox(Loc - LoadExtent, Loc + LoadExtent);
		LoadEditorCells(LoadCellsBox, false);
	}
#endif
}

#if WITH_EDITOR
void UWorldPartition::OnActorDataLayersEditorLoadingStateChanged(bool bFromUserOperation)
{
	auto GetCellsToRefresh = [this](TArray<UWorldPartitionEditorCell*>& CellsToRefresh)
	{
		EditorHash->ForEachCell([&CellsToRefresh](UWorldPartitionEditorCell* Cell)
		{
			if (Cell->IsLoaded())
			{
				CellsToRefresh.Add(Cell);
			}
		});
		return !CellsToRefresh.IsEmpty();
	};

	UpdateEditorCells(GetCellsToRefresh, /*bIsCellShouldBeLoaded*/true, bFromUserOperation);
}
#endif

void UWorldPartition::RegisterDelegates()
{
	check(World); 

	if (IsMainWorldPartition())
	{
#if WITH_EDITOR
		if (GEditor && !IsTemplate() && !World->IsGameWorld())
		{
			FEditorDelegates::PreBeginPIE.AddUObject(this, &UWorldPartition::OnPreBeginPIE);
			FEditorDelegates::PrePIEEnded.AddUObject(this, &UWorldPartition::OnPrePIEEnded);
			FEditorDelegates::CancelPIE.AddUObject(this, &UWorldPartition::OnCancelPIE);
			FGameDelegates::Get().GetEndPlayMapDelegate().AddUObject(this, &UWorldPartition::OnEndPlay);

			FCoreUObjectDelegates::PostReachabilityAnalysis.AddUObject(this, &UWorldPartition::OnGCPostReachabilityAnalysis);

			GEditor->OnPostBugItGoCalled().AddUObject(this, &UWorldPartition::OnPostBugItGoCalled);

			FDataLayersEditorBroadcast::Get().OnActorDataLayersEditorLoadingStateChanged().AddUObject(this, &UWorldPartition::OnActorDataLayersEditorLoadingStateChanged);
		}
#endif

		if (World->IsGameWorld())
		{
			World->OnWorldMatchStarting.AddUObject(this, &UWorldPartition::OnWorldMatchStarting);

#if !UE_BUILD_SHIPPING
			FCoreDelegates::OnGetOnScreenMessages.AddUObject(this, &UWorldPartition::GetOnScreenMessages);
#endif
		}
	}
}

void UWorldPartition::UnregisterDelegates()
{
	check(World);

	if (IsMainWorldPartition())
	{
#if WITH_EDITOR
		if (GEditor && !IsTemplate() && !World->IsGameWorld())
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

			FDataLayersEditorBroadcast::Get().OnActorDataLayersEditorLoadingStateChanged().RemoveAll(this);
		}
#endif

		if (World->IsGameWorld())
		{
			World->OnWorldMatchStarting.RemoveAll(this);

#if !UE_BUILD_SHIPPING
			FCoreDelegates::OnGetOnScreenMessages.RemoveAll(this);
#endif
		}
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
			EditorHashClass = FindObject<UClass>(ANY_PACKAGE, TEXT("WorldPartitionEditorSpatialHash"));
		}

		WorldPartition->EditorHash = NewObject<UWorldPartitionEditorHash>(WorldPartition, EditorHashClass);
		WorldPartition->EditorHash->SetDefaultValues();
	}

	if (!WorldPartition->RuntimeHash)
	{
		if (!RuntimeHashClass)
		{
			RuntimeHashClass = FindObject<UClass>(ANY_PACKAGE, TEXT("WorldPartitionRuntimeSpatialHash"));
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
void UWorldPartition::LoadEditorCells(const FBox& Box, bool bIsFromUserChange)
{
	TArray<UWorldPartitionEditorCell*> CellsToLoad;
	if (EditorHash->GetIntersectingCells(Box, CellsToLoad))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartition::LoadEditorCells);

		FWorldPartionCellUpdateContext CellUpdateContext(this);

		int32 NumActorsToLoad = Algo::TransformAccumulate(CellsToLoad, [](UWorldPartitionEditorCell* Cell) { return Cell->Actors.Num() - Cell->LoadedActors.Num();}, 0);

		FScopedSlowTask SlowTask(NumActorsToLoad, LOCTEXT("LoadingCells", "Loading cells..."));
		SlowTask.MakeDialog();

		for (UWorldPartitionEditorCell* Cell : CellsToLoad)
		{
			SlowTask.EnterProgressFrame(Cell->Actors.Num() - Cell->LoadedActors.Num());
			UpdateLoadingEditorCell(Cell, true, bIsFromUserChange);
		}
	}
}

void UWorldPartition::LoadEditorCells(const TArray<FName>& CellNames, bool bIsFromUserChange)
{
	for (FName CellName : CellNames)
	{
		if (UWorldPartitionEditorCell* Cell = FindObject<UWorldPartitionEditorCell>(EditorHash, *CellName.ToString()))
		{
			UpdateLoadingEditorCell(Cell, true, bIsFromUserChange);
		}
	}
}

void UWorldPartition::UnloadEditorCells(const FBox& Box, bool bIsFromUserChange)
{
	FWorldPartionCellUpdateContext CellUpdateContext(this);

	auto GetCellsToUnload = [this, Box](TArray<UWorldPartitionEditorCell*>& CellsToUnload)
	{
		return EditorHash->GetIntersectingCells(Box, CellsToUnload) > 0;
	};

	UpdateEditorCells(GetCellsToUnload, /*bIsCellShouldBeLoaded*/false, bIsFromUserChange);
}

bool UWorldPartition::AreEditorCellsLoaded(const FBox& Box)
{
	TArray<UWorldPartitionEditorCell*> CellsToLoad;
	if (EditorHash->GetIntersectingCells(Box, CellsToLoad))
	{
		for (UWorldPartitionEditorCell* Cell : CellsToLoad)
		{
			if (!Cell->IsLoaded())
			{
				return false;
			}
		}
	}

	return true;
}

bool UWorldPartition::UpdateEditorCells(TFunctionRef<bool(TArray<UWorldPartitionEditorCell*>&)> GetCellsToProcess, bool bIsCellShouldBeLoaded, bool bIsFromUserChange)
{
	FWorldPartionCellUpdateContext CellUpdateContext(this);

	TArray<UWorldPartitionEditorCell*> CellsToProcess;
	if (!GetCellsToProcess(CellsToProcess))
	{
		return true;
	}

	TSet<UPackage*> ModifiedPackages;
	TMap<FWorldPartitionActorDesc*, int32> UnloadCount;

	for (UWorldPartitionEditorCell* Cell : CellsToProcess)
	{
		for (const UWorldPartitionEditorCell::FActorReference& ActorDesc : Cell->LoadedActors)
		{
			if (ActorDesc.IsValid())
			{
				if (!bIsCellShouldBeLoaded || !ShouldActorBeLoadedByEditorCells(*ActorDesc))
				{
					UnloadCount.FindOrAdd(*ActorDesc, 0)++;
				}
			}
		}
	}

	bool bIsUpdateUnloadingActors = false;

	for (const TPair<FWorldPartitionActorDesc*, int32> Pair : UnloadCount)
	{
		FWorldPartitionActorDesc* ActorDesc = Pair.Key;

		// Only prompt if the actor will get unloaded by the unloading cells
		if (ActorDesc->GetHardRefCount() == Pair.Value)
		{
			if (AActor* LoadedActor = ActorDesc->GetActor())
			{
				UPackage* ActorPackage = LoadedActor->GetExternalPackage();
				if (ActorPackage && ActorPackage->IsDirty())
				{
					ModifiedPackages.Add(ActorPackage);
				}
				bIsUpdateUnloadingActors = true;
			}
		}
	}

	// Make sure we save modified actor packages before unloading
	FEditorFileUtils::EPromptReturnCode RetCode = FEditorFileUtils::PR_Success;

	// Skip this when running commandlet because MarkPackageDirty() is allowed to dirty packages at loading when running a commandlet.
	// Usually, the main reason actor packages are dirtied is when RerunConstructionScripts is called on the actor when it is added to the level.
	if (ModifiedPackages.Num() && !IsRunningCommandlet())
	{
		const bool bCheckDirty = false;
		const bool bAlreadyCheckedOut = false;
		const bool bCanBeDeclined = true;
		const bool bPromptToSave = true;
		const FText Title = LOCTEXT("SaveActorsTitle", "Save Actor(s)");
		const FText Message = LOCTEXT("SaveActorsMessage", "Save Actor(s) before unloading them.");

		RetCode = FEditorFileUtils::PromptForCheckoutAndSave(ModifiedPackages.Array(), bCheckDirty, bPromptToSave, Title, Message, nullptr, bAlreadyCheckedOut, bCanBeDeclined);
		if (RetCode == FEditorFileUtils::PR_Cancelled)
		{
			OnCancelWorldPartitionUpdateEditorCells.Broadcast(this);
			return false;
		}

		check(RetCode != FEditorFileUtils::PR_Failure);
	}

	if (bIsUpdateUnloadingActors)
	{
		GEditor->SelectNone(true, true);
	}

	// At this point, cells might have changed due to saving deleted actors
	CellsToProcess.Empty(CellsToProcess.Num());
	if (!GetCellsToProcess(CellsToProcess))
	{
		return true;
	}

	FScopedSlowTask SlowTask(CellsToProcess.Num(), LOCTEXT("UpdatingCells", "Updating cells..."));
	SlowTask.MakeDialog();

	for (UWorldPartitionEditorCell* Cell : CellsToProcess)
	{
		SlowTask.EnterProgressFrame(1);
		UpdateLoadingEditorCell(Cell, bIsCellShouldBeLoaded, bIsFromUserChange);
	}

	return true;
}

bool UWorldPartition::ShouldActorBeLoadedByEditorCells(const FWorldPartitionActorDesc* ActorDesc) const
{
	if (!ActorDesc->ShouldBeLoadedByEditorCells())
	{
		return false;
	}

	if (UDataLayerSubsystem* DataLayerSubsystem = UWorld::GetSubsystem<UDataLayerSubsystem>(GetWorld()))
	{
		// Use DataLayers of loaded/dirty Actor if available to handle dirtied actors
		FWorldPartitionActorViewProxy ActorDescProxy(ActorDesc);

		if (IsRunningCookCommandlet())
		{
			// When running cook commandlet, dont allow loading of actors with dynamically loaded data layers
			for (const FName& DataLayerInstanceName : ActorDescProxy.GetDataLayers())
			{
				const UDataLayerInstance* DataLayerInstance = DataLayerSubsystem->GetDataLayerInstance(DataLayerInstanceName);
				if (DataLayerInstance && DataLayerInstance->IsRuntime())
				{
					return false;
				}
			}
		}
		else
		{
			uint32 NumValidLayers = 0;
			for (const FName& DataLayerInstanceName : ActorDescProxy.GetDataLayers())
			{
				if (const UDataLayerInstance* DataLayerInstance = DataLayerSubsystem->GetDataLayerInstance(DataLayerInstanceName))
				{
					if (DataLayerInstance->IsEffectiveLoadedInEditor())
					{
						return true;
					}
					NumValidLayers++;
				}
			}
			return !NumValidLayers;
		}
	}
	
	return true;
};

// Loads actors in Editor cell
void UWorldPartition::UpdateLoadingEditorCell(UWorldPartitionEditorCell* Cell, bool bShouldBeLoaded, bool bFromUserOperation)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartition::UpdateLoadingEditorCell);
	
	FWorldPartionCellUpdateContext CellUpdateContext(this);

	UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartition::UpdateLoadingEditorCell 0x%08llx [%s/%s]"), Cell, bShouldBeLoaded ? TEXT("Load") : TEXT("Unload"), bFromUserOperation ? TEXT("User") : TEXT("Auto"));

	Cell->Modify(false);

	bool bPotentiallyUnloadedActors = false;

	if (!bShouldBeLoaded)
	{
		bPotentiallyUnloadedActors = !Cell->LoadedActors.IsEmpty();
		Cell->LoadedActors.Empty();
	}
	else
	{
		for (UWorldPartitionEditorCell::FActorHandle& ActorHandle: Cell->Actors)
		{
			if (ActorHandle.IsValid())
			{
				// Filter actor against DataLayers
				if (ShouldActorBeLoadedByEditorCells(*ActorHandle))
				{
					Cell->LoadedActors.Add(UWorldPartitionEditorCell::FActorReference(ActorHandle.Source, ActorHandle.Handle));
				}
				else
				{
					// Don't call LoadedActors.Remove(ActorHandle) right away, as this will create a temporary reference and might try to load.
					for (const UWorldPartitionEditorCell::FActorReference& ActorReference : Cell->LoadedActors)
					{
						if ((ActorReference.Source == ActorHandle.Source) && (ActorReference.Handle == ActorHandle.Handle))
						{
							Cell->LoadedActors.Remove(UWorldPartitionEditorCell::FActorReference(ActorHandle.Source, ActorHandle.Handle));
							bPotentiallyUnloadedActors = true;
							break;
						}
					}
				}
			}
		}
	}

	if (Cell->IsLoaded() != bShouldBeLoaded)
	{
		Cell->SetLoaded(bShouldBeLoaded, bShouldBeLoaded && bFromUserOperation);
	}

	if (bPotentiallyUnloadedActors)
	{
		bForceGarbageCollection = true;
		bForceGarbageCollectionPurge = true;
	}
}

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

void UWorldPartition::OnActorDescRegistered(const FWorldPartitionActorDesc& ActorDesc) 
{
	AActor* Actor = ActorDesc.GetActor();
	check(Actor);
	Actor->GetLevel()->AddLoadedActor(Actor, GetInstanceTransformPtr());
}

void UWorldPartition::OnActorDescUnregistered(const FWorldPartitionActorDesc& ActorDesc) 
{
	AActor* Actor = ActorDesc.GetActor();
	if (IsValidChecked(Actor))
	{
		Actor->GetLevel()->RemoveLoadedActor(Actor, GetInstanceTransformPtr());
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

bool UWorldPartition::SupportsStreaming() const
{
	return World ? World->GetWorldSettings()->SupportsWorldPartitionStreaming() : false;
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
	// Pin the actor handles on the actor to prevent unloading it when unhashing
	TArray<FWorldPartitionHandlePinRefScope> PinRefScopes;
	PinRefScopes.Reserve(ActorDescList.Num());

	for (UActorDescContainer::TIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
	{
		FWorldPartitionHandle ExistingActorHandle(this, ActorDescIterator->GetGuid());
		PinRefScopes.Emplace(ExistingActorHandle);

		UnhashActorDesc(*ActorDescIterator);
	}

	for (UActorDescContainer::TIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
	{
		ActorDescIterator->bIsForcedNonSpatiallyLoaded = !IsStreamingEnabled();
		HashActorDesc(*ActorDescIterator);
	}

	if (!IsStreamingEnabled())
	{
		UpdateLoadingEditorCell(EditorHash->GetAlwaysLoadedCell(), true, false);
	}

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
							FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("WorldPartitionEnableStreamingDialolg", "Please refer to https://docs.unrealengine.com/5.0/en-US/building-virtual-worlds/world-partition for how to set up streaming."));			
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
	return GetWorld()->IsGameWorld() || UWorldPartition::IsSimulating();
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

bool UWorldPartition::PopulateGeneratedPackageForCook(UPackage* InPackage, const FString& InPackageRelativePath)
{
	check(RuntimeHash);
	return RuntimeHash->PopulateGeneratedPackageForCook(InPackage, InPackageRelativePath);
}

bool UWorldPartition::FinalizeGeneratorPackageForCook(const TArray<ICookPackageSplitter::FGeneratedPackageForPreSave>& InGeneratedPackages)
{
	check(RuntimeHash);
	if (RuntimeHash->FinalizeGeneratorPackageForCook(InGeneratedPackages))
	{
		// Apply remapping of Persistent Level's SoftObjectPaths
		FWorldPartitionLevelHelper::RemapLevelSoftObjectPaths(World->PersistentLevel, this);
		return true;
	}
	return false;
}

TArray<FName> UWorldPartition::GetUserLoadedEditorGridCells() const
{
	// Save last loaded cells settings
	TArray<FName> LastEditorGridLoadedCells = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetEditorGridLoadedCells(GetWorld());

	TArray<FName> EditorGridLastLoadedCells;
	EditorHash->ForEachCell([this, &LastEditorGridLoadedCells, &EditorGridLastLoadedCells](UWorldPartitionEditorCell* Cell)
	{
		FName CellName = Cell->GetFName();

		if (Cell != EditorHash->GetAlwaysLoadedCell())
		{
			if (Cell->IsLoaded() && Cell->IsLoadedChangedByUserOperation())
			{
				EditorGridLastLoadedCells.Add(CellName);
			}
			else if (!Cell->IsLoaded() && !Cell->IsLoadedChangedByUserOperation() && LastEditorGridLoadedCells.Contains(CellName))
			{
				EditorGridLastLoadedCells.Add(CellName);
			}
		}
	});

	return EditorGridLastLoadedCells;
}

void UWorldPartition::SavePerUserSettings()
{
	if (GIsEditor && !World->IsGameWorld() && !IsRunningCommandlet() && !IsEngineExitRequested())
	{
		GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->SetEditorGridLoadedCells(GetWorld(), GetUserLoadedEditorGridCells());
	}
}

void UWorldPartition::DumpActorDescs(const FString& Path)
{
	if (FArchive* LogFile = IFileManager::Get().CreateFileWriter(*Path))
	{
		FString LineEntry = TEXT("Guid, Class, Name, Package, BVCenterX, BVCenterY, BVCenterZ, BVExtentX, BVExtentY, BVExtentZ, DataLayers") LINE_TERMINATOR;
		LogFile->Serialize(TCHAR_TO_ANSI(*LineEntry), LineEntry.Len());

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
			LineEntry = GetActorDescDumpString(ActorDescIterator, DataLayersDumpString);
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

uint32 UWorldPartition::GetWantedEditorCellSize() const
{
	return EditorHash->GetWantedEditorCellSize();
}

void UWorldPartition::SetEditorWantedCellSize(uint32 InCellSize)
{
	EditorHash->SetEditorWantedCellSize(InCellSize);
}

AActor* UWorldPartition::PinActor(const FGuid& ActorGuid)
{
	if (FWorldPartitionReference* PinnedActor = PinnedActors.Find(ActorGuid))
	{
		return (*PinnedActor).IsValid() ? (*PinnedActor)->GetActor() : nullptr;
	}

	TFunction<void(const FGuid&, TMap<FGuid, FWorldPartitionReference>&)> AddReferences = [this, &AddReferences](const FGuid& ActorGuid, TMap<FGuid, FWorldPartitionReference>& ReferenceMap)
	{
		if (ReferenceMap.Contains(ActorGuid))
		{
			return;
		}

		if (TUniquePtr<FWorldPartitionActorDesc>* const ActorDescPtr = GetActorDescriptor(ActorGuid); ActorDescPtr != nullptr && ActorDescPtr->IsValid())
		{
			ReferenceMap.Emplace(ActorGuid, ActorDescPtr);
			
			const FWorldPartitionActorDesc* const ActorDesc = ActorDescPtr->Get(); 
			for (const FGuid& ReferencedActor : ActorDesc->GetReferences())
			{
				AddReferences(ReferencedActor, ReferenceMap);
			}
		}
	};
	
	if (TUniquePtr<FWorldPartitionActorDesc>* const ActorDescPtr = GetActorDescriptor(ActorGuid))
	{
		if (const FWorldPartitionActorDesc* const ActorDesc = ActorDescPtr->Get())
		{
			FWorldPartitionReference& PinnedActor = PinnedActors.Emplace(ActorGuid, ActorDescPtr);

			// If the pinned actor has references, we must also create references to those to ensure they are added
			TMap<FGuid, FWorldPartitionReference>& References = PinnedActorRefs.Emplace(ActorGuid);

			for (const FGuid& ReferencedActor : ActorDesc->GetReferences())
			{
				AddReferences(ReferencedActor, References);
			}

			return PinnedActor.IsValid() ? PinnedActor->GetActor() : nullptr;
		}
	}

	return nullptr;
}

void UWorldPartition::UnpinActor(const FGuid& ActorGuid)
{
	PinnedActors.Remove(ActorGuid);
	PinnedActorRefs.Remove(ActorGuid);
}

bool UWorldPartition::IsActorPinned(const FGuid& ActorGuid) const
{
	return PinnedActors.Contains(ActorGuid);
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

FBox UWorldPartition::GetWorldBounds() const
{
	FBox WorldBounds(ForceInit);
	for (UActorDescContainer::TConstIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
	{
		if (ActorDescIterator->GetIsSpatiallyLoaded())
		{
			WorldBounds += ActorDescIterator->GetBounds();
		}
	}
	return WorldBounds;
}

FBox UWorldPartition::GetEditorWorldBounds() const
{
	// This is not a valid call in PIE
	check(EditorHash);
	return EditorHash ? EditorHash->GetEditorWorldBounds() : FBox(EForceInit::ForceInit);
}
#endif

#undef LOCTEXT_NAMESPACE