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
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartitionStreamingPolicy.generated.h"

class UWorldPartition;

UCLASS(Abstract, Within = WorldPartition)
class UWorldPartitionStreamingPolicy : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	virtual void UpdateStreamingState();
	virtual void LoadCells(const TSet<const UWorldPartitionRuntimeCell*>& ToLoadCells);
	virtual void UnloadCells(const TSet<const UWorldPartitionRuntimeCell*>& ToUnloadCells);
	virtual void LoadCell(const UWorldPartitionRuntimeCell* Cell) PURE_VIRTUAL(UWorldPartitionStreamingPolicy::LoadCell, );
	virtual void UnloadCell(const UWorldPartitionRuntimeCell* Cell) PURE_VIRTUAL(UWorldPartitionStreamingPolicy::UnloadCell, );
	virtual class ULevel* GetPreferredLoadedLevelToAddToWorld() const { return nullptr; }
	virtual FVector2D GetDrawRuntimeHash2DDesiredFootprint(const FVector2D& CanvasSize);
	virtual void DrawRuntimeHash2D(class UCanvas* Canvas, const FVector2D& PartitionCanvasOffset, const FVector2D& PartitionCanvasSize);
	virtual void DrawRuntimeHash3D();

#if WITH_EDITOR
	virtual TSubclassOf<class UWorldPartitionRuntimeCell> GetRuntimeCellClass() const PURE_VIRTUAL(UWorldPartitionStreamingPolicy::GetRuntimeCellClass, return UWorldPartitionRuntimeCell::StaticClass(); );

	// PIE/Game methods
	virtual void OnBeginPlay() {}
	virtual void OnEndPlay() {}
	virtual void RemapSoftObjectPath(FSoftObjectPath& ObjectPath) {}
#endif

	const TArray<FWorldPartitionStreamingSource>& GetStreamingSources() const { return StreamingSources; }

protected:
	void UpdateStreamingSources();

	bool bIsServerLoadingDone;
	const UWorldPartition* WorldPartition;
	TSet<const UWorldPartitionRuntimeCell*> LoadedCells;
	TArray<FWorldPartitionStreamingSource> StreamingSources; // Streaming sources (local to world partition)
};