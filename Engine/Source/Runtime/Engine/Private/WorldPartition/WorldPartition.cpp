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
#include "Algo/Transform.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/WorldSettings.h"
#include "ProfilingDebugging/ScopedTimers.h"

#if WITH_EDITOR
#include "LevelUtils.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "LevelEditorViewport.h"
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
#include "GameDelegates.h"
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
					TArray<FName> EditorGridLastLoadedCells;
					WorldPartition->EditorHash->ForEachCell([this, &EditorGridLastLoadedCells](UWorldPartitionEditorCell* Cell)
					{
						if ((Cell != WorldPartition->EditorHash->GetAlwaysLoadedCell()) && Cell->bLoaded)
						{
							EditorGridLastLoadedCells.Add(Cell->GetFName());
						}
					});

					GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->SetEditorGridLoadedCells(WorldPartition->GetWorld(), EditorGridLastLoadedCells);

					GEditor->ResetTransaction(LOCTEXT("LoadingEditorCellsResetTrans", "Editor Cells Loading State Changed"));
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
	// Gather all modified/newly created actors for PIE so they can be duplicated when streaming their cell
	for (AActor* Actor : World->PersistentLevel->Actors)
	{
		if (Actor && !Actor->IsPendingKill() && Actor->GetPackage()->IsDirty())
		{
			World->PersistentLevel->ActorsModifiedForPIE.Add(Actor->GetFName(), Actor);
		}
	}

	check(IsMainWorldPartition());

	OnBeginPlay(EWorldPartitionStreamingMode::PIE);
}

void UWorldPartition::OnBeginPlay(EWorldPartitionStreamingMode Mode)
{
	GenerateStreaming(Mode);
	RuntimeHash->OnBeginPlay(Mode);
	GetStreamingPolicy()->OnBeginPlay();
}

void UWorldPartition::OnEndPlay()
{
	check(IsMainWorldPartition());

	FlushStreaming();
	RuntimeHash->OnEndPlay();
	GetStreamingPolicy()->OnEndPlay();

	World->PersistentLevel->ActorsModifiedForPIE.Empty();
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

	check(IsMainWorldPartition());

#if WITH_EDITOR
	bool bEditorOnly = !World->IsGameWorld();
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

	if (bEditorOnly || IsRunningGame())
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

		const bool bRegisterDelegates = bEditorOnly;
		UActorDescContainer::Initialize(World, PackageName, bRegisterDelegates);
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
	}

	if (bEditorOnly)
	{
		// Make sure to preload any AInfo actors first (mainly for ShouldActorBeLoaded as it needs to have access to the WorldDataLayers actor)
		TArray<UWorldPartitionEditorCell::FActorHandle> AlwaysLoadedActors = EditorHash->GetAlwaysLoadedCell()->Actors.Array();
		AlwaysLoadedActors.Sort([](const UWorldPartitionEditorCell::FActorHandle& A, const UWorldPartitionEditorCell::FActorHandle& B) { return A->GetActorClass()->IsChildOf<AInfo>(); });
			
		TArray<FWorldPartitionReference> LoadedActors;
		LoadedActors.Reserve(AlwaysLoadedActors.Num());
		Algo::Transform(AlwaysLoadedActors, LoadedActors, [](const UWorldPartitionEditorCell::FActorHandle& ActorHandle) { return ActorHandle.Handle; });

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
			
			// When considered as a small world, load all actors
			if (bItsASmallWorld)
			{
				EditorHash->ForEachCell([this](UWorldPartitionEditorCell* Cell)
				{
					UpdateLoadingEditorCell(Cell, true);
				});
			}

			// Load last loaded cells
			if (WorldPartitionEditorModule.GetEnableLoadingOfLastLoadedCells())
			{
				const TArray<FName>& EditorGridLastLoadedCells = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetEditorGridLoadedCells(InWorld);

				for (FName EditorGridLastLoadedCell : EditorGridLastLoadedCells)
				{
					if (UWorldPartitionEditorCell* Cell = FindObject<UWorldPartitionEditorCell>(EditorHash, *EditorGridLastLoadedCell.ToString()))
					{
						UpdateLoadingEditorCell(Cell, true);
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
	if (IsRunningGame())
	{
		OnBeginPlay(EWorldPartitionStreamingMode::EditorStandalone);
	}
#endif
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
		for (auto& Pair : ActorDescContainers)
		{
			if (UActorDescContainer* Container = Pair.Value.Get())
			{
				Container->Uninitialize();
			}
		}
		ActorDescContainers.Empty();

		if (World->IsGameWorld())
		{
			OnEndPlay();
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

	Super::Uninitialize();
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
UWorldPartition* UWorldPartition::CreateWorldPartition(AWorldSettings* WorldSettings, TSubclassOf<UWorldPartitionEditorHash> EditorHashClass, TSubclassOf<UWorldPartitionRuntimeHash> RuntimeHashClass)
{
	if (!EditorHashClass)
	{
		EditorHashClass = FindObject<UClass>(ANY_PACKAGE, TEXT("WorldPartitionEditorSpatialHash"));
	}

	if (!RuntimeHashClass)
	{
		RuntimeHashClass = FindObject<UClass>(ANY_PACKAGE, TEXT("WorldPartitionRuntimeSpatialHash"));
	}

	UWorldPartition* WorldPartition = NewObject<UWorldPartition>(WorldSettings);
	WorldSettings->SetWorldPartition(WorldPartition);
	WorldSettings->MarkPackageDirty();

	WorldPartition->EditorHash = NewObject<UWorldPartitionEditorHash>(WorldPartition, EditorHashClass);
	WorldPartition->RuntimeHash = NewObject<UWorldPartitionRuntimeHash>(WorldPartition, RuntimeHashClass);

	WorldPartition->EditorHash->SetDefaultValues();
	WorldPartition->RuntimeHash->SetDefaultValues();
	WorldPartition->DefaultHLODLayer = nullptr;

	WorldPartition->GetWorld()->PersistentLevel->bIsPartitioned = true;

	return WorldPartition;
}

void UWorldPartition::RegisterDelegates()
{
	Super::RegisterDelegates();
	if (GEditor && !IsTemplate())
	{
		FEditorDelegates::PreBeginPIE.AddUObject(this, &UWorldPartition::OnPreBeginPIE);
		FGameDelegates::Get().GetEndPlayMapDelegate().AddUObject(this, &UWorldPartition::OnEndPlay);
	}
}

void UWorldPartition::UnregisterDelegates()
{
	Super::UnregisterDelegates();
	if (GEditor && !IsTemplate())
	{
		FEditorDelegates::PreBeginPIE.RemoveAll(this);
		FGameDelegates::Get().GetEndPlayMapDelegate().RemoveAll(this);
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

bool UWorldPartition::IsSimulating()
{
#if WITH_EDITOR
	return GEditor && GEditor->bIsSimulatingInEditor && GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->IsSimulateInEditorViewport();
#else
	return false;
#endif
}

#if WITH_EDITOR
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
		for (const UWorldPartitionEditorCell::FActorReference& ActorDesc : Cell->LoadedActors)
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
		for (UWorldPartitionEditorCell::FActorHandle& ActorHandle: Cell->Actors)
		{
			AActor* Actor = ActorHandle->GetActor();

			// The actor could be either loaded but with no cells loaded (in the case of a reference from another actor, for example)
			// or directly referenced by a loaded cell.
			check(Actor || !ActorHandle->GetHardRefCount());

			// Filter actor against DataLayers
			if (ShouldActorBeLoaded(*ActorHandle))
			{
				Cell->LoadedActors.Add(UWorldPartitionEditorCell::FActorReference(ActorHandle.Source, ActorHandle.Handle));
			}
			else
			{
				// Don't call LoadedActors.Remove(ActorHandle) right away, as this will create a temporary reference and might try to load.
				for (const UWorldPartitionEditorCell::FActorReference& ActorReference: Cell->LoadedActors)
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

void UWorldPartition::ApplyActorTransform(AActor* Actor, const FTransform& InTransform)
{
	if (!InTransform.Equals(FTransform::Identity))
	{
		FLevelUtils::FApplyLevelTransformParams TransformParams(Actor->GetLevel(), InTransform);
		TransformParams.Actor = Actor;
		TransformParams.bDoPostEditMove = true;
		FLevelUtils::ApplyLevelTransform(TransformParams);
	}
}

void UWorldPartition::OnActorDescRegistered(const FWorldPartitionActorDesc& ActorDesc) 
{
	AActor* Actor = ActorDesc.GetActor();
	check(Actor);
	ApplyActorTransform(Actor, InstanceTransform);
	Actor->GetLevel()->AddLoadedActor(Actor);
}

void UWorldPartition::OnActorDescUnregistered(const FWorldPartitionActorDesc& ActorDesc) 
{
	AActor* Actor = ActorDesc.GetActor();
	if (!Actor->IsPendingKill())
	{
		Actor->GetLevel()->RemoveLoadedActor(Actor);
		ApplyActorTransform(Actor, InstanceTransform.Inverse());
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

bool UWorldPartition::CanDrawRuntimeHash() const
{
	return GetWorld()->IsGameWorld() || UWorldPartition::IsSimulating();
}

FVector2D UWorldPartition::GetDrawRuntimeHash2DDesiredFootprint(const FVector2D& CanvasSize)
{
	check(CanDrawRuntimeHash());
	return GetStreamingPolicy()->GetDrawRuntimeHash2DDesiredFootprint(CanvasSize);
}

void UWorldPartition::DrawRuntimeHash2D(class UCanvas* Canvas, const FVector2D& PartitionCanvasOffset, const FVector2D& PartitionCanvasSize)
{
	check(CanDrawRuntimeHash());
	GetStreamingPolicy()->DrawRuntimeHash2D(Canvas, PartitionCanvasOffset, PartitionCanvasSize);
}

void UWorldPartition::DrawRuntimeHash3D()
{
	check(CanDrawRuntimeHash());
	GetStreamingPolicy()->DrawRuntimeHash3D();
}

#if WITH_EDITOR
void UWorldPartition::DrawRuntimeHashPreview()
{
	RuntimeHash->DrawPreview();
}

bool UWorldPartition::GenerateStreaming(EWorldPartitionStreamingMode Mode, TArray<FString>* OutPackagesToGenerate)
{
	check(RuntimeHash);
	return RuntimeHash->GenerateRuntimeStreaming(Mode, GetStreamingPolicy(), OutPackagesToGenerate);
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

void UWorldPartition::GenerateHLOD(ISourceControlHelper* SourceControlHelper, bool bCreateActorsOnly)
{
	RuntimeHash->GenerateHLOD(SourceControlHelper, bCreateActorsOnly);
}

void UWorldPartition::GenerateNavigationData()
{
	RuntimeHash->GenerateNavigationData();
}

const UActorDescContainer* UWorldPartition::RegisterActorDescContainer(FName PackageName)
{
	TWeakObjectPtr<UActorDescContainer>* ExistingContainerPtr = ActorDescContainers.Find(PackageName);
	if (ExistingContainerPtr)
	{
		if (UActorDescContainer* LevelContainer = ExistingContainerPtr->Get())
		{
			return LevelContainer;
		}
	}
		
	UActorDescContainer* NewContainer = NewObject<UActorDescContainer>(GetTransientPackage());
	NewContainer->Initialize(GetWorld(), PackageName, true);
	ActorDescContainers.Add(PackageName, TWeakObjectPtr<UActorDescContainer>(NewContainer));

	return NewContainer;
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

void UWorldPartition::RemapSoftObjectPath(FSoftObjectPath& ObjectPath)
{
	if (GetWorld()->IsGameWorld())
	{
		GetStreamingPolicy()->RemapSoftObjectPath(ObjectPath);
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