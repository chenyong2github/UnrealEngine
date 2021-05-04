// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartitionRuntimeSpatialHashCell.generated.h"

UCLASS(Abstract, Within = WorldPartition)
class UWorldPartitionRuntimeSpatialHashCell : public UWorldPartitionRuntimeCell
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
#endif
	virtual bool CacheStreamingSourceInfo(const FWorldPartitionStreamingSource& Source) const override;
	virtual int32 SortCompare(const UWorldPartitionRuntimeCell* InOther) const override;

	UPROPERTY()
	FVector Position;

	UPROPERTY()
	int32 Level;

	// Computed and cached value used by SortCompare to sort Cells
	mutable float CachedSourceMinDistance;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UActorContainer* ActorContainer;
#endif
};