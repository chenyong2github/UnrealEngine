// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"

#if WITH_EDITOR

#include "WorldPartition/RuntimeSpatialHash/RuntimeSpatialHashGridHelper.h"
#include "WorldPartition/HLOD/HLODBuilder.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"

#include "HAL/PlatformFileManager.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"

#include "Algo/AnyOf.h"
#include "Algo/ForEach.h"

#include "EngineUtils.h"

#include "DistanceFieldAtlas.h"
#include "MeshCardRepresentation.h"
#include "StaticMeshCompiler.h"

#include "AssetRegistryModule.h"
#include "AssetData.h"

#include "IDirectoryWatcher.h"
#include "DirectoryWatcherModule.h"


DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionRuntimeSpatialHashHLOD, Log, All);


static void SavePackage(UPackage* Package, ISourceControlHelper* SourceControlHelper)
{
	if (SourceControlHelper)
	{
		SourceControlHelper->Save(Package);
	}
	else
	{
		Package->MarkAsFullyLoaded();

		FPackagePath PackagePath = FPackagePath::FromPackageNameChecked(Package->GetName());
		const FString PackageFileName = PackagePath.GetLocalFullPath();
		if (!UPackage::SavePackage(Package, nullptr, RF_Standalone, *PackageFileName))
		{
			UE_LOG(LogWorldPartitionRuntimeSpatialHashHLOD, Error, TEXT("Error saving package %s."), *Package->GetName());
			check(0);
		}
	}
}

static void DeletePackage(const FString& PackageName, ISourceControlHelper* SourceControlHelper)
{
	FPackagePath PackagePath = FPackagePath::FromPackageNameChecked(PackageName);
	const FString PackageFileName = PackagePath.GetLocalFullPath();

	if (SourceControlHelper)
	{
		SourceControlHelper->Delete(PackageFileName);
	}
	else
	{
		FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*PackageFileName);
	}
}

static void DeletePackage(UPackage* Package, ISourceControlHelper* SourceControlHelper)
{
	if (SourceControlHelper)
	{
		SourceControlHelper->Delete(Package);
	}
	else
	{
		DeletePackage(Package->GetName(), SourceControlHelper);
	}
}

static void DeletePackage(FWorldPartitionActorDesc* ActorDesc, ISourceControlHelper* SourceControlHelper)
{
	if (ActorDesc->IsLoaded())
	{
		DeletePackage(ActorDesc->GetActor()->GetPackage(), SourceControlHelper);
	}
	else
	{
		DeletePackage(ActorDesc->GetActorPackage().ToString(), SourceControlHelper);
	}
}

static TArray<FGuid> GenerateHLODsForGrid(UWorldPartition* WorldPartition, const FSpatialHashRuntimeGrid& RuntimeGrid, uint32 HLODLevel, FHLODCreationContext& Context, ISourceControlHelper* SourceControlHelper, bool bCreateActorsOnly, const TArray<const FActorClusterInstance*>& ClusterInstances)
{
	auto HasExceededMaxMemory = []()
	{
		const uint64 MemoryMinFreePhysical = 1024ll * 1024 * 1024;
		const uint64 MemoryMaxUsedPhysical = 16384ll * 1024 * 1024l;
		const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
		return (MemStats.AvailablePhysical < MemoryMinFreePhysical) || (MemStats.UsedPhysical >= MemoryMaxUsedPhysical);
	};

	auto DoCollectGarbage = []()
	{
		if (GDistanceFieldAsyncQueue)
		{
			GDistanceFieldAsyncQueue->BlockUntilAllBuildsComplete();
		}

		if (GCardRepresentationAsyncQueue)
		{
			GCardRepresentationAsyncQueue->BlockUntilAllBuildsComplete();
		}

		const FPlatformMemoryStats MemStatsBefore = FPlatformMemory::GetStats();
		CollectGarbage(RF_NoFlags, true);
		const FPlatformMemoryStats MemStatsAfter = FPlatformMemory::GetStats();

		UE_LOG(LogWorldPartitionRuntimeSpatialHashHLOD, Warning, TEXT("AvailablePhysical:%.2fGB AvailableVirtual %.2fGB"),
			((int64)MemStatsAfter.AvailablePhysical - (int64)MemStatsBefore.AvailablePhysical) / (1024.0 * 1024.0 * 1024.0),
			((int64)MemStatsAfter.AvailableVirtual - (int64)MemStatsBefore.AvailableVirtual) / (1024.0 * 1024.0 * 1024.0)
		);
	};

	const FBox WorldBounds = WorldPartition->GetWorldBounds();

	const FSquare2DGridHelper PartitionedActors = GetPartitionedActors(WorldPartition, WorldBounds, RuntimeGrid, ClusterInstances);
	const FSquare2DGridHelper::FGridLevel::FGridCell& AlwaysLoadedCell = PartitionedActors.GetAlwaysLoadedCell();

	auto ShouldGenerateHLODs = [&AlwaysLoadedCell](const FSquare2DGridHelper::FGridLevel::FGridCell& GridCell, const FSquare2DGridHelper::FGridLevel::FGridCellDataChunk& GridCellDataChunk)
	{
		const bool bIsCellAlwaysLoaded = &GridCell == &AlwaysLoadedCell;
		const bool bShouldGenerateHLODForDataLayers = GridCellDataChunk.GetDataLayers().IsEmpty() ? true : Algo::AnyOf(GridCellDataChunk.GetDataLayers(), [](const UDataLayer* DataLayer) { return DataLayer->ShouldGenerateHLODs(); });
		const bool bChunkHasActors = !GridCellDataChunk.GetActors().IsEmpty();

		const bool bShouldGenerateHLODs = !bIsCellAlwaysLoaded && bShouldGenerateHLODForDataLayers && bChunkHasActors;
		return bShouldGenerateHLODs;
	};

	// Quick pass to compute the number of cells we'll have to process, to provide a meaningful progress display
	int32 NbCellsToProcess = 0;
	PartitionedActors.ForEachCells([&](const FIntVector& CellCoord)
	{
		const FSquare2DGridHelper::FGridLevel::FGridCell& GridCell = PartitionedActors.GetCell(CellCoord);

		for (const FSquare2DGridHelper::FGridLevel::FGridCellDataChunk& GridCellDataChunk : GridCell.GetDataChunks())
		{
			const bool bShouldGenerateHLODs = ShouldGenerateHLODs(GridCell, GridCellDataChunk);
			if (bShouldGenerateHLODs)
			{
				NbCellsToProcess++;
			}
		}
	});

	FScopedSlowTask SlowTask(NbCellsToProcess, FText::FromString(FString::Printf(TEXT("Building HLODs for grid %s..."), *RuntimeGrid.GridName.ToString())));
	SlowTask.MakeDialog();

	FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TArray<FGuid> GridHLODActors;
	PartitionedActors.ForEachCells([&](const FIntVector& CellCoord)
	{
		const FSquare2DGridHelper::FGridLevel::FGridCell& GridCell = PartitionedActors.GetCell(CellCoord);

		FBox2D CellBounds2D;
		PartitionedActors.GetCellBounds(CellCoord, CellBounds2D);
		FBox CellBounds = FBox(FVector(CellBounds2D.Min, WorldBounds.Min.Z), FVector(CellBounds2D.Max, WorldBounds.Max.Z));

		for (const FSquare2DGridHelper::FGridLevel::FGridCellDataChunk& GridCellDataChunk : GridCell.GetDataChunks())
		{
			const bool bShouldGenerateHLODs = ShouldGenerateHLODs(GridCell, GridCellDataChunk);
			if (bShouldGenerateHLODs)
			{
				SlowTask.EnterProgressFrame(1);

				FName CellName = UWorldPartitionRuntimeSpatialHash::GetCellName(WorldPartition, RuntimeGrid.GridName, CellCoord.Z, CellCoord.X, CellCoord.Y, GridCellDataChunk.GetDataLayersID());

				TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*CellName.ToString());

				UE_LOG(LogWorldPartitionRuntimeSpatialHashHLOD, Display, TEXT("[%d / %d] Creating/updating HLOD actor(s) for cell %s"), (int32)SlowTask.CompletedWork, (int32)SlowTask.TotalAmountOfWork, *CellName.ToString());

				FHLODCreationParams CreationParams;
				CreationParams.WorldPartition = WorldPartition;
				CreationParams.GridIndexX = CellCoord.X;
				CreationParams.GridIndexY = CellCoord.Y;
				CreationParams.GridIndexZ = CellCoord.Z;
				CreationParams.DataLayersID = GridCellDataChunk.GetDataLayersID();
				CreationParams.CellName = CellName;
				CreationParams.CellBounds = CellBounds;
				CreationParams.HLODLevel = HLODLevel;

				TArray<AWorldPartitionHLOD*> CellHLODActors = FHLODBuilderUtilities::CreateHLODActors(Context, CreationParams, GridCellDataChunk.GetActors(), GridCellDataChunk.GetDataLayers());
				if (!CellHLODActors.IsEmpty())
				{
					TArray<AWorldPartitionHLOD*> NewCellHLODActors;

					for (AWorldPartitionHLOD* CellHLODActor : CellHLODActors)
					{
						FGuid ActorGuid = CellHLODActor->GetActorGuid();
						GridHLODActors.Add(ActorGuid);

						UPackage* CellHLODActorPackage = CellHLODActor->GetPackage();
						if (CellHLODActorPackage->HasAnyPackageFlags(PKG_NewlyCreated))
						{
							NewCellHLODActors.Add(CellHLODActor);
						}
					}

					for (AWorldPartitionHLOD* CellHLODActor : CellHLODActors)
					{
						if (!bCreateActorsOnly)
						{
							FStaticMeshCompilingManager::Get().FinishAllCompilation();

							if (HasExceededMaxMemory())
							{
								DoCollectGarbage();
							}

							CellHLODActor->BuildHLOD();
						}

						if (CellHLODActor->GetPackage()->IsDirty())
						{
							SavePackage(CellHLODActor->GetPackage(), SourceControlHelper);
						}
					}

					// Manually tick the directory watcher and the asset registry to register newly created actors
					DirectoryWatcherModule.Get()->Tick(-1.0f);
					AssetRegistryModule.Get().Tick(-1.0f);

					// Update newly created HLOD actors
					for (AWorldPartitionHLOD* CellHLODActor : NewCellHLODActors)
					{
						Context.ActorReferences.Emplace(WorldPartition, CellHLODActor->GetActorGuid());
					}
				}

				// Unload actors
				Context.ActorReferences.Empty();
			}
		}
	});

	// Need to collect garbage here since some HLOD actors have been marked pending kill when destroying them
	// and they may be loaded when generating the next HLOD layer.
	DoCollectGarbage();

	return GridHLODActors;
}

// Find all HLOD grids from the HLODLayer assets we have and build a dependency graph
static void GatherHLODGrids(TMap<FName, FSpatialHashRuntimeGrid>& OutHLODGrids, TMap<FName, TSet<FName>>& OutHLODGridsGraph)
{
	// We don't know the HLOD level for those grids yet, we'll have to compute a graph of dependencies between our HLOD layers first
	// Start with level 0, we'll rename once we have the correct info
	const uint32 InitialHLODLevel = 0;

	// Gather up all HLODLayer assets
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> HLODLayerAssets;
	AssetRegistryModule.Get().GetAssetsByClass(UHLODLayer::StaticClass()->GetFName(), HLODLayerAssets);

	// Find all used HLOD grids & build a dependency graph
	for (const FAssetData& HLODLayerAsset : HLODLayerAssets)
	{
		if (const UHLODLayer* HLODLayer = Cast<UHLODLayer>(HLODLayerAsset.GetAsset()))
		{
			FSpatialHashRuntimeGrid HLODGrid;
			HLODGrid.CellSize = HLODLayer->GetCellSize();
			HLODGrid.LoadingRange = HLODLayer->GetLoadingRange();
			HLODGrid.DebugColor = FLinearColor::Red;
			HLODGrid.GridName = HLODLayer->GetRuntimeGrid(InitialHLODLevel);

			OutHLODGrids.Emplace(HLODGrid.GridName, HLODGrid);

			TSet<FName>& HLODParentGrids = OutHLODGridsGraph.FindOrAdd(HLODGrid.GridName);
			if (const UHLODLayer* ParentHLODLayer = HLODLayer->GetParentLayer().LoadSynchronous())
			{
				HLODParentGrids.Add(ParentHLODLayer->GetRuntimeGrid(InitialHLODLevel));
			}
		}
	}
}

// Sort HLOD grids in the order they'll need to be processed for HLOD generation
static bool SortHLODGrids(const TMap<FName, TSet<FName>>& InHLODGridsGraph, TArray<FName>& OutSortedGrids, TMap<FName, uint32>& OutGridsDepth)
{
	TSet<FName>		ProcessedGridsSet;	// Processed grids
	TSet<FName>		VisitedGridsSet;	// Visited grids

	TFunction<bool(const FName, uint32)> VisitGraph = [&](const FName GridName, uint32 CurrentDepth) -> bool
	{
		uint32& GridDepth = OutGridsDepth.FindOrAdd(GridName);
		GridDepth = FMath::Max(GridDepth, CurrentDepth);

		if (ProcessedGridsSet.Contains(GridName))
		{
			return true;
		}

		// Detect cyclic dependencies between HLOD grids...
		if (VisitedGridsSet.Contains(GridName))
		{
			return false; // Not a DAG
		}

		VisitedGridsSet.Add(GridName);

		for (const FName ParentGridName : InHLODGridsGraph.FindChecked(GridName))
		{
			if (!VisitGraph(ParentGridName, CurrentDepth + 1))
			{
				return false;
			}
		}

		VisitedGridsSet.Remove(GridName);

		ProcessedGridsSet.Add(GridName);
		OutSortedGrids.Insert(GridName, 0);
		return true;
	};

	bool bIsADAG = true;
	for (const auto& HLODGridEntry : InHLODGridsGraph)
	{
		FName GridName = HLODGridEntry.Key;
		if (!ProcessedGridsSet.Contains(GridName))
		{
			const uint32 HLODDepth = 0;
			bIsADAG = VisitGraph(GridName, HLODDepth);
			if (!bIsADAG)
			{
				break;
			}
		}
	}

	// Remove leafs, those grids are the standard runtime grids, we'll generate HLOD for those in a first pass
	OutSortedGrids.RemoveAll([&InHLODGridsGraph](const FName GridName) { return !InHLODGridsGraph.Contains(GridName); });

	return bIsADAG;
}

static void RenameHLODGrids(TMap<FName, FSpatialHashRuntimeGrid>& HLODGrids, TArray<FName>& SortedGrids, TMap<FName, uint32>& GridsDepth)
{
	// Build a mapping of old -> new names
	TMap<FName, FName> NewNamesMapping;
	for (const auto& HLODGrid : HLODGrids)
	{
		NewNamesMapping.Emplace(HLODGrid.Key, UHLODLayer::GetRuntimeGridName(GridsDepth[HLODGrid.Key], HLODGrid.Value.CellSize, HLODGrid.Value.LoadingRange));
	}

	// Remplace map entries
	for (const auto& Names : NewNamesMapping)
	{
		FSpatialHashRuntimeGrid RuntimeGrid = HLODGrids.FindAndRemoveChecked(Names.Key);
		RuntimeGrid.GridName = Names.Value;
		HLODGrids.Emplace(Names.Value, RuntimeGrid);

		int32 GridDepth = GridsDepth.FindAndRemoveChecked(Names.Key);
		GridsDepth.Emplace(Names.Value, GridDepth);
	}

	// Replace arrays entries
	for (FName& Name : SortedGrids)
	{
		Name = NewNamesMapping[Name];
	}
}

// Create/destroy HLOD grid actors
static void UpdateHLODGridsActors(UWorld* World, const TMap<FName, FSpatialHashRuntimeGrid>& HLODGrids, ISourceControlHelper* SourceControlHelper)
{
	static const FName HLODGridTag = TEXT("HLOD");
	static const uint32 HLODGridTagLen = HLODGridTag.ToString().Len();

	// Gather all existing HLOD grid actors, see if some are unused and needs to be deleted
	TMap<FName, ASpatialHashRuntimeGridInfo*> ExistingGridActors;
	for (TActorIterator<ASpatialHashRuntimeGridInfo> ItRuntimeGridActor(World); ItRuntimeGridActor; ++ItRuntimeGridActor)
	{
		ASpatialHashRuntimeGridInfo* GridActor = *ItRuntimeGridActor;
		if (GridActor->ActorHasTag(HLODGridTag))
		{
			if (HLODGrids.Contains(GridActor->GridSettings.GridName))
			{
				ExistingGridActors.Emplace(GridActor->GridSettings.GridName, GridActor);
			}
			else
			{
				World->DestroyActor(GridActor);

				DeletePackage(GridActor->GetPackage(), SourceControlHelper);
			}
		}
	}

	// Create missing HLOD grid actors 
	for (const auto& HLODGridEntry : HLODGrids)
	{
		FName GridName = HLODGridEntry.Key;
		if (!ExistingGridActors.Contains(GridName))
		{
			const FSpatialHashRuntimeGrid& GridSettings = HLODGridEntry.Value;

			FActorSpawnParameters SpawnParams;
			SpawnParams.bCreateActorPackage = true;
			ASpatialHashRuntimeGridInfo* GridActor = World->SpawnActor<ASpatialHashRuntimeGridInfo>(SpawnParams);
			GridActor->Tags.Add(HLODGridTag);
			GridActor->SetActorLabel(GridSettings.GridName.ToString());
			GridActor->GridSettings = GridSettings;

			// Setup grid debug color to match HLODColorationColors
			const uint32 LastLODColorationColorIdx = GEngine->HLODColorationColors.Num() - 1;
			uint32 HLODLevel;
			if (!LexTryParseString(HLODLevel, *GridSettings.GridName.ToString().Mid(HLODGridTagLen, 1)))
			{
				HLODLevel = LastLODColorationColorIdx;
			}
			HLODLevel = FMath::Clamp(HLODLevel + 2, 0U, LastLODColorationColorIdx);
			GridActor->GridSettings.DebugColor = GEngine->HLODColorationColors[HLODLevel];

			SavePackage(GridActor->GetPackage(), SourceControlHelper);
		}
	}
}

bool UWorldPartitionRuntimeSpatialHash::GenerateHLOD(ISourceControlHelper* SourceControlHelper, bool bCreateActorsOnly)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionRuntimeSpatialHash::GenerateHLOD);

	if (!Grids.Num())
	{
		UE_LOG(LogWorldPartitionRuntimeSpatialHashHLOD, Error, TEXT("Invalid partition grids setup"));
		return false;
	}

	// Find all used HLOD grids & build a dependency graph
	TMap<FName, FSpatialHashRuntimeGrid> HLODGrids;
	TMap<FName, TSet<FName>>			 HLODGridsGraph;
	GatherHLODGrids(HLODGrids, HLODGridsGraph);

	// Sort HLOD grids in the order they'll need to be processed
	TArray<FName>	SortedGrids;		// HLOD Grids, sorted in the order they'll need to be processed for HLOD generation
	TMap<FName, uint32>	GridsDepth;		// Depth - Used to obtain the HLOD level
	bool bIsADAG = SortHLODGrids(HLODGridsGraph, SortedGrids, GridsDepth);
	if (!bIsADAG)
	{
		UE_LOG(LogWorldPartitionRuntimeSpatialHashHLOD, Error, TEXT("Invalid grids setup, cycles detected"));
		return false;
	}

	// Now that we computed proper depth per grid, rename the grids
	RenameHLODGrids(HLODGrids, SortedGrids, GridsDepth);

	TMap<FName, int32> GridsMapping;
	GridsMapping.Add(NAME_None, 0);
	for (int32 i = 0; i < Grids.Num(); i++)
	{
		const FSpatialHashRuntimeGrid& Grid = Grids[i];
		check(!GridsMapping.Contains(Grid.GridName));
		GridsMapping.Add(Grid.GridName, i);
	}

	UWorldPartition* WorldPartition = GetOuterUWorldPartition();

	// HLOD creation context
	FHLODCreationContext Context;
	TArray<FWorldPartitionHandle> InvalidHLODActors;

	for (UActorDescContainer::TIterator<AWorldPartitionHLOD> HLODIterator(WorldPartition); HLODIterator; ++HLODIterator)
	{
		FWorldPartitionHandle HLODActorHandle(WorldPartition, HLODIterator->GetGuid());

		if (HLODIterator->GetCellHash())
		{
			Context.HLODActorDescs.Add(HLODIterator->GetCellHash(), HLODActorHandle);
		}
		else
		{
			InvalidHLODActors.Add(HLODActorHandle);
		}
	}

	// Create actor clusters - ignore HLOD actors
	FActorClusterContext ClusterContext(WorldPartition, this, TOptional<FActorClusterContext::FFilterPredicate>([](const FWorldPartitionActorDescView& ActorDescView)
		{
			return !ActorDescView.GetActorClass()->IsChildOf<AWorldPartitionHLOD>();
		}), /* bInIncludeChildContainers=*/ false);
		
	TArray<TArray<const FActorClusterInstance*>> GridsClusters;
	GridsClusters.InsertDefaulted(0, Grids.Num());

	for (const FActorClusterInstance& ClusterInstance : ClusterContext.GetClusterInstances())
	{
		const FActorCluster* ActorCluster = ClusterInstance.Cluster;
		check(ActorCluster);
		int32* FoundIndex = GridsMapping.Find(ActorCluster->RuntimeGrid);
		if (!FoundIndex)
		{
			UE_LOG(LogWorldPartitionRuntimeSpatialHashHLOD, Error, TEXT("Invalid partition grid '%s' referenced by actor cluster"), *ActorCluster->RuntimeGrid.ToString());
		}

		int32 GridIndex = FoundIndex ? *FoundIndex : 0;
		GridsClusters[GridIndex].Add(&ClusterInstance);
	}

	// Keep track of all valid HLOD actors, along with which runtime grid they live in
	TMap<FName, TArray<FGuid>> GridsHLODActors;

	auto GenerateHLODs = [&GridsHLODActors, WorldPartition, &GridsDepth, &Context, SourceControlHelper, bCreateActorsOnly](const FSpatialHashRuntimeGrid& RuntimeGrid, uint32 HLODLevel, const TArray<const FActorClusterInstance*>& ActorClusterInstances)
	{
		// Generate HLODs for this grid
		TArray<FGuid> HLODActors = GenerateHLODsForGrid(WorldPartition, RuntimeGrid, HLODLevel, Context, SourceControlHelper, bCreateActorsOnly, ActorClusterInstances);

		for (const FGuid& HLODActorGuid : HLODActors)
		{
			FWorldPartitionActorDesc* HLODActorDesc = WorldPartition->GetActorDesc(HLODActorGuid);
			check(HLODActorDesc);

			GridsHLODActors.FindOrAdd(HLODActorDesc->GetRuntimeGrid()).Add(HLODActorGuid);
		}
	};

	// Generate HLODs for the standard runtime grids (HLOD 0)
	for (int32 GridIndex = 0; GridIndex < Grids.Num(); GridIndex++)
	{
		// Generate HLODs for this grid - retrieve actors from the world partition
		GenerateHLODs(Grids[GridIndex], 0, GridsClusters[GridIndex]);
	}

	// Now, go on and create HLOD actors from HLOD grids (HLOD 1-N)
	FActorContainerInstance& MainContainerInstance = *ClusterContext.GetClusterInstance(WorldPartition);
	for (const FName HLODGridName : SortedGrids)
	{
		// No need to process empty grids
		if (!GridsHLODActors.Contains(HLODGridName))
		{
			HLODGrids.Remove(HLODGridName); // No need to keep this grid around, we have no actors in it
			continue;
		}

		// Create one actor cluster per HLOD actor
		TArray<FActorCluster> HLODActorClusters;
		TArray<FActorClusterInstance> HLODActorClusterInstances;
		TArray<const FActorClusterInstance*> HLODActorClusterInstancePtrs;
		HLODActorClusters.Reserve(GridsHLODActors[HLODGridName].Num());
		HLODActorClusterInstances.Reserve(GridsHLODActors[HLODGridName].Num());
		HLODActorClusterInstancePtrs.Reserve(GridsHLODActors[HLODGridName].Num());

		for (const FGuid& HLODActorGuid : GridsHLODActors[HLODGridName])
		{
			const FWorldPartitionActorDescView& HLODActorDescView = MainContainerInstance.GetActorDescView(HLODActorGuid);
			FActorCluster& NewHLODActorCluster = HLODActorClusters.Emplace_GetRef(WorldPartition->GetWorld(), HLODActorDescView, HLODActorDescView.GetGridPlacement());
			FActorClusterInstance& NewHLODActorClusterInstance = HLODActorClusterInstances.Emplace_GetRef(&NewHLODActorCluster, &MainContainerInstance);
			HLODActorClusterInstancePtrs.Add(&NewHLODActorClusterInstance);
		};

		// Generate HLODs for this grid - retrieve actors from our ValidHLODActors map
		// We can't rely on actor descs for newly created HLOD actors
		GenerateHLODs(HLODGrids[HLODGridName], GridsDepth[HLODGridName] + 1, HLODActorClusterInstancePtrs);
	}

	auto DeleteHLODActor = [&SourceControlHelper](FWorldPartitionHandle ActorHandle)
	{
		FWorldPartitionActorDesc* HLODActorDesc = ActorHandle.Get();
		check(HLODActorDesc);

		DeletePackage(HLODActorDesc, SourceControlHelper);
	};

	// Destroy all unreferenced HLOD actors
	for (const auto& HLODActorPair : Context.HLODActorDescs)
	{
		DeleteHLODActor(HLODActorPair.Value);
	}

	// Destroy all invalid HLOD actors
	Algo::ForEach(InvalidHLODActors, DeleteHLODActor);

	// Create/destroy HLOD grid actors
	UpdateHLODGridsActors(GetWorld(), HLODGrids, SourceControlHelper);

	return true;
}

#endif // #if WITH_EDITOR