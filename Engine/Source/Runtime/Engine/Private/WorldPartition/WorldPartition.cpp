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
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "Algo/Accumulate.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/WorldSettings.h"
#include "ProfilingDebugging/ScopedTimers.h"

#if WITH_EDITOR
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "UnrealEdMisc.h"
#include "WorldPartition/IWorldPartitionEditorModule.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartition/WorldPartitionEditorSpatialHash.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"
#include "WorldPartition/WorldPartitionEditorPerProjectUserSettings.h"
#include "Modules/ModuleManager.h"
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

				// Save last loaded cells settings (ignore while running commandlets)
				if (!IsRunningCommandlet())
				{
					TArray<FVector> EditorGridLastLoadedCells;
					WorldPartition->EditorHash->ForEachCell([this, &EditorGridLastLoadedCells](UWorldPartitionEditorCell* Cell)
					{
						if ((Cell != WorldPartition->EditorHash->GetAlwaysLoadedCell()) && Cell->bLoaded)
						{
							EditorGridLastLoadedCells.Add(Cell->Bounds.GetCenter());
						}
					});

					GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->SetEditorGridLoadedCells(WorldPartition->GetWorld(), EditorGridLastLoadedCells);
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
	UE_SCOPED_TIMER(TEXT("WorldPartition initialize"), LogWorldPartition);
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
		FName PackageName = LevelPackage->GetLoadedPath().GetPackageFName();

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

		const bool bRegisterDelegates = bEditorOnly && IsMainWorldPartition();
		UActorDescContainer::Initialize(PackageName, bRegisterDelegates);
		check(bContainerInitialized);

		for (UActorDescContainer::TIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
		{
			if (bIsInstanced)
			{
				const FString LongActorPackageName = ActorDescIterator->ActorPackage.ToString();
				const FString ActorPackageName = FPaths::GetBaseFilename(LongActorPackageName);
				const FString InstancedName = FString::Printf(TEXT("%s_InstanceOf_%s"), *LevelPackage->GetName(), *ActorPackageName);

				InstancingContext.AddMapping(*LongActorPackageName, *InstancedName);

				ActorDescIterator->TransformInstance(ReplaceFrom, ReplaceTo, InstanceTransform);
			}

			ActorDescIterator->OnRegister();

			if (bEditorOnly)
			{
				HashActorDesc(*ActorDescIterator);
			}
		}

		if (bEditorOnly)
		{
			// Make sure to preload any AInfo actors first (mainly for ShouldActorBeLoaded as it needs to have access to the WorldDataLayers actor)
			TArray<FWorldPartitionHandle> AlwaysLoadedActors = EditorHash->GetAlwaysLoadedCell()->Actors.Array();			
			AlwaysLoadedActors.Sort([](const FWorldPartitionHandle& A, const FWorldPartitionHandle& B) { return A->GetActorClass()->IsChildOf<AInfo>(); });
			TArray<FWorldPartitionReference> LoadedActors(AlwaysLoadedActors);

			// Load the always loaded cell, don't call LoadCells to avoid creating a transaction
			UpdateLoadingEditorCell(EditorHash->GetAlwaysLoadedCell(), true);

			// Load more cells depending on the user's settings
			// Skipped when running from a commandlet
			if (!IsRunningCommandlet())
			{
				// Autoload all cells if the world is smaller than the project setting's value
				IWorldPartitionEditorModule& WorldPartitionEditorModule = FModuleManager::LoadModuleChecked<IWorldPartitionEditorModule>("WorldPartitionEditor");
				const float AutoCellLoadingMaxWorldSize = WorldPartitionEditorModule.GetAutoCellLoadingMaxWorldSize();
				FVector WorldSize = GetEditorWorldBounds().GetSize();
				const bool bItsASmallWorld = WorldSize.X <= AutoCellLoadingMaxWorldSize && WorldSize.Y <= AutoCellLoadingMaxWorldSize && WorldSize.Z <= AutoCellLoadingMaxWorldSize;
			
				// When loading a subworld, load all actors
				const bool bWorldIsSubPartition = !IsMainWorldPartition();

				if (bWorldIsSubPartition || bItsASmallWorld)
				{
					EditorHash->ForEachCell([this](UWorldPartitionEditorCell* Cell)
					{
						UpdateLoadingEditorCell(Cell, true);
					});
				}

				// Load last loaded cells
				if (WorldPartitionEditorModule.GetEnableLoadingOfLastLoadedCells())
				{
					const TArray<FVector>& EditorGridLastLoadedCells = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetEditorGridLoadedCells(InWorld);

					for (const FVector& EditorGridLastLoadedCellCenter : EditorGridLastLoadedCells)
					{
						EditorHash->ForEachIntersectingCell(FBox(&EditorGridLastLoadedCellCenter, 1), [this](UWorldPartitionEditorCell* Cell)
						{
							UpdateLoadingEditorCell(Cell, true);
						});
					}
				}
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

void UWorldPartition::RegisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider* StreamingSource)
{
	StreamingSourceProviders.Add(StreamingSource);
}

bool UWorldPartition::UnregisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider* StreamingSource)
{
	return !!StreamingSourceProviders.Remove(StreamingSource);
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
	Super::Uninitialize();

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
		else
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

void UWorldPartition::CleanupWorldPartition()
{
	Uninitialize();
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
	Super::RegisterDelegates();
	if (GEditor && !IsTemplate())
	{
		FEditorDelegates::PreBeginPIE.AddUObject(this, &UWorldPartition::OnPreBeginPIE);
		FEditorDelegates::EndPIE.AddUObject(this, &UWorldPartition::OnEndPIE);
	}
}

void UWorldPartition::UnregisterDelegates()
{
	Super::UnregisterDelegates();
	if (GEditor && !IsTemplate())
	{
		FEditorDelegates::PreBeginPIE.RemoveAll(this);
		FEditorDelegates::EndPIE.RemoveAll(this);
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
	for (UActorDescContainer::TConstIterator<> ActorDescIterator(this, ActorClass); ActorDescIterator; ++ActorDescIterator)
	{
		if (!Predicate(*ActorDescIterator))
		{
			return;
		}
	}
}
#endif

const TArray<FWorldPartitionStreamingSource>& UWorldPartition::GetStreamingSources() const
{
	if (GetWorld()->IsGameWorld())
	{
		return GetStreamingPolicy()->GetStreamingSources();
	}

	static TArray<FWorldPartitionStreamingSource> EmptyStreamingSources;
	return EmptyStreamingSources;
}

#if WITH_EDITOR
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
	FWorldPartionCellUpdateContext CellUpdateContext(this);

	auto GetCellsToUnload = [this, Box](TArray<UWorldPartitionEditorCell*>& CellsToUnload)
	{
		return EditorHash->GetIntersectingCells(Box, CellsToUnload) > 0;
	};

	UpdateEditorCells(GetCellsToUnload, /*bIsCellShouldBeLoaded*/false);
}

bool UWorldPartition::AreEditorCellsLoaded(const FBox& Box)
{
	TArray<UWorldPartitionEditorCell*> CellsToLoad;
	if (EditorHash->GetIntersectingCells(Box, CellsToLoad))
	{
		for (UWorldPartitionEditorCell* Cell : CellsToLoad)
		{
			if (!Cell->bLoaded)
			{
				return false;
			}
		}
	}

	return true;
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
		for (const FWorldPartitionReference& ActorDesc : Cell->LoadedActors)
		{
			if (!bIsCellShouldBeLoaded || !ShouldActorBeLoaded(*ActorDesc))
			{
				UnloadCount.FindOrAdd(*ActorDesc, 0)++;
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

	bool bPotentiallyUnloadedActors = false;

	if (!bShouldBeLoaded)
	{
		bPotentiallyUnloadedActors = !Cell->LoadedActors.IsEmpty();
		Cell->LoadedActors.Empty();
	}
	else
	{
		for (FWorldPartitionHandle& ActorHandle: Cell->Actors)
		{
			AActor* Actor = ActorHandle->GetActor();

			// The actor could be either loaded but with no cells loaded (in the case of a reference from another actor, for example)
			// or directly referenced by a loaded cell.
			check(Actor || !ActorHandle->GetHardRefCount());

			// Filter actor against DataLayers
			if (ShouldActorBeLoaded(*ActorHandle))
			{
				Cell->LoadedActors.Add(ActorHandle);
			}
			else
			{
				// Don't call LoadedActors.Remove(ActorHandle) right away, as this will create a temporary reference and might try to load.
				for (const FWorldPartitionReference& ActorReference: Cell->LoadedActors)
				{
					if (ActorReference == ActorHandle)
					{
						Cell->LoadedActors.Remove(ActorHandle);
						bPotentiallyUnloadedActors = true;
						break;
					}
				}
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

	if (bPotentiallyUnloadedActors)
	{
		bForceGarbageCollection = true;
		bForceGarbageCollectionPurge = true;
	}
}

void UWorldPartition::OnActorDescAdded(const TUniquePtr<FWorldPartitionActorDesc>& NewActorDesc)
{
	HashActorDesc(NewActorDesc.Get());

	NewActorDesc->OnRegister();

	if (WorldPartitionEditor)
	{
		WorldPartitionEditor->Refresh();
	}
}

void UWorldPartition::OnActorDescRemoved(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc)
{
	UnhashActorDesc(ActorDesc.Get());
	ActorDesc->OnUnregister();
		
	if (WorldPartitionEditor)
	{
		WorldPartitionEditor->Refresh();
	}
}

void UWorldPartition::OnActorDescUpdating(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc)
{
	UnhashActorDesc(ActorDesc.Get());
}

void UWorldPartition::OnActorDescUpdated(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc)
{
	HashActorDesc(ActorDesc.Get());

	if (WorldPartitionEditor)
	{
		WorldPartitionEditor->Refresh();
	}
}

void UWorldPartition::HashActorDesc(FWorldPartitionActorDesc* ActorDesc)
{
	check(ActorDesc);
	check(EditorHash);
	FWorldPartitionHandle ActorHandle(this, ActorDesc->GetGuid());
	EditorHash->HashActor(ActorHandle);
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
	Super::Serialize(Ar);

#if WITH_EDITOR
	if (Ar.GetPortFlags() & PPF_Duplicate)
	{
		Ar << EditorHash;
	}
#endif
}

void UWorldPartition::BeginDestroy()
{
	Super::BeginDestroy();

#if WITH_EDITOR
	LoadedSubobjects.Empty();
#endif
}

#if WITH_EDITOR
UObject* UWorldPartition::LoadSubobject(const TCHAR* SubObjectPath)
{
	for (UActorDescContainer::TIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
	{
		FWorldPartitionActorDesc* ActorDesc = *ActorDescIterator;

		if (FString(ActorDesc->ActorPath.ToString()).EndsWith(SubObjectPath))
		{
			int32 ReferenceIndex = LoadedSubobjects.Emplace(this, ActorDesc->GetGuid());
			return LoadedSubobjects[ReferenceIndex]->GetActor();
		}
	}

	return nullptr;
}
#endif

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
void UWorldPartition::DrawRuntimeHashPreview()
{
	RuntimeHash->DrawPreview();
}

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

		TArray<const FWorldPartitionActorDesc*> ActorDescs;
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
			LineEntry = FString::Printf(
				TEXT("%s, %s, %s, %s, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f") LINE_TERMINATOR, 
				*ActorDescIterator->GetGuid().ToString(), 
				*ActorDescIterator->GetClass().ToString(), 
				*FPaths::GetExtension(ActorDescIterator->GetActorPath().ToString()), 
				ActorDescIterator->GetActor() ? *ActorDescIterator->GetActor()->GetActorLabel(false) : TEXT("None"),
				ActorDescIterator->GetBounds().GetCenter().X,
				ActorDescIterator->GetBounds().GetCenter().Y,
				ActorDescIterator->GetBounds().GetCenter().Z,
				ActorDescIterator->GetBounds().GetExtent().X,
				ActorDescIterator->GetBounds().GetExtent().Y,
				ActorDescIterator->GetBounds().GetExtent().Z
			);
			LogFile->Serialize(TCHAR_TO_ANSI(*LineEntry), LineEntry.Len());
		}

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
	for (UActorDescContainer::TConstIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
	{
		switch (ActorDescIterator->GetGridPlacement())
		{
			case EActorGridPlacement::Location:
			{
				FVector Location = ActorDescIterator->GetOrigin();
				WorldBounds += FBox(Location, Location);
			}
			break;
			case EActorGridPlacement::Bounds:
			{
				WorldBounds += ActorDescIterator->GetBounds();
			}
			break;
		}
	}
	return WorldBounds;
}

FBox UWorldPartition::GetEditorWorldBounds() const
{
	return EditorHash->GetEditorWorldBounds();
}
#endif

#undef LOCTEXT_NAMESPACE