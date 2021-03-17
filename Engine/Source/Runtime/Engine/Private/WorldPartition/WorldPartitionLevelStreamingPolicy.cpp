// Copyright Epic Games, Inc. All Rights Reserved.

/*
 * WorldPartitionLevelStreamingPolicy implementation
 */

#include "WorldPartition/WorldPartitionLevelStreamingPolicy.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeLevelStreamingCell.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionLevelHelper.h"
#include "Engine/Level.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "Misc/PackageName.h"
#endif

static int32 GMaxLoadingLevelStreamingCells = 4;
static FAutoConsoleVariableRef CMaxLoadingLevelStreamingCells(
	TEXT("wp.Runtime.MaxLoadingLevelStreamingCells"),
	GMaxLoadingLevelStreamingCells,
	TEXT("Used to limit the number of concurrent loading world partition streaming cells."));

int32 UWorldPartitionLevelStreamingPolicy::GetCellLoadingCount() const
{
	int32 CellLoadingCount = 0;
	for (const UWorldPartitionRuntimeCell* ActivatedCell : ActivatedCells)
	{
		const UWorldPartitionRuntimeLevelStreamingCell* Cell = Cast<const UWorldPartitionRuntimeLevelStreamingCell>(ActivatedCell);
		if (!Cell->IsAlwaysLoaded())
		{
			if (UWorldPartitionLevelStreamingDynamic* LevelStreaming = Cell->GetLevelStreaming())
			{
				ULevelStreaming::ECurrentState CurrentState = LevelStreaming->GetCurrentState();
				if (CurrentState == ULevelStreaming::ECurrentState::Removed || CurrentState == ULevelStreaming::ECurrentState::Unloaded || CurrentState == ULevelStreaming::ECurrentState::Loading)
				{
					++CellLoadingCount;
				}
			}
		}
	}
	return CellLoadingCount;
}

void UWorldPartitionLevelStreamingPolicy::SetTargetStateForCells(EWorldPartitionRuntimeCellState TargetState, const TSet<const UWorldPartitionRuntimeCell*>& Cells)
{
	switch (TargetState)
	{
	case EWorldPartitionRuntimeCellState::Unloaded:
		SetCellsStateToUnloaded(Cells);
		break;
	case EWorldPartitionRuntimeCellState::Loaded:
		SetCellsStateToLoaded(Cells);
		break;
	case EWorldPartitionRuntimeCellState::Activated:
		SetCellsStateToActivated(Cells);
		break;
	default:
		check(false);
	}
}

void UWorldPartitionLevelStreamingPolicy::SetCellsStateToLoaded(const TSet<const UWorldPartitionRuntimeCell*>& ToLoadCells)
{
	// This policy limits the number of concurrent loading streaming cells
	int32 CellLoadingCount = GetCellLoadingCount();
	int32 MaxCellsToLoad = GMaxLoadingLevelStreamingCells - CellLoadingCount;
	
	// Sort cells based on importance
	TArray<const UWorldPartitionRuntimeCell*, TInlineAllocator<256>> SortedCells;
	WorldPartition->RuntimeHash->SortStreamingCellsByImportance(ToLoadCells, StreamingSources, SortedCells);

	// Trigger cell loading. Depending on actual state of cell limit loading.
	for (const UWorldPartitionRuntimeCell* Cell : SortedCells)
	{
		UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartitionLevelStreamingPolicy::LoadCells %s"), *Cell->GetName());
		const UWorldPartitionRuntimeLevelStreamingCell* LevelCell = CastChecked<const UWorldPartitionRuntimeLevelStreamingCell>(Cell);
		if (ActivatedCells.Contains(Cell))
		{
			LevelCell->Deactivate();
			ActivatedCells.Remove(Cell);
			LoadedCells.Add(Cell);
		}
		else if (MaxCellsToLoad > 0)
		{
			LevelCell->Load();
			LoadedCells.Add(Cell);
			if (!Cell->IsAlwaysLoaded())
			{
				--MaxCellsToLoad;
			}
		}
	}
}

void UWorldPartitionLevelStreamingPolicy::SetCellsStateToActivated(const TSet<const UWorldPartitionRuntimeCell*>& ToActivateCells)
{
	// This policy limits the number of concurrent loading streaming cells
	int32 CellLoadingCount = GetCellLoadingCount();
	int32 MaxCellsToLoad = GMaxLoadingLevelStreamingCells - CellLoadingCount;
	if (MaxCellsToLoad <= 0)
	{
		return;
	}

	// Sort cells based on importance
	TArray<const UWorldPartitionRuntimeCell*, TInlineAllocator<256>> SortedCells;
	WorldPartition->RuntimeHash->SortStreamingCellsByImportance(ToActivateCells, StreamingSources, SortedCells);

	// Trigger cell activation. Depending on actual state of cell limit loading.
	for (const UWorldPartitionRuntimeCell* Cell : SortedCells)
	{
		UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartitionLevelStreamingPolicy::ActivateCells %s"), *Cell->GetName());
		const UWorldPartitionRuntimeLevelStreamingCell* LevelCell = CastChecked<const UWorldPartitionRuntimeLevelStreamingCell>(Cell);
		if (LoadedCells.Contains(LevelCell))
		{
			LoadedCells.Remove(Cell);
			ActivatedCells.Add(Cell);
			LevelCell->Activate();
		}
		else if (MaxCellsToLoad > 0)
		{
			if (!Cell->IsAlwaysLoaded())
			{
				--MaxCellsToLoad;
			}
			ActivatedCells.Add(Cell);
			LevelCell->Activate();
		}		
	}
}

void UWorldPartitionLevelStreamingPolicy::SetCellsStateToUnloaded(const TSet<const UWorldPartitionRuntimeCell*>& ToUnloadCells)
{
	for (const UWorldPartitionRuntimeCell* Cell : ToUnloadCells)
	{
		UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartitionLevelStreamingPolicy::UnloadCells %s"), *Cell->GetName());
		const UWorldPartitionRuntimeLevelStreamingCell* LevelCell = CastChecked<const UWorldPartitionRuntimeLevelStreamingCell>(Cell);
		LevelCell->Unload();
		ActivatedCells.Remove(Cell);
		LoadedCells.Remove(Cell);
	}
}

ULevel* UWorldPartitionLevelStreamingPolicy::GetPreferredLoadedLevelToAddToWorld() const
{
	check(WorldPartition->IsInitialized());
	UWorld* World = WorldPartition->GetWorld();
	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}

	// Sort loaded cells based on importance (only those with a streaming level in MakingVisible state)
	TArray<const UWorldPartitionRuntimeCell*, TInlineAllocator<256>> SortedMakingVisibileCells;
	TSet<const UWorldPartitionRuntimeCell*> MakingVisibileCells;
	MakingVisibileCells.Reserve(ActivatedCells.Num());
	for (const UWorldPartitionRuntimeCell* LoadedCell : ActivatedCells)
	{
		if (const UWorldPartitionRuntimeLevelStreamingCell* LevelSreamingCell = Cast<const UWorldPartitionRuntimeLevelStreamingCell>(LoadedCell))
		{
			ULevelStreaming* LevelStreaming = LevelSreamingCell->GetLevelStreaming();
			if (LevelStreaming && LevelStreaming->GetLoadedLevel() && (LevelStreaming->GetCurrentState() == ULevelStreaming::ECurrentState::MakingVisible))
			{
				MakingVisibileCells.Add(LoadedCell);
			}
		}
	}
	WorldPartition->RuntimeHash->SortStreamingCellsByImportance(MakingVisibileCells, StreamingSources, SortedMakingVisibileCells);
	return SortedMakingVisibileCells.Num() > 0 ? CastChecked<UWorldPartitionRuntimeLevelStreamingCell>(SortedMakingVisibileCells[0])->GetLevelStreaming()->GetLoadedLevel() : nullptr;
}

EWorldPartitionRuntimeCellState UWorldPartitionLevelStreamingPolicy::GetCurrentStateForCell(const UWorldPartitionRuntimeCell* Cell) const
{
	const UWorldPartitionRuntimeLevelStreamingCell* LevelCell = CastChecked<const UWorldPartitionRuntimeLevelStreamingCell>(Cell);
	return LevelCell->GetCurrentState();
}

#if WITH_EDITOR

FString UWorldPartitionLevelStreamingPolicy::GetCellPackagePath(const FName& InCellName, const UWorld* InWorld)
{
	if (InWorld->IsGameWorld())
	{
		// Set as memory package to avoid wasting time in FPackageName::DoesPackageExist
		return FString::Printf(TEXT("/Memory/%s"), *InCellName.ToString());
	}
	else
	{
		return FString::Printf(TEXT("/%s"), *InCellName.ToString());
	}
}

TSubclassOf<UWorldPartitionRuntimeCell> UWorldPartitionLevelStreamingPolicy::GetRuntimeCellClass() const
{
	return UWorldPartitionRuntimeLevelStreamingCell::StaticClass();
}

void UWorldPartitionLevelStreamingPolicy::OnBeginPlay()
{
	TSet<const UWorldPartitionRuntimeCell*> StreamingCells;
	WorldPartition->RuntimeHash->GetAllStreamingCells(StreamingCells, /*bIncludeDataLayers*/ true);

	// Build Actor-to-Cell remapping
	for (const UWorldPartitionRuntimeCell* Cell : StreamingCells)
	{
		const UWorldPartitionRuntimeLevelStreamingCell* StreamingCell = Cast<const UWorldPartitionRuntimeLevelStreamingCell>(Cell);
		check(StreamingCell);
		for (const FWorldPartitionRuntimeCellObjectMapping& CellObjectMap : StreamingCell->GetPackages())
		{
			ActorToCellRemapping.Add(CellObjectMap.Path, StreamingCell->GetFName());
		}
	}
}

void UWorldPartitionLevelStreamingPolicy::OnEndPlay()
{
	ActorToCellRemapping.Empty();
}

void UWorldPartitionLevelStreamingPolicy::RemapSoftObjectPath(FSoftObjectPath& ObjectPath)
{
	// Once we fix up the path to the proper streaming Level, normal flow of FixupForPIE will adapt the path for PIE.
	FString SrcPath = ObjectPath.ToString();

	FName* CellName = ActorToCellRemapping.Find(FName(*SrcPath));
	if (!CellName)
	{
		const FString& SubPathString = ObjectPath.GetSubPathString();
		if (SubPathString.StartsWith(TEXT("PersistentLevel.")))
		{
			int32 DotPos = ObjectPath.GetSubPathString().Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromStart);
			if (DotPos != INDEX_NONE)
			{
				DotPos = ObjectPath.GetSubPathString().Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromStart, DotPos + 1);
				if (DotPos != INDEX_NONE)
				{
					FString ActorSubPathString = ObjectPath.GetSubPathString().Left(DotPos);
					FString ActorPath = FString::Printf(TEXT("%s:%s"), *ObjectPath.GetAssetPathName().ToString(), *ActorSubPathString);
					CellName = ActorToCellRemapping.Find(FName(*ActorPath));
				}
			}
		}
	}

	if (CellName)
	{
		const FString ShortPackageOuterAndName = FPackageName::GetLongPackageAssetName(*SrcPath);
		int32 DelimiterIdx;
		if (ShortPackageOuterAndName.FindChar('.', DelimiterIdx))
		{
			const FString ObjectNameWithoutPackage = ShortPackageOuterAndName.Mid(DelimiterIdx + 1);
			const FString PackagePath = UWorldPartitionLevelStreamingPolicy::GetCellPackagePath(*CellName, WorldPartition->GetWorld());
			FString NewPath = FString::Printf(TEXT("%s.%s"), *PackagePath, *ObjectNameWithoutPackage);
			ObjectPath.SetPath(MoveTemp(NewPath));
		}
	}
}

#endif