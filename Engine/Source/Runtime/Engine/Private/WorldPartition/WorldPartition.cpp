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
#include "Layers/Layer.h"
#include "Layers/LayersSubsystem.h"
#include "Editor/GroupActor.h"
#include "EditorLevelUtils.h"
#include "FileHelpers.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "UnrealEdMisc.h"
#include "AssetRegistryModule.h"
#include "WorldPartition/WorldPartitionActorDescFactory.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartition/WorldPartitionEditorSpatialHash.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#endif

DEFINE_LOG_CATEGORY(LogWorldPartition);

PRAGMA_DISABLE_OPTIMIZATION

#define LOCTEXT_NAMESPACE "WorldPartitionEditor"

#if WITH_EDITOR
static void GenerateHLOD(const TArray<FString>& Args)
{
	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		if (!World->IsPlayInEditor())
		{
			if (UWorldPartition* WorldPartition = World->GetWorldPartition())
			{
				WorldPartition->Modify();
				WorldPartition->GenerateHLOD();
			}
		}
	}
}

static FAutoConsoleCommand GenerateHLODCmd(
	TEXT("wp.Editor.GenerateHLOD"),
	TEXT("Generates HLOD data for runtime."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&GenerateHLOD)
);
#endif

#if WITH_EDITOR
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
			GEngine->BroadcastLevelActorListChanged();
			GEditor->NoteSelectionChange();

			if (WorldPartition->WorldPartitionEditor)
			{
				WorldPartition->WorldPartitionEditor->Refresh();
			}
		}
	}

	static int32 UpdatesInProgress;
	UWorldPartition* WorldPartition;
};

int32 FWorldPartionCellUpdateContext::UpdatesInProgress = 0;
#endif

#if WITH_EDITOR
UWorldPartition::FEnableWorldPartitionEvent UWorldPartition::EnableWorldPartitionEvent;
UWorldPartition::FWorldPartitionChangedEvent UWorldPartition::WorldPartitionChangedEvent;
#endif

UWorldPartition::UWorldPartition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, World(nullptr)
#if WITH_EDITOR
	, EditorHash(nullptr)
	, WorldPartitionEditor(nullptr)
	, bDirty(false)
	, bForceGarbageCollection(false)
	, bForceGarbageCollectionPurge(false)
#endif
	, InitState(EWorldPartitionInitState::Uninitialized)
	, InstanceTransform(FTransform::Identity)
	, StreamingPolicy(nullptr)
#if WITH_EDITOR
	, bIsPreCooked(false)
#endif
{
	static bool bRegisteredDelegate = false;
	if (!bRegisteredDelegate)
	{
		FWorldDelegates::LevelRemovedFromWorld.AddStatic(&UWorldPartition::WorldPartitionOnLevelRemovedFromWorld);
		bRegisteredDelegate = true;
	}
}

#if WITH_EDITOR
void UWorldPartition::OnPostGarbageCollect()
{
	// Clean-up ModifiedActors (deleted/garbage collected actors)
	ModifiedActors.Remove(nullptr);
}

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

void UWorldPartition::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (AActor* Actor = Cast<AActor>(Object))
	{
		if (FWorldPartitionActorDesc* ActorDesc = GetActorDesc(Actor->GetActorGuid()))
		{
			UpdateActorDesc(Actor);
		}
	}
}

void UWorldPartition::OnObjectModified(UObject* Object)
{
	if (Object->GetPackage()->IsDirty())
	{
		// Don't rely on undo stack and keep references on modified actor to avoid loosing changes when unloading editor cells
		if (AActor* Actor = Cast<AActor>(Object))
		{
			if (FWorldPartitionActorDesc* ActorDesc = GetActorDesc(Actor->GetActorGuid()))
			{
				UPackage* Package = Actor->GetOutermost();
				if (Package && Package->IsDirty())
				{
					ModifiedActors.Add(Actor);
				}
			}
		}
	}
}

void UWorldPartition::OnObjectSaved(UObject* Object)
{
	if (FUnrealEdMisc::Get().GetAutosaveState() == FUnrealEdMisc::EAutosaveState::Inactive)
	{
		// Once modified actor is saved, we can remove reference on it
		if (AActor* Actor = Cast<AActor>(Object))
		{
			ModifiedActors.Remove(Actor);
		}
	}
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

		LayerSubSystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();

		if (IsMainWorldPartition())
		{
			RegisterDelegates();
		}
	}

	if (bEditorOnly || !IsMainWorldPartition())
	{
		TSet<FName> AllLayersNames;

		auto GetLevelActors = [](const FName& LevelPath, TArray<FAssetData>& OutAssets)
		{
			if (!LevelPath.IsNone())
			{
				FString LevelPathStr = LevelPath.ToString();
				IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	
				// Do a synchronous scan of the level external actors path.
				const bool bForceRescan = true;
				AssetRegistry.ScanPathsSynchronous({ULevel::GetExternalActorsPath(LevelPath.ToString())}, bForceRescan);

				static const FName NAME_LevelPackage(TEXT("LevelPackage"));
				FARFilter Filter;
				Filter.TagsAndValues.Add(NAME_LevelPackage, LevelPathStr);
				Filter.bIncludeOnlyOnDiskAssets = true;
				AssetRegistry.GetAssets(Filter, OutAssets);
			}
		};

		TArray<FAssetData> Assets;
		UPackage* LevelPackage = OuterWorld->PersistentLevel->GetOutermost();
		GetLevelActors(LevelPackage->FileName, Assets);

		bool bIsInstanced = (bEditorOnly && !IsRunningCommandlet()) ? OuterWorld->PersistentLevel->IsInstancedLevel() : false;

		FString ReplaceFrom;
		FString ReplaceTo;

		if (bIsInstanced)
		{
			InstancingContext.AddMapping(LevelPackage->FileName, LevelPackage->GetFName());

			const FString SourceWorldName = FPaths::GetBaseFilename(LevelPackage->FileName.ToString());
			const FString DestWorldName = FPaths::GetBaseFilename(LevelPackage->GetFName().ToString());

			ReplaceFrom = SourceWorldName + TEXT(".") + SourceWorldName;
			ReplaceTo = DestWorldName + TEXT(".") + DestWorldName;
		}

		for (const FAssetData& Asset : Assets)
		{
			FActorMetaDataReader ActorMetaDataSerializer(Asset);

			FName ActorClass;
			ActorMetaDataSerializer.Serialize(TEXT("ActorClass"), ActorClass);

			FWorldPartitionActorDescInitData ActorDescInitData;
			ActorDescInitData.NativeClass = FindObjectChecked<UClass>(ANY_PACKAGE, *ActorClass.ToString(), true);
			ActorDescInitData.PackageName = Asset.PackageName;
			ActorDescInitData.AssetData = Asset;
			ActorDescInitData.ActorPath = Asset.ObjectPath;
			ActorDescInitData.Transform = bIsInstanced ? InstanceTransform : FTransform::Identity;
			ActorDescInitData.Serializer = &ActorMetaDataSerializer;

			if (bIsInstanced)
			{
				const FString LongActorPackageName = ActorDescInitData.PackageName.ToString();
				const FString ActorPackageName = FPaths::GetBaseFilename(LongActorPackageName);
				const FString InstancedName = FString::Printf(TEXT("%s_InstanceOf_%s"), *LevelPackage->GetName(), *ActorPackageName);

				InstancingContext.AddMapping(*LongActorPackageName, *InstancedName);

				ActorDescInitData.ActorPath = *ActorDescInitData.ActorPath.ToString().Replace(*ReplaceFrom, *ReplaceTo);
			}

			TUniquePtr<FWorldPartitionActorDesc> NewActorDesc(GetActorDescFactory(ActorDescInitData.NativeClass)->Create());

			if (NewActorDesc->Init(ActorDescInitData))
			{
				if (bEditorOnly)
				{
					AllLayersNames.Append(NewActorDesc->GetLayers());
				}

				Actors.Add(NewActorDesc->GetGuid(), MoveTemp(NewActorDesc));
			}
		}

		if (bEditorOnly)
		{
			for (const auto& Pair : Actors)
			{
				AddToPartition(Pair.Value.Get());
			}

			CreateLayers(AllLayersNames);

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
#endif

	InitState = EWorldPartitionInitState::Initialized;

	UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
	if (ensure(WorldPartitionSubsystem))
	{
		WorldPartitionSubsystem->RegisterWorldPartition(this);
	}

#if WITH_EDITOR
	if (World->IsPlayInEditor())
	{
		PrepareForPIE();
	}
#endif
}

void UWorldPartition::BeginDestroy()
{
	Uninitialize();

	Super::BeginDestroy();
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
			UnregisterDelegates();
			ActorDescFactories.Empty();
		}

		EditorHash = nullptr;

		for (FActorCluster* ActorCluster : ActorClustersSet) { delete ActorCluster; }
		ActorClustersSet.Empty();
		ActorToActorCluster.Empty();
#endif		

		UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
		if (ensure(WorldPartitionSubsystem))
		{
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
		GEditor->OnActorMoving().AddUObject(this, &UWorldPartition::OnActorMoving);
		GEditor->OnActorMoved().AddUObject(this, &UWorldPartition::OnActorMoving);
		GEngine->OnLevelActorOuterChanged().AddUObject(this, &UWorldPartition::OnActorOuterChanged);
		GEditor->OnLevelActorAdded().AddUObject(this, &UWorldPartition::OnActorAdded);
		GEditor->OnLevelActorDeleted().AddUObject(this, &UWorldPartition::OnActorDeleted);

		FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UWorldPartition::OnObjectPropertyChanged);
		FCoreUObjectDelegates::OnObjectModified.AddUObject(this, &UWorldPartition::OnObjectModified);
		FCoreUObjectDelegates::OnObjectSaved.AddUObject(this, &UWorldPartition::OnObjectSaved);
		FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &UWorldPartition::OnPostGarbageCollect);

		FEditorDelegates::PreBeginPIE.AddUObject(this, &UWorldPartition::OnPreBeginPIE);
		FEditorDelegates::EndPIE.AddUObject(this, &UWorldPartition::OnEndPIE);

		if (LayerSubSystem)
		{
			OnLayersChangedHandle = LayerSubSystem->OnLayersChanged().AddLambda([this](const ELayersAction::Type Action, const TWeakObjectPtr<ULayer>& ChangedLayer, const FName& ChangedProperty)
			{
				static FName NAME_bShouldLoadActors(TEXT("bShouldLoadActors"));
				if (ChangedProperty == NAME_bShouldLoadActors)
				{
					RefreshLoadedCells();
				}
			});

			OnActorsLayersChangedHandle = LayerSubSystem->OnActorsLayersChanged().AddLambda([this](const TWeakObjectPtr<AActor>& ChangedActor)
			{
				UpdateActorDesc(ChangedActor.Get());
			});
		}
	}
}

void UWorldPartition::UnregisterDelegates()
{
	if (GEditor && !IsTemplate())
	{
		if (LayerSubSystem)
		{
			LayerSubSystem->OnLayersChanged().Remove(OnLayersChangedHandle);
			LayerSubSystem->OnActorsLayersChanged().Remove(OnActorsLayersChangedHandle);
		}

		FEditorDelegates::PreBeginPIE.RemoveAll(this);
		FEditorDelegates::EndPIE.RemoveAll(this);

		GEditor->OnActorMoving().RemoveAll(this);
		GEditor->OnActorMoved().RemoveAll(this);
		GEngine->OnLevelActorOuterChanged().RemoveAll(this);
		GEditor->OnLevelActorAdded().RemoveAll(this);
		GEditor->OnLevelActorDeleted().RemoveAll(this);

		FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
		FCoreUObjectDelegates::OnObjectModified.RemoveAll(this);
		FCoreUObjectDelegates::OnObjectSaved.RemoveAll(this);
		FCoreUObjectDelegates::GetPostGarbageCollect().RemoveAll(this);
	}
}

void UWorldPartition::UpdateActorDesc(AActor* InActor)
{
	check(InActor)

	if (!InActor->IsChildActor())
	{
		TUniquePtr<FWorldPartitionActorDesc>* ActorDescPtr = Actors.Find(InActor->GetActorGuid());

		if (GIsTransacting && InActor->IsPendingKill())
		{
			OnActorDeleted(InActor);
		}
		else if (GIsTransacting && !ActorDescPtr)
		{
			OnActorAdded(InActor);
		}
		else if (ActorDescPtr && InActor->GetLevel() == World->PersistentLevel)
		{
			TUniquePtr<FWorldPartitionActorDesc> NewDesc(GetActorDescFactory(InActor)->Create());
			NewDesc->Init(InActor);
			
			if (NewDesc->GetHash() != ActorDescPtr->Get()->GetHash())
			{
				RemoveFromPartition(ActorDescPtr->Get(), false);
				*ActorDescPtr = MoveTemp(NewDesc);
				AddToPartition(ActorDescPtr->Get());
			}
		}
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

TUniquePtr<FWorldPartitionActorDescFactory> UWorldPartition::DefaultActorDescFactory;
TMap<FName, FWorldPartitionActorDescFactory*> UWorldPartition::ActorDescFactories;

void UWorldPartition::RegisterActorDescFactory(TSubclassOf<AActor> Class, FWorldPartitionActorDescFactory* Factory)
{
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		if (ClassIt->IsChildOf(Class))
		{
			const FName ClassName = ClassIt->GetFName();
			if (!ActorDescFactories.Contains(ClassName))
			{
				ActorDescFactories.Add(ClassName, Factory);
			}
		}
	}
}

FWorldPartitionActorDescFactory* UWorldPartition::GetActorDescFactory(TSubclassOf<AActor> Class)
{
	Class = GetParentNativeClass(Class);

	FName ClassName = Class->GetFName();
	if (FWorldPartitionActorDescFactory** Factory = ActorDescFactories.Find(ClassName))
	{
		return *Factory;
	}

	if (!DefaultActorDescFactory)
	{
		DefaultActorDescFactory.Reset(new FWorldPartitionActorDescFactory());
	}
	return DefaultActorDescFactory.Get();
}

FWorldPartitionActorDescFactory* UWorldPartition::GetActorDescFactory(const AActor* Actor)
{
	return GetActorDescFactory(Actor->GetClass());
}

TArray<const FWorldPartitionActorDesc*> UWorldPartition::GetIntersectingActorDescs(const FBox& Box, TSubclassOf<AActor> ActorClass) const
{
	TArray<const FWorldPartitionActorDesc*> ActorDescs;

	EditorHash->ForEachIntersectingActor(Box, [&ActorDescs, &ActorClass](const FWorldPartitionActorDesc* ActorDesc)
	{
		if (ActorDesc->GetActorClass()->IsChildOf(ActorClass))
		{
			ActorDescs.Add(ActorDesc);
		}
	});
	
	return MoveTemp(ActorDescs);
}

const FWorldPartitionActorDesc* UWorldPartition::GetActorDesc(const FGuid& Guid) const
{
	const TUniquePtr<FWorldPartitionActorDesc>* ActorDescObj = Actors.Find(Guid);
	return ActorDescObj != nullptr ? ActorDescObj->Get() : nullptr;
}

FWorldPartitionActorDesc* UWorldPartition::GetActorDesc(const FGuid& Guid)
{
	TUniquePtr<FWorldPartitionActorDesc>* ActorDescObj = Actors.Find(Guid);
	return ActorDescObj != nullptr ? ActorDescObj->Get() : nullptr;
}

void UWorldPartition::AddToClusters(const FWorldPartitionActorDesc* ActorDesc)
{
	FActorCluster* ActorCluster = ActorToActorCluster.FindRef(ActorDesc->GetGuid());
	if (!ActorCluster)
	{
		ActorCluster = new FActorCluster(ActorDesc);
		ActorClustersSet.Add(ActorCluster);
		ActorToActorCluster.Add(ActorDesc->GetGuid(), ActorCluster);
	}

	// Don't include references from editor-only actors
	if (!ActorDesc->GetActorIsEditorOnly())
	{
		for (const FGuid& ReferenceGuid : ActorDesc->GetReferences())
		{
			const FWorldPartitionActorDesc* ReferenceActorDesc = GetActorDesc(ReferenceGuid);

			// Don't include references to editor-only actors
			if (ReferenceActorDesc && !ReferenceActorDesc->GetActorIsEditorOnly())
			{
				FActorCluster* ReferenceCluster = ActorToActorCluster.FindRef(ReferenceGuid);
				if (ReferenceCluster)
				{
					if (ReferenceCluster != ActorCluster)
					{
						// Merge reference cluster in Actor's cluster
						ActorCluster->Add(*ReferenceCluster);
						for (const FGuid& ReferenceClusterActorGuid : ReferenceCluster->Actors)
						{
							ActorToActorCluster[ReferenceClusterActorGuid] = ActorCluster;
						}
						ActorClustersSet.Remove(ReferenceCluster);
						delete ReferenceCluster;
					}
				}
				else
				{
					// Put Reference in Actor's cluster
					ActorCluster->Add(FActorCluster(ReferenceActorDesc));
				}

				// Map its cluster
				ActorToActorCluster.Add(ReferenceGuid, ActorCluster);
			}
		}
	}
}

void UWorldPartition::RemoveFromClusters(const FWorldPartitionActorDesc* ActorDesc)
{
	FActorCluster* ActorCluster = ActorToActorCluster.FindAndRemoveChecked(ActorDesc->GetGuid());
	ActorCluster->Actors.Remove(ActorDesc->GetGuid());

	// Break up this cluster and reinsert all actors
	ActorClustersSet.Remove(ActorCluster);

	for (const FGuid& Guid : ActorCluster->Actors)
	{
		ActorToActorCluster[Guid] = nullptr;
	}

	for (const FGuid& Guid : ActorCluster->Actors)
	{
		if (FWorldPartitionActorDesc* ClusterActorDesc = GetActorDesc(Guid))
		{
			AddToClusters(ClusterActorDesc);
		}
	}

	delete ActorCluster;
}

UWorldPartition::FActorCluster::FActorCluster(const FWorldPartitionActorDesc* ActorDesc)
	: GridPlacement(ActorDesc->GetGridPlacement())
	, RuntimeGrid(ActorDesc->GetRuntimeGrid())
	, Bounds(ActorDesc->GetBounds())
{
	check(GridPlacement != EActorGridPlacement::None);
	Actors.Add(ActorDesc->GetGuid());
}

void UWorldPartition::FActorCluster::Add(const UWorldPartition::FActorCluster& ActorCluster)
{
	// Merge Actors
	Actors.Append(ActorCluster.Actors);

	// Merge RuntimeGrid
	RuntimeGrid = RuntimeGrid == ActorCluster.RuntimeGrid ? RuntimeGrid : NAME_None;

	// Merge Bounds
	Bounds += ActorCluster.Bounds;

	// Merge GridPlacement
	// If currently None, will always stay None
	if (GridPlacement != EActorGridPlacement::None)
	{
		// If grid placement differs between the two clusters
		if (GridPlacement != ActorCluster.GridPlacement)
		{
			// If one of the two cluster was always loaded, set to None
			if (ActorCluster.GridPlacement == EActorGridPlacement::AlwaysLoaded || GridPlacement == EActorGridPlacement::AlwaysLoaded)
			{
				GridPlacement = EActorGridPlacement::None;
			}
			else
			{
				GridPlacement = ActorCluster.GridPlacement;
			}
		}

		// If current placement is set to Location, that won't make sense when merging two clusters. Set to Bounds
		if (GridPlacement == EActorGridPlacement::Location)
		{
			GridPlacement = EActorGridPlacement::Bounds;
		}
	}
}

const TSet<UWorldPartition::FActorCluster*>& UWorldPartition::GetActorClusters() const
{
	return ActorClustersSet;
}

const UWorldPartition::FActorCluster* UWorldPartition::GetClusterForActor(const FGuid& InActorGuid) const
{
	return ActorToActorCluster.FindRef(InActorGuid);
}

void UWorldPartition::RefreshLoadedCells()
{
	FWorldPartionCellUpdateContext CellUpdateContext(this);

	UpdateLoadingEditorCell(EditorHash->GetAlwaysLoadedCell(), true);

	EditorHash->ForEachCell([&](UWorldPartitionEditorCell* Cell)
	{
		if (Cell->bLoaded)
		{
			UpdateLoadingEditorCell(Cell, true);
		}
	});
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
		LoadEditorCells(CellsToLoad);
	}
}

void UWorldPartition::UnloadEditorCells(const FBox& Box)
{
	TArray<UWorldPartitionEditorCell*> CellsToUnload;
	if (EditorHash->GetIntersectingCells(Box, CellsToUnload))
	{
		UnloadEditorCells(CellsToUnload);
	}
}

void UWorldPartition::LoadEditorCells(const TArray<UWorldPartitionEditorCell*>& CellsToLoad)
{
	FWorldPartionCellUpdateContext CellUpdateContext(this);

	FScopedTransaction Transaction(LOCTEXT("UWorldPartition_LoadCells", "Load Cells"));

	int32 NumActorsToLoad = Algo::TransformAccumulate(CellsToLoad, [](UWorldPartitionEditorCell* Cell) { return Cell->Actors.Num() - Cell->LoadedActors.Num();}, 0);

	FScopedSlowTask SlowTask(NumActorsToLoad, LOCTEXT("LoadingCells", "Loading cells..."));
	SlowTask.MakeDialog();

	for (UWorldPartitionEditorCell* Cell : CellsToLoad)
	{
		SlowTask.EnterProgressFrame(Cell->Actors.Num() - Cell->LoadedActors.Num());
		UpdateLoadingEditorCell(Cell, true);
	}
}

void UWorldPartition::UnloadEditorCells(const TArray<UWorldPartitionEditorCell*>& CellsToUnload)
{
	FWorldPartionCellUpdateContext CellUpdateContext(this);

	FScopedTransaction Transaction(LOCTEXT("UWorldPartition_UnloadCells", "Unload Cells"));

	int32 NumActorsToUnload = Algo::TransformAccumulate(CellsToUnload, [](UWorldPartitionEditorCell* Cell) { return Cell->LoadedActors.Num(); }, 0);

	FScopedSlowTask SlowTask(NumActorsToUnload, LOCTEXT("UnloadingCells", "Unloading cells..."));
	SlowTask.MakeDialog();

	for (UWorldPartitionEditorCell* Cell: CellsToUnload)
	{
		SlowTask.EnterProgressFrame(Cell->LoadedActors.Num());
		UpdateLoadingEditorCell(Cell, false);
	}
}

// Loads actors in Editor cell
void UWorldPartition::UpdateLoadingEditorCell(UWorldPartitionEditorCell* Cell, bool bShouldBeLoaded)
{
	FWorldPartionCellUpdateContext CellUpdateContext(this);

	UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartition::UpdateLoadingEditorCell 0x%08llx [%s]"), Cell, bShouldBeLoaded ? TEXT("Load") : TEXT("Unload"));

	Cell->Modify(false);

	auto ShouldActorBeLoaded = [&](const FWorldPartitionActorDesc* ActorDesc)
	{
		if (LayerSubSystem)
		{
			uint32 NumValidLayers = 0;
			for (const FName& LayerName : ActorDesc->GetLayers())
			{
				if (ULayer* Layer = LayerSubSystem->GetLayer(LayerName))
				{
					if (Layer->ShouldLoadActors())
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

	auto UnloadActor = [this](FWorldPartitionActorDesc* ActorDesc, AActor* Actor)
	{
		check(Actor);

		const uint32 ActorRefCount = ActorDesc->RemoveLoadedRefCount();
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
			AActor* Actor = ActorDesc->GetActor();
			check(Actor);

			UnloadActor(ActorDesc, Actor);
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
			check(Actor || !ActorDesc->GetLoadedRefCount());

			// Filter actor against visible layers and load it if required
			if (ShouldActorBeLoaded(ActorDesc))
			{
				bool bIsAlreadyInLoadedActors = false;
				Cell->LoadedActors.Add(ActorDesc, &bIsAlreadyInLoadedActors);

				if (bIsAlreadyInLoadedActors)
				{
					// We already hold a reference to this actor
					check(Actor);
					check(ActorDesc->GetLoadedRefCount());
					UE_LOG(LogWorldPartition, Verbose, TEXT(" ==> Skipped already loaded actor %s"), *Actor->GetFullName());
				}
				else
				{
					const uint32 ActorRefCount = ActorDesc->AddLoadedRefCount();

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

void UWorldPartition::CreateLayers(const TSet<FName>& LayerNames)
{
	// Make sure all the layers we discovered are created
	if (LayerSubSystem)
	{
		for (FName LayerName : LayerNames)
		{
			if (!LayerSubSystem->IsLayer(LayerName))
			{
				LayerSubSystem->CreateLayer(LayerName);
			}
		}
	}
}
#endif

bool UWorldPartition::IsValidPartitionActor(AActor* Actor) const
{
#if WITH_EDITOR
	if (Actor->IsPackageExternal() || Actor->GetPackage()->HasAnyPackageFlags(PKG_PlayInEditor))
	{
		return true;
	}
	const TUniquePtr<FWorldPartitionActorDesc>* ActorDesc = Actors.Find(Actor->GetActorGuid());
	return ActorDesc->IsValid();
#else
	return Actor->IsPackageExternal();
#endif
}

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
	check(IsValidPartitionActor(Actor));
	check(ActorDesc->GetActor() == Actor);

	ApplyActorTransform(Actor, InstanceTransform);

	// Since Actor might have been kept loaded but unregistered (modified), we need to make sure its visibility reflects its layer visibility
	if (LayerSubSystem)
	{
		bool bActorModified = false;
		bool bActorSelectionChanged = false;
		const bool bActorNotifySelectionChange = false;
		const bool bActorRedrawViewports = false;
		LayerSubSystem->UpdateActorVisibility(Actor, bActorSelectionChanged, bActorModified, bActorNotifySelectionChange, bActorRedrawViewports);
	}

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
	check(IsValidPartitionActor(Actor));
	FWorldPartitionActorDesc* ActorDesc = GetActorDesc(Actor->GetActorGuid());
	check(ActorDesc);

	if (LayerSubSystem)
	{
		LayerSubSystem->DisassociateActorFromLayers(Actor);
	}

	OnActorRegisteredEvent.Broadcast(*Actor, false);

	Actor->GetLevel()->RemoveLoadedActor(Actor);

	ActorDesc->Unload();

	ApplyActorTransform(Actor, InstanceTransform.Inverse());

	bForceGarbageCollection = true;
	bForceGarbageCollectionPurge = true;

	UE_LOG(LogWorldPartition, Verbose, TEXT(" ==> Unregistered loaded actor %s"), *Actor->GetFullName());
#endif
}

#if WITH_EDITOR
void UWorldPartition::OnActorAdded(AActor* InActor)
{
	check(!InActor->IsPendingKill());

	if (InActor->GetLevel() == World->PersistentLevel && InActor->IsPackageExternal() && !InActor->IsChildActor())
	{
		TUniquePtr<FWorldPartitionActorDesc>* ActorDescPtr = Actors.Find(InActor->GetActorGuid());

		TUniquePtr<FWorldPartitionActorDesc> NewDesc(GetActorDescFactory(InActor)->Create());
		NewDesc->Init(InActor);

		if (ActorDescPtr)
		{
			// The only case where this is valid is when reinstancing BP actors. In this case, we'll receive the call
			// for the newly spawned actor, while the old one hasn't been removed from the world yet. So we remove the
			// old one before adding the new one.
			check(GIsReinstancing);

			RemoveFromPartition(ActorDescPtr->Get(), false);

			*ActorDescPtr = MoveTemp(NewDesc);
		}
		else
		{
			checkSlow(InActor->GetLevel()->Actors.Find(InActor) != INDEX_NONE);

			ActorDescPtr = &Actors.Add(InActor->GetActorGuid(), MoveTemp(NewDesc));
		}

		AddToPartition(ActorDescPtr->Get());
	}
}

void UWorldPartition::OnActorDeleted(AActor* InActor)
{
	if (InActor->GetLevel() == World->PersistentLevel)
	{
		if (FWorldPartitionActorDesc* ActorDesc = GetActorDesc(InActor->GetActorGuid()))
		{
			if (InActor->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists))
			{
				// We are receiving this event when destroying the old actor after BP reinstantiation. In this case,
				// the newly created actor was already added to the list, so we can safely ignore this case.
				check(GIsReinstancing);
			}
			else
			{
				// During undo transactions, newly created objects will get removed with their package unset due to the
				// order of SetPackage/Modify. Take this into account in the check below.
				check(InActor->IsPackageExternal() || GIsTransacting);

				// Validate that this actor is already removed from the level
				verify(InActor->GetLevel()->Actors.Find(InActor) == INDEX_NONE);

				RemoveFromPartition(ActorDesc);
			}
		}
	}
}

void UWorldPartition::OnActorMoving(AActor* InActor)
{
	if (InActor->GetLevel() == World->PersistentLevel)
	{
		UpdateActorDesc(InActor);
	}
}

void UWorldPartition::OnActorOuterChanged(AActor* InActor, UObject* InOldOuter)
{
	ULevel* OldLevel = Cast<ULevel>(InOldOuter);

	if (OldLevel == World->PersistentLevel)
	{
		if (FWorldPartitionActorDesc* ActorDesc = GetActorDesc(InActor->GetActorGuid()))
		{
			RemoveFromPartition(ActorDesc);
		}
	}
}

void UWorldPartition::HashActorDesc(FWorldPartitionActorDesc* ActorDesc)
{
	check(ActorDesc);
	check(!ActorDesc->GetLoadedRefCount());
	check(EditorHash);
	EditorHash->HashActor(ActorDesc);
}

void UWorldPartition::UnhashActorDesc(FWorldPartitionActorDesc* ActorDesc)
{
	check(ActorDesc);
	check(EditorHash);
	EditorHash->UnhashActor(ActorDesc);

	check(!ActorDesc->GetLoadedRefCount());
}

void UWorldPartition::AddToPartition(FWorldPartitionActorDesc* ActorDesc)
{
	AddToClusters(ActorDesc);

	HashActorDesc(ActorDesc);	
}

void UWorldPartition::RemoveFromPartition(FWorldPartitionActorDesc* ActorDesc, bool bRemoveDescriptorFromArray)
{
	// Unhash this actor from the editor hash
	UnhashActorDesc(ActorDesc);

	RemoveFromClusters(ActorDesc);

	// Remove this actor descriptor
	if (bRemoveDescriptorFromArray)
	{
		Actors.Remove(ActorDesc->GetGuid());
	}
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

	if (!IsPreCooked())
	{
		if (!IsMainWorldPartition())
		{
			GenerateStreaming(EWorldPartitionStreamingMode::PIE);
		}

		GetStreamingPolicy()->PrepareForPIE();
	}
}

void UWorldPartition::CleanupForPIE()
{
	check(World->IsPlayInEditor());

	if (!IsPreCooked())
	{
		FlushStreaming();
	}
}

bool UWorldPartition::GenerateStreaming(EWorldPartitionStreamingMode Mode)
{
	if (!IsPreCooked())
	{
		return RuntimeHash->GenerateStreaming(Mode, GetStreamingPolicy());
	}
	return false;
}

void UWorldPartition::FlushStreaming()
{
	if (!IsPreCooked())
	{
		RuntimeHash->FlushStreaming();
	}
}

void UWorldPartition::GenerateHLOD()
{
	RuntimeHash->GenerateHLOD();
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
		const FWorldPartitionActorDesc* ActorDesc = Pair.Value.Get();
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
PRAGMA_ENABLE_OPTIMIZATION