// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * WorldPartitionStreamingPolicy
 *
 * Base class for World Partition Runtime Streaming Policy
 *
 */

#pragma once

#include "Containers/Set.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartitionStreamingPolicy.generated.h"

class UWorldPartition;

UCLASS(Abstract, Within = WorldPartition)
class UWorldPartitionStreamingPolicy : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	virtual void UpdateStreamingState();
	virtual void SetTargetStateForCells(EWorldPartitionRuntimeCellState TargetState, const TSet<const UWorldPartitionRuntimeCell*>& Cells);
	virtual class ULevel* GetPreferredLoadedLevelToAddToWorld() const;
	virtual FVector2D GetDrawRuntimeHash2DDesiredFootprint(const FVector2D& CanvasSize);
	virtual void DrawRuntimeHash2D(class UCanvas* Canvas, const FVector2D& PartitionCanvasSize, FVector2D& Offset);
	virtual void DrawRuntimeHash3D();
	virtual void DrawRuntimeCellsDetails(class UCanvas* Canvas, FVector2D& Offset) {}
	virtual void DrawStreamingStatusLegend(class UCanvas* Canvas, FVector2D& Offset) {}

	virtual bool IsStreamingCompleted(EWorldPartitionRuntimeCellState QueryState, const TArray<FWorldPartitionStreamingQuerySource>& QuerySources, bool bExactState = true) const;

#if WITH_EDITOR
	virtual TSubclassOf<class UWorldPartitionRuntimeCell> GetRuntimeCellClass() const PURE_VIRTUAL(UWorldPartitionStreamingPolicy::GetRuntimeCellClass, return UWorldPartitionRuntimeCell::StaticClass(); );

	// PIE/Game methods
	virtual void PrepareActorToCellRemapping() {}
	virtual void ClearActorToCellRemapping() {}
	virtual void RemapSoftObjectPath(FSoftObjectPath& ObjectPath) {}
#endif

	virtual UObject* GetSubObject(const TCHAR* SubObjectPath) { return nullptr; }

	const TArray<FWorldPartitionStreamingSource>& GetStreamingSources() const { return StreamingSources; }

protected:
	virtual void SetCellsStateToLoaded(const TSet<const UWorldPartitionRuntimeCell*>& ToLoadCells);
	virtual void SetCellsStateToActivated(const TSet<const UWorldPartitionRuntimeCell*>& ToActivateCells);
	virtual void SetCellsStateToUnloaded(const TSet<const UWorldPartitionRuntimeCell*>& ToUnloadCells);
	virtual int32 GetCellLoadingCount() const { return 0; }
	virtual int32 GetMaxCellsToLoad() const;
	virtual void UpdateStreamingSources();

	const UWorldPartition* WorldPartition;
	TSet<const UWorldPartitionRuntimeCell*> LoadedCells;
	TSet<const UWorldPartitionRuntimeCell*> ActivatedCells;
	TArray<FWorldPartitionStreamingSource> StreamingSources;

	UWorldPartitionRuntimeHash::FStreamingSourceCells FrameActivateCells;
	UWorldPartitionRuntimeHash::FStreamingSourceCells FrameLoadCells;
	
	int32 UpdateStreamingStateEpoch;
	mutable int32 SortedAddToWorldCellsEpoch;
	mutable TArray<const UWorldPartitionRuntimeCell*, TInlineAllocator<256>> SortedAddToWorldCells;

	int32 DataLayersStatesServerEpoch;
};