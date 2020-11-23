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
	for (const UWorldPartitionRuntimeCell* LoadedCell : LoadedCells)
	{
		const UWorldPartitionRuntimeLevelStreamingCell* Cell = Cast<const UWorldPartitionRuntimeLevelStreamingCell>(LoadedCell);
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

void UWorldPartitionLevelStreamingPolicy::LoadCells(const TSet<const UWorldPartitionRuntimeCell*>& ToLoadCells)
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
	WorldPartition->RuntimeHash->SortStreamingCellsByImportance(ToLoadCells, StreamingSources, SortedCells);

	// Trigger cell loading until we hit the maximum
	for (const UWorldPartitionRuntimeCell* Cell : SortedCells)
	{
		LoadCell(Cell);
		LoadedCells.Add(Cell);
		if (!Cell->IsAlwaysLoaded())
		{
			if (--MaxCellsToLoad <= 0)
			{
				break;
			}
		}
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
	MakingVisibileCells.Reserve(LoadedCells.Num());
	for (const UWorldPartitionRuntimeCell* LoadedCell : LoadedCells)
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

void UWorldPartitionLevelStreamingPolicy::LoadCell(const UWorldPartitionRuntimeCell* InCell)
{
	UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartitionLevelStreamingPolicy::LoadCell %s"), *InCell->GetName());
	const UWorldPartitionRuntimeLevelStreamingCell* Cell = Cast<const UWorldPartitionRuntimeLevelStreamingCell>(InCell);
	if (ensure(Cell))
	{
		Cell->Activate();
	}
}

void UWorldPartitionLevelStreamingPolicy::UnloadCell(const UWorldPartitionRuntimeCell* InCell)
{
	UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartitionLevelStreamingPolicy::UnloadCell %s"), *InCell->GetName());
	const UWorldPartitionRuntimeLevelStreamingCell* Cell = Cast<const UWorldPartitionRuntimeLevelStreamingCell>(InCell);
	if (ensure(Cell))
	{
		Cell->Deactivate();
	}
}

#if WITH_EDITOR

FString UWorldPartitionLevelStreamingPolicy::GetCellPackagePath(const FName& InCellName, const UWorld* InWorld)
{
	if (InWorld->IsPlayInEditor())
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

void UWorldPartitionLevelStreamingPolicy::PrepareForPIE()
{
	TSet<const UWorldPartitionRuntimeCell*> AlwaysLoadedCells;
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

		if (StreamingCell->IsAlwaysLoaded())
		{
			AlwaysLoadedCells.Add(Cell);
		}
	}

	// In PIE, this is where we load Always Loaded cells
	Super::LoadCells(AlwaysLoadedCells);
}

void UWorldPartitionLevelStreamingPolicy::OnPreFixupForPIE(int32 InPIEInstanceID, FSoftObjectPath& ObjectPath)
{
	// Once we fix up the path to the proper streaming Level, normal flow of FixupForPIE will adapt the path for PIE.
	FixupSoftObjectPath(ObjectPath);
}

// Fix up FSoftObjectPath to remap to proper streaming level
void UWorldPartitionLevelStreamingPolicy::FixupSoftObjectPath(FSoftObjectPath& ObjectPath)
{
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