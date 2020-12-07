// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	WorldPartition.cpp: UWorldPartition implementation
=============================================================================*/
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorCell.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartition/WorldPartitionLevelStreamingPolicy.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Algo/Accumulate.h"
#include "Algo/Transform.h"
#include "Algo/RemoveIf.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/WorldSettings.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "LevelUtils.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/GroupActor.h"
#include "EditorLevelUtils.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/Base64.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "UnrealEdMisc.h"
#include "AssetRegistryModule.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartition/WorldPartitionEditorSpatialHash.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"
#include "Misc/Base64.h"
#endif //WITH_EDITOR

DEFINE_LOG_CATEGORY(LogWorldPartition);

#define LOCTEXT_NAMESPACE "WorldPartitionEditor"

#if WITH_EDITOR
static FAutoConsoleCommand DumpActorDescs(
	TEXT("wp.Editor.DumpActorDescs"),
	TEXT("Dump the list of actor descriptors in a CSV file."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() > 0)
		{
			if (UWorld* World = GEditor->GetEditorWorldContext().World())
			{
				if (!World->IsPlayInEditor())
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

// Helper class to avoid sending global events until all cells updates are processed.
struct FWorldPartionCellUpdateContext
{
	FWorldPartionCellUpdateContext(UWorldPartition* InWorldPartition)
		: WorldPartition(InWorldPartition)
	{
		UpdatesInProgress++;
	}

	~FWorldPartionCellUpdateContext()
	{
		UpdatesInProgress--;
		if (UpdatesInProgress == 0)
		{
			// @todo_ow: Once Metadata is removed from external actor's package, testing WorldPartition->IsInitialized() won't be necessary anymore.
			if (WorldPartition->IsInitialized())
			{
				GEngine->BroadcastLevelActorListChanged();
				GEditor->NoteSelectionChange();

				if (WorldPartition->WorldPartitionEditor)
				{
					WorldPartition->WorldPartitionEditor->Refresh();
				}
			}
		}
	}

	static int32 UpdatesInProgress;
	UWorldPartition* WorldPartition;
};

int32 FWorldPartionCellUpdateContext::UpdatesInProgress = 0;

UWorldPartition::FEnableWorldPartitionEvent UWorldPartition::EnableWorldPartitionEvent;
UWorldPartition::FWorldPartitionChangedEvent UWorldPartition::WorldPartitionChangedEvent;
#endif

UWorldPartition::UWorldPartition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, World(nullptr)
#if WITH_EDITOR
	, EditorHash(nullptr)
	, WorldPartitionEditor(nullptr)
	, bIgnoreAssetRegistryEvents(false)
	, bForceGarbageCollection(false)
	, bForceGarbageCollectionPurge(false)
#endif
	, InitState(EWorldPartitionInitState::Uninitialized)
	, InstanceTransform(FTransform::Identity)
	, StreamingPolicy(nullptr)
{
	static bool bRegisteredDelegate = false;
	if (!bRegisteredDelegate)
	{
		FWorldDelegates::LevelRemovedFromWorld.AddStatic(&UWorldPartition::WorldPartitionOnLevelRemovedFromWorld);
		bRegisteredDelegate = true;
	}
}

#if WITH_EDITOR
void UWorldPartition::OnPreBeginPIE(bool bStartSimulate)
{
	check(IsMainWorldPartition());
	GenerateStreaming(EWorldPartitionStreamingMode::PIE);
}

void UWorldPartition::OnEndPIE(bool bStartSimulate)
{
	check(IsMainWorldPartition());
	FlushStreaming();
}

FName UWorldPartition::GetWorldPartitionEditorName()
{
	return EditorHash->GetWorldPartitionEditorName();
}
#endif

void UWorldPartition::Initialize(UWorld* InWorld, const FTransform& InTransform)
{
	UE_SCOPED_TIMER(TEXT("WorldPartition initialize"), LogWorldPartition, Log);
	
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
	InstanceTransform = InTransform;

	check(InitState == EWorldPartitionInitState::Uninitialized);
	InitState = EWorldPartitionInitState::Initializing;

	UWorld* OuterWorld = GetTypedOuter<UWorld>();
	check(OuterWorld);

#if WITH_EDITOR
	bool bEditorOnly = !World->IsPlayInEditor();
	if (bEditorOnly)
	{
		if (!EditorHash)
		{
			UWorldPartitionEditorSpatialHash* EditorSpatialHash = NewObject<UWorldPartitionEditorSpatialHash>(this, NAME_None, RF_Transactional);
			EditorSpatialHash->SetDefaultValues();
			EditorHash = EditorSpatialHash;
		}

		EditorHash->Initialize();

		if (IsMainWorldPartition())
		{
			RegisterDelegates();
		}
	}

	if (!RuntimeHash)
	{
		UWorldPartitionRuntimeSpatialHash* RuntimeSpatialHash = NewObject<UWorldPartitionRuntimeSpatialHash>(this);
		RuntimeSpatialHash->SetDefaultValues();
		RuntimeHash = RuntimeSpatialHash;
	}

	if (bEditorOnly || !IsMainWorldPartition())
	{
		TArray<FAssetData> Assets;
		UPackage* LevelPackage = OuterWorld->PersistentLevel->GetOutermost();

		if (!LevelPackage->GetLoadedPath().GetPackageFName().IsNone())
		{
			TGuardValue<bool> IgnoreAssetRegistryEvents(bIgnoreAssetRegistryEvents, true);

			const FString LevelPathStr = LevelPackage->GetLoadedPath().GetPackageName();
			const FString LevelExternalActorsPath = ULevel::GetExternalActorsPath(LevelPathStr);

			// Do a synchronous scan of the level external actors path.			
			IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			AssetRegistry.ScanPathsSynchronous({LevelExternalActorsPath}, /*bForceRescan*/true, /*bIgnoreBlackListScanFilters*/true);

			FARFilter Filter;
			Filter.bRecursivePaths = true;
			Filter.bIncludeOnlyOnDiskAssets = true;
			Filter.PackagePaths.Add(*LevelExternalActorsPath);

			AssetRegistry.GetAssets(Filter, Assets);
		}

		bool bIsInstanced = (bEditorOnly && !IsRunningCommandlet()) ? OuterWorld->PersistentLevel->IsInstancedLevel() : false;

		FString ReplaceFrom;
		FString ReplaceTo;

		if (bIsInstanced)
		{
			InstancingContext.AddMapping(LevelPackage->GetLoadedPath().GetPackageFName(), LevelPackage->GetFName());

			const FString SourceWorldName = FPaths::GetBaseFilename(LevelPackage->GetLoadedPath().GetPackageName());
			const FString DestWorldName = FPaths::GetBaseFilename(LevelPackage->GetFName().ToString());

			ReplaceFrom = SourceWorldName + TEXT(".") + SourceWorldName;
			ReplaceTo = DestWorldName + TEXT(".") + DestWorldName;
		}

		for (const FAssetData& Asset : Assets)
		{
			TUniquePtr<FWorldPartitionActorDesc>* NewActorDesc = ActorDescList.Emplace(GetActorDescriptor(Asset));
			check(NewActorDesc->IsValid());

			if (bIsInstanced)
			{
				const FString LongActorPackageName = Asset.PackageName.ToString();
				const FString ActorPackageName = FPaths::GetBaseFilename(LongActorPackageName);
				const FString InstancedName = FString::Printf(TEXT("%s_InstanceOf_%s"), *LevelPackage->GetName(), *ActorPackageName);

				InstancingContext.AddMapping(*LongActorPackageName, *InstancedName);

				(*NewActorDesc)->TransformInstance(ReplaceFrom, ReplaceTo, InstanceTransform);
			}

			Actors.Add((*NewActorDesc)->GetGuid(), NewActorDesc);

			if (bEditorOnly)
			{
				HashActorDesc(NewActorDesc->Get());
			}
		}

		if (bEditorOnly)
		{
			// Load the always loaded cell, don't call LoadCells to avoid creating a transaction
			UpdateLoadingEditorCell(EditorHash->GetAlwaysLoadedCell(), true);

			// When loading a subworld, load all actors
			if (!IsMainWorldPartition())
			{
				EditorHash->ForEachCell([this](UWorldPartitionEditorCell* Cell)
				{
					UpdateLoadingEditorCell(Cell, true);
				});
			}
		}
	}
#endif //WITH_EDITOR

	InitState = EWorldPartitionInitState::Initialized;

	if (!IsRunningCookCommandlet())
	{
		UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
		WorldPartitionSubsystem->RegisterWorldPartition(this);
	}

#if WITH_EDITOR
	if (World->IsPlayInEditor())
	{
		PrepareForPIE();
	}
#endif
}

UWorld* UWorldPartition::GetWorld() const
{
	if (World)
	{
		return World;
	}
	return Super::GetWorld();
}

// This will trap all broadcast of LevelRemovedFromWorld and Uninitialize world partition if existing
void UWorldPartition::WorldPartitionOnLevelRemovedFromWorld(ULevel* Level, UWorld* InWorld)
{
	if (UWorldPartition* WorldPartition = Level ? Level->GetWorldPartition() : nullptr)
	{
		WorldPartition->Uninitialize();
	}
}

void UWorldPartition::Uninitialize()
{
	if (IsInitialized())
	{
		check(World);

		InitState = EWorldPartitionInitState::Uninitializing;
		
		// Unload all loaded cells
		if (World->IsGameWorld())
		{
			UpdateStreamingState();
		}

#if WITH_EDITOR
		if (World->IsPlayInEditor())
		{
			CleanupForPIE();
		}
		else if (IsMainWorldPartition())
		{
			if (!IsEngineExitRequested())
			{
				// Unload all Editor cells
				// @todo_ow: Once Metadata is removed from external actor's package, this won't be necessary anymore.
				EditorHash->ForEachCell([this](UWorldPartitionEditorCell* Cell)
				{
					UpdateLoadingEditorCell(Cell, /*bShouldBeLoaded*/false);
				});
			}
			UnregisterDelegates();
		}

		EditorHash = nullptr;
#endif		

		if (!IsRunningCookCommandlet())
		{
			UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
			WorldPartitionSubsystem->UnregisterWorldPartition(this);
		}

		World = nullptr;

		InitState = EWorldPartitionInitState::Uninitialized;
	}
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

#if WITH_EDITOR
void UWorldPartition::RegisterDelegates()
{
	if (GEditor && !IsTemplate())
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AssetRegistry.OnAssetAdded().AddUObject(this, &UWorldPartition::OnAssetAdded);
		AssetRegistry.OnAssetRemoved().AddUObject(this, &UWorldPartition::OnAssetRemoved);
		AssetRegistry.OnAssetUpdated().AddUObject(this, &UWorldPartition::OnAssetUpdated);

		FEditorDelegates::PreBeginPIE.AddUObject(this, &UWorldPartition::OnPreBeginPIE);
		FEditorDelegates::EndPIE.AddUObject(this, &UWorldPartition::OnEndPIE);
	}
}

void UWorldPartition::UnregisterDelegates()
{
	if (GEditor && !IsTemplate())
	{
		if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
		{
			IAssetRegistry& AssetRegistry = AssetRegistryModule->Get();
			AssetRegistry.OnAssetAdded().RemoveAll(this);
			AssetRegistry.OnAssetRemoved().RemoveAll(this);
			AssetRegistry.OnAssetUpdated().RemoveAll(this);
		}

		FEditorDelegates::PreBeginPIE.RemoveAll(this);
		FEditorDelegates::EndPIE.RemoveAll(this);
	}
}

void UWorldPartition::ApplyActorTransform(AActor* InActor, const FTransform& InTransform)
{
	if (!InTransform.Equals(FTransform::Identity))
	{
		FLevelUtils::FApplyLevelTransformParams TransformParams(InActor->GetLevel(), InTransform);
		TransformParams.Actor = InActor;
		TransformParams.bDoPostEditMove = true;
		FLevelUtils::ApplyLevelTransform(TransformParams);
	}
}

void UWorldPartition::ForEachIntersectingActorDesc(const FBox& Box, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate) const
{
	EditorHash->ForEachIntersectingActor(Box, [&ActorClass, Predicate](const FWorldPartitionActorDesc* ActorDesc)
	{
		if (ActorDesc->GetActorClass()->IsChildOf(ActorClass))
		{
			Predicate(ActorDesc);
		}
	});
}

void UWorldPartition::ForEachActorDesc(TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate) const
{
	for (const TPair<FGuid, TUniquePtr<FWorldPartitionActorDesc>*> Pair : Actors)
	{
		FWorldPartitionActorDesc* ActorDesc = Pair.Value->Get();
		if (ActorDesc->GetActorClass()->IsChildOf(ActorClass))
		{
			if (!Predicate(ActorDesc))
			{
				return;
			}
		}
	}
}

const FWorldPartitionActorDesc* UWorldPartition::GetActorDesc(const FGuid& Guid) const
{
	const TUniquePtr<FWorldPartitionActorDesc>* const * ActorDesc = Actors.Find(Guid);
	return ActorDesc ? (*ActorDesc)->Get() : nullptr;
}

FWorldPartitionActorDesc* UWorldPartition::GetActorDesc(const FGuid& Guid)
{
	TUniquePtr<FWorldPartitionActorDesc>** ActorDesc = Actors.Find(Guid);
	return ActorDesc ? (*ActorDesc)->Get() : nullptr;
}

bool UWorldPartition::IsSimulating() const
{
	return GEditor->bIsSimulatingInEditor || !!GEditor->PlayWorld;
}

void UWorldPartition::LoadEditorCells(const FBox& Box)
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
			UpdateLoadingEditorCell(Cell, true);
		}
	}
}

bool UWorldPartition::RefreshLoadedEditorCells()
{
	auto GetCellsToRefresh = [this](TArray<UWorldPartitionEditorCell*>& CellsToRefresh)
	{
		CellsToRefresh.Add(EditorHash->GetAlwaysLoadedCell());
		EditorHash->ForEachCell([&CellsToRefresh](UWorldPartitionEditorCell* Cell)
		{
			if (Cell->bLoaded)
			{
				CellsToRefresh.Add(Cell);
			}
		});
		return !CellsToRefresh.IsEmpty();
	};

	return UpdateEditorCells(GetCellsToRefresh, /*bIsCellShouldBeLoaded*/true);
}

void UWorldPartition::UnloadEditorCells(const FBox& Box)
{
	auto GetCellsToUnload = [this, Box](TArray<UWorldPartitionEditorCell*>& CellsToUnload)
	{
		return EditorHash->GetIntersectingCells(Box, CellsToUnload) > 0;
	};

	UpdateEditorCells(GetCellsToUnload, /*bIsCellShouldBeLoaded*/false);
}

bool UWorldPartition::UpdateEditorCells(TFunctionRef<bool(TArray<UWorldPartitionEditorCell*>&)> GetCellsToProcess, bool bIsCellShouldBeLoaded)
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
		for (FWorldPartitionActorDesc* ActorDesc : Cell->LoadedActors)
		{
			if (!bIsCellShouldBeLoaded || !ShouldActorBeLoaded(ActorDesc))
			{
				UnloadCount.FindOrAdd(ActorDesc, 0)++;
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
			AActor* LoadedActor = ActorDesc->GetActor();
			check(LoadedActor);
			UPackage* ActorPackage = LoadedActor->GetExternalPackage();
			if (ActorPackage && ActorPackage->IsDirty())
			{
				ModifiedPackages.Add(ActorPackage);
			}
			bIsUpdateUnloadingActors = true;
		}
	}

	// Make sure we save modified actor packages before unloading
	FEditorFileUtils::EPromptReturnCode RetCode = FEditorFileUtils::PR_Success;
	if (ModifiedPackages.Num())
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
		UpdateLoadingEditorCell(Cell, bIsCellShouldBeLoaded);
	}

	if (bIsUpdateUnloadingActors)
	{
		GEditor->ResetTransaction(LOCTEXT("UnloadingEditorCellsResetTrans", "Unloading Cells"));
	}

	return true;
}

bool UWorldPartition::ShouldActorBeLoaded(const FWorldPartitionActorDesc* ActorDesc) const
{
	// No filtering when running cook commandlet
	if (IsRunningCookCommandlet())
	{
		return true;
	}

	if (const AWorldDataLayers* WorldDataLayers = AWorldDataLayers::Get(GetWorld()))
	{
		uint32 NumValidLayers = 0;
		for (const FName& DataLayerName : ActorDesc->GetDataLayers())
		{
			if (const UDataLayer* DataLayer = WorldDataLayers->GetDataLayerFromName(DataLayerName))
			{
				if (DataLayer->IsDynamicallyLoadedInEditor())
				{
					return true;
				}
				NumValidLayers++;
			}
		}
		return !NumValidLayers;
	}

	return true;
};

// Loads actors in Editor cell
void UWorldPartition::UpdateLoadingEditorCell(UWorldPartitionEditorCell* Cell, bool bShouldBeLoaded)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartition::UpdateLoadingEditorCell);
	
	FWorldPartionCellUpdateContext CellUpdateContext(this);

	UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartition::UpdateLoadingEditorCell 0x%08llx [%s]"), Cell, bShouldBeLoaded ? TEXT("Load") : TEXT("Unload"));

	Cell->Modify(false);

	auto UnloadActor = [this](FWorldPartitionActorDesc* ActorDesc, AActor* Actor)
	{
		check(Actor);

		const uint32 ActorRefCount = ActorDesc->DecHardRefCount();
		UE_LOG(LogWorldPartition, Verbose, TEXT(" ==> Unreferenced loaded actor %s(%d) [UWorldPartition::UpdateLoadingEditorCell]"), *Actor->GetFullName(), ActorRefCount);

		if (!ActorRefCount)
		{
			UnregisterActor(Actor);
		}
	};

	if (!bShouldBeLoaded)
	{
		for (FWorldPartitionActorDesc* ActorDesc: Cell->LoadedActors.Array())
		{
			// Here we test for null Actor only for when UpdateLoadingEditorCell is called by UWorldPartition::Uninitialize() when exiting the editor (see StaticExit() / GExitPurge).
			AActor* Actor = ActorDesc->GetActor();
			check(Actor || GExitPurge);
			if (Actor)
			{
				UnloadActor(ActorDesc, Actor);
			}
		}

		Cell->LoadedActors.Empty();
	}
	else
	{
		TGuardValue<bool> IsEditorLoadingPackageGuard(GIsEditorLoadingPackage, true);

		TArray<FWorldPartitionActorDesc*> ActorsToUnload;
		for (FWorldPartitionActorDesc* ActorDesc: Cell->Actors.Array())
		{
			AActor* Actor = ActorDesc->GetActor();

			// The actor could be either loaded but with no cells loaded (in the case of a reference from another actor, for example)
			// or directly referenced by a loaded cell.
			check(Actor || !ActorDesc->GetHardRefCount());

			// Filter actor against DataLayers
			if (ShouldActorBeLoaded(ActorDesc))
			{
				bool bIsAlreadyInLoadedActors = false;
				Cell->LoadedActors.Add(ActorDesc, &bIsAlreadyInLoadedActors);

				if (bIsAlreadyInLoadedActors)
				{
					// We already hold a reference to this actor
					check(Actor);
					check(ActorDesc->GetHardRefCount());
					UE_LOG(LogWorldPartition, Verbose, TEXT(" ==> Skipped already loaded actor %s"), *Actor->GetFullName());
				}
				else
				{
					const uint32 ActorRefCount = ActorDesc->IncHardRefCount();

					if (ActorRefCount == 1)
					{
						// Register actor
						Actor = RegisterActor(ActorDesc);
					}
					else
					{
						check(Actor);
						UE_LOG(LogWorldPartition, Verbose, TEXT(" ==> Referenced unloaded actor %s(%d)"), *Actor->GetFullName(), ActorRefCount);
					}

					check(Actor && Actor->GetActorGuid() == ActorDesc->GetGuid());
				}
			}
			else if (Cell->LoadedActors.Remove(ActorDesc))
			{
				UnloadActor(ActorDesc, Actor);
			}
		}
	}

	if (Cell->bLoaded != bShouldBeLoaded)
	{
		Cell->bLoaded = bShouldBeLoaded;

		if (Cell->bLoaded)
		{
			EditorHash->OnCellLoaded(Cell);
		}
		else
		{
			EditorHash->OnCellUnloaded(Cell);
		}
	}
}

#endif

AActor* UWorldPartition::RegisterActor(FWorldPartitionActorDesc* ActorDesc)
{
#if WITH_EDITOR
	check(ActorDesc);
	AActor* Actor = ActorDesc->GetActor();

	if (!Actor)
	{
		Actor = ActorDesc->Load(InstancingContext.IsInstanced() ? &InstancingContext : nullptr);
		UE_LOG(LogWorldPartition, Verbose, TEXT(" ==> Loaded %s"), *Actor->GetFullName());
	}

	check(Actor);
	check(Actor->IsPackageExternal());
	check(ActorDesc->GetActor() == Actor);

	ApplyActorTransform(Actor, InstanceTransform);

	TGuardValue<ITransaction*> TransGuard(GUndo, nullptr);
	Actor->GetLevel()->AddLoadedActor(Actor);

	OnActorRegisteredEvent.Broadcast(*Actor, true);

	UE_LOG(LogWorldPartition, Verbose, TEXT(" ==> Registered loaded actor %s"), *Actor->GetFullName());

	return Actor;
#else
	return nullptr;
#endif	
}

void UWorldPartition::UnregisterActor(AActor* Actor)
{
#if WITH_EDITOR
	FWorldPartitionActorDesc* ActorDesc = GetActorDesc(Actor->GetActorGuid());
	check(ActorDesc);

	if (!Actor->IsPendingKill())
	{
		OnActorRegisteredEvent.Broadcast(*Actor, false);

		Actor->GetLevel()->RemoveLoadedActor(Actor);
	}

	ActorDesc->Unload();

	ApplyActorTransform(Actor, InstanceTransform.Inverse());

	bForceGarbageCollection = true;
	bForceGarbageCollectionPurge = true;

	UE_LOG(LogWorldPartition, Verbose, TEXT(" ==> Unregistered loaded actor %s"), *Actor->GetFullName());
#endif
}

#if WITH_EDITOR
void UWorldPartition::OnAssetAdded(const FAssetData& InAssetData)
{
	if (ShouldHandleAssetEvent(InAssetData))
	{
		TUniquePtr<FWorldPartitionActorDesc>* NewActorDesc = ActorDescList.Emplace(GetActorDescriptor(InAssetData));
		check(NewActorDesc->IsValid());

		check(!Actors.Contains((*NewActorDesc)->GetGuid()));
		Actors.Add((*NewActorDesc)->GetGuid(), NewActorDesc);

		HashActorDesc(NewActorDesc->Get());
	}
}

void UWorldPartition::OnAssetRemoved(const FAssetData& InAssetData)
{
	if (ShouldHandleAssetEvent(InAssetData))
	{
		TUniquePtr<FWorldPartitionActorDesc> NewActorDesc = GetActorDescriptor(InAssetData);
		check(NewActorDesc.IsValid());

		TUniquePtr<FWorldPartitionActorDesc>* ExistingActorDesc = Actors.FindChecked(NewActorDesc->GetGuid());

		UnhashActorDesc(ExistingActorDesc->Get());

		Actors.Remove((*ExistingActorDesc)->GetGuid());
		ExistingActorDesc->Release();
	}
}

void UWorldPartition::OnAssetUpdated(const FAssetData& InAssetData)
{
	if (ShouldHandleAssetEvent(InAssetData))
	{
		TUniquePtr<FWorldPartitionActorDesc> NewActorDesc = GetActorDescriptor(InAssetData);
		check(NewActorDesc.IsValid());
			
		TUniquePtr<FWorldPartitionActorDesc>* ExistingActorDesc = Actors.FindChecked(NewActorDesc->GetGuid());

		UnhashActorDesc(ExistingActorDesc->Get());

		// This is required to support external load references
		NewActorDesc->TransferRefCounts(ExistingActorDesc->Get());

		*ExistingActorDesc = MoveTemp(NewActorDesc);
		
		HashActorDesc(ExistingActorDesc->Get());
	}
}

bool UWorldPartition::ShouldHandleAssetEvent(const FAssetData& InAssetData)
{
	// Ignore asset event when specifically asking to
	if (bIgnoreAssetRegistryEvents)
	{
		return false;
	}

	// Ignore in-memory assets until they gets saved
	if (InAssetData.HasAnyPackageFlags(PKG_NewlyCreated))
	{
		return false;
	}

	// Only handle actors
	if (!InAssetData.GetClass()->IsChildOf<AActor>())
	{
		return false;
	}

	// Make sure asset contains the required tags
	static FName NAME_ActorMetaDataClass(TEXT("ActorMetaDataClass"));
	static FName NAME_ActorMetaData(TEXT("ActorMetaData"));
	if (!InAssetData.FindTag(NAME_ActorMetaDataClass) || !InAssetData.FindTag(NAME_ActorMetaData))
	{
		return false;
	}

	// Only handle assets that belongs to our level
	auto RemoveAfterFirstDot = [](const FString& InValue)
	{
		int32 DotIndex;
		if (InValue.FindChar(TEXT('.'), DotIndex))
		{
			return InValue.LeftChop(InValue.Len() - DotIndex);
		}
		return InValue;
	};

	const FString ThisLevelPath = GetPackage()->GetLoadedPath().GetPackageName();
	const FString AssetLevelPath = RemoveAfterFirstDot(InAssetData.ObjectPath.ToString());
	return (ThisLevelPath == AssetLevelPath);
}

TUniquePtr<FWorldPartitionActorDesc> UWorldPartition::GetActorDescriptor(const FAssetData& InAssetData)
{
	FString ActorClass;
	static FName NAME_ActorMetaDataClass(TEXT("ActorMetaDataClass"));
	if (InAssetData.GetTagValue(NAME_ActorMetaDataClass, ActorClass))
	{
		FString ActorMetaDataStr;
		static FName NAME_ActorMetaData(TEXT("ActorMetaData"));
		if (InAssetData.GetTagValue(NAME_ActorMetaData, ActorMetaDataStr))
		{
			FWorldPartitionActorDescInitData ActorDescInitData;
			ActorDescInitData.NativeClass = FindObjectChecked<UClass>(ANY_PACKAGE, *ActorClass, true);
			ActorDescInitData.PackageName = InAssetData.PackageName;
			ActorDescInitData.ActorPath = InAssetData.ObjectPath;
			FBase64::Decode(ActorMetaDataStr, ActorDescInitData.SerializedData);

			TUniquePtr<FWorldPartitionActorDesc> NewActorDesc(AActor::CreateClassActorDesc(ActorDescInitData.NativeClass));
			NewActorDesc->Init(ActorDescInitData);
			return NewActorDesc;
		}
	}

	return nullptr;
}

void UWorldPartition::HashActorDesc(FWorldPartitionActorDesc* ActorDesc)
{
	check(ActorDesc);
	check(EditorHash);
	EditorHash->HashActor(ActorDesc);
}

void UWorldPartition::UnhashActorDesc(FWorldPartitionActorDesc* ActorDesc)
{
	check(ActorDesc);
	check(EditorHash);
	EditorHash->UnhashActor(ActorDesc);
}
#endif

void UWorldPartition::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

#if WITH_EDITOR
	if (Ar.GetPortFlags() & PPF_Duplicate)
	{
		Ar << EditorHash;
	}
#endif
}

UWorldPartitionStreamingPolicy* UWorldPartition::GetStreamingPolicy() const
{
	check(GetWorld());
	if (!StreamingPolicy && GetWorld())
	{
		StreamingPolicy = NewObject<UWorldPartitionStreamingPolicy>(const_cast<UWorldPartition*>(this), GetWorld()->GetWorldSettings()->WorldPartitionStreamingPolicyClass.Get());
	}
	return StreamingPolicy;
}

void UWorldPartition::Tick(float DeltaSeconds)
{
#if WITH_EDITOR
	EditorHash->Tick(DeltaSeconds);

	if (bForceGarbageCollection)
	{
		GEngine->ForceGarbageCollection(bForceGarbageCollectionPurge);

		bForceGarbageCollection = false;
		bForceGarbageCollectionPurge = false;
	}
#endif
}

void UWorldPartition::UpdateStreamingState()
{
	if (GetWorld()->IsGameWorld())
	{
		GetStreamingPolicy()->UpdateStreamingState();
	}
}

class ULevel* UWorldPartition::GetPreferredLoadedLevelToAddToWorld() const
{
	if (GetWorld()->IsGameWorld())
	{
		return GetStreamingPolicy()->GetPreferredLoadedLevelToAddToWorld();
	}
	return nullptr;
}

FVector2D UWorldPartition::GetDrawRuntimeHash2DDesiredFootprint(const FVector2D& CanvasSize)
{
	check(GetWorld()->IsGameWorld());
	return GetStreamingPolicy()->GetDrawRuntimeHash2DDesiredFootprint(CanvasSize);
}

void UWorldPartition::DrawRuntimeHash2D(class UCanvas* Canvas, const FVector2D& PartitionCanvasOffset, const FVector2D& PartitionCanvasSize)
{
	check(GetWorld()->IsGameWorld());
	GetStreamingPolicy()->DrawRuntimeHash2D(Canvas, PartitionCanvasOffset, PartitionCanvasSize);
}

void UWorldPartition::DrawRuntimeHash3D()
{
	check(GetWorld()->IsGameWorld());
	GetStreamingPolicy()->DrawRuntimeHash3D();
}

#if WITH_EDITOR
void UWorldPartition::PrepareForPIE()
{
	check(World->IsPlayInEditor());

	if (!IsMainWorldPartition())
	{
		GenerateStreaming(EWorldPartitionStreamingMode::PIE);
	}

	GetStreamingPolicy()->PrepareForPIE();
}

void UWorldPartition::CleanupForPIE()
{
	check(World->IsPlayInEditor());

	FlushStreaming();
}

bool UWorldPartition::GenerateStreaming(EWorldPartitionStreamingMode Mode, TArray<FString>* OutPackagesToGenerate)
{
	check(RuntimeHash);
	return RuntimeHash->GenerateStreaming(Mode, GetStreamingPolicy(), OutPackagesToGenerate);
}

bool UWorldPartition::PopulateGeneratedPackageForCook(UPackage* InPackage, const FString& InPackageRelativePath, const FString& InPackageCookName)
{
	check(RuntimeHash);
	return RuntimeHash->PopulateGeneratedPackageForCook(InPackage, InPackageRelativePath, InPackageCookName);
}

void UWorldPartition::FinalizeGeneratedPackageForCook()
{
	check(RuntimeHash);
	RuntimeHash->FinalizeGeneratedPackageForCook();
}

void UWorldPartition::FlushStreaming()
{
	RuntimeHash->FlushStreaming();
}

void UWorldPartition::GenerateHLOD(ISourceControlHelper* SourceControlHelper)
{
	RuntimeHash->GenerateHLOD(SourceControlHelper);
}

void UWorldPartition::GenerateNavigationData()
{
	RuntimeHash->GenerateNavigationData();
}

void UWorldPartition::DumpActorDescs(const FString& Path)
{
	if (FArchive* LogFile = IFileManager::Get().CreateFileWriter(*Path))
	{
		FString LineEntry = TEXT("Guid, Class, Name, BVCenterX, BVCenterY, BVCenterZ, BVExtentX, BVExtentY, BVExtentZ") LINE_TERMINATOR;
		LogFile->Serialize(TCHAR_TO_ANSI(*LineEntry), LineEntry.Len());
			
		ForEachActorDesc(AActor::StaticClass(), [LogFile, &LineEntry](const FWorldPartitionActorDesc* ActorDesc)
		{
				LineEntry = FString::Printf(
				TEXT("%s, %s, %s, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f") LINE_TERMINATOR, 
				*ActorDesc->GetGuid().ToString(), 
				*ActorDesc->GetClass().ToString(), 
				*FPaths::GetExtension(ActorDesc->GetActorPath().ToString()), 
				ActorDesc->GetBounds().GetCenter().X,
				ActorDesc->GetBounds().GetCenter().Y,
				ActorDesc->GetBounds().GetCenter().Z,
				ActorDesc->GetBounds().GetExtent().X,
				ActorDesc->GetBounds().GetExtent().Y,
				ActorDesc->GetBounds().GetExtent().Z
			);
			LogFile->Serialize(TCHAR_TO_ANSI(*LineEntry), LineEntry.Len());
			return true;
		});

		LogFile->Close();
		delete LogFile;
	}
}

void UWorldPartition::OnPreFixupForPIE(int32 InPIEInstanceID, FSoftObjectPath& ObjectPath)
{
	if (GetWorld()->IsPlayInEditor())
	{
		GetStreamingPolicy()->OnPreFixupForPIE(InPIEInstanceID, ObjectPath);
	}
}

FBox UWorldPartition::GetWorldBounds() const
{
	FBox WorldBounds(ForceInit);
	for (const auto& Pair : Actors)
	{
		const FWorldPartitionActorDesc* ActorDesc = Pair.Value->Get();

		switch (ActorDesc->GetGridPlacement())
		{
			case EActorGridPlacement::Location:
			{
				FVector Location = ActorDesc->GetOrigin();
				WorldBounds += FBox(Location, Location);
			}
			break;
			case EActorGridPlacement::Bounds:
			{
				WorldBounds += ActorDesc->GetBounds();
			}
			break;
		}
	}
	return WorldBounds;
}
#endif

#undef LOCTEXT_NAMESPACE