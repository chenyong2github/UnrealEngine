// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartitionRuntimeSpatialHashCell.generated.h"

class UActorContainer;

UCLASS(Abstract, Within = WorldPartition)
class UWorldPartitionRuntimeSpatialHashCell : public UWorldPartitionRuntimeCell
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
#endif
	virtual bool CacheStreamingSourceInfo(const UWorldPartitionRuntimeCell::FStreamingSourceInfo& Info) const override;
	virtual int32 SortCompare(const UWorldPartitionRuntimeCell* InOther) const override;

	UPROPERTY()
	FVector Position;

	UPROPERTY()
	float Extent;

	UPROPERTY()
	int32 Level;
		
	// Used to determine if cell was requested by blocking source
	mutable bool CachedIsBlockingSource;

	// Represents the square distance from cell to the closest blocking streaming source
	mutable float CachedMinSquareDistanceToBlockingSource;

	// Represents the square distance from cell to the closest streaming source
	mutable float CachedMinSquareDistanceToSource;

	// Modulated distance to the different streaming sources used to sort relative priority amongst streaming cells
	// The value is affected by :
	// - All sources intersecting the cell
	// - The priority of each source
	// - The distance between the cell and each source
	// - The angle between the cell and each source orientation
	mutable float CachedSourceSortingDistance;

	mutable TArray<float> CachedSourceModulatedDistances;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UActorContainer> UnsavedActorsContainer;
#endif
};