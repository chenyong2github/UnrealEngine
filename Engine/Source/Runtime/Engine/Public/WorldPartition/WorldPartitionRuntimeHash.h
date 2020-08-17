// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "WorldPartition.h"
#include "WorldPartitionStreamingPolicy.h"
#include "WorldPartitionRuntimeHash.generated.h"

class UWorldPartitionRuntimeCell;

UCLASS(Abstract, Config=Engine, AutoExpandCategories=(WorldPartition), Within = WorldPartition)
class ENGINE_API UWorldPartitionRuntimeHash : public UObject
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
	virtual void SetDefaultValues() {}
	virtual bool GenerateStreaming(EWorldPartitionStreamingMode Mode, class UWorldPartitionStreamingPolicy* Policy) { return false; }
	virtual void FlushStreaming() {}
	virtual bool GenerateHLOD() { return false; }
#endif

	// Streaming interface
	virtual int32 GetAllStreamingCells(TSet<const UWorldPartitionRuntimeCell*>& Cells) const { return 0; }
	virtual int32 GetStreamingCells(const TArray<FWorldPartitionStreamingSource>& Sources, TSet<const UWorldPartitionRuntimeCell*>& Cells) const { return 0; };
	virtual void SortStreamingCellsByDistance(const TSet<const UWorldPartitionRuntimeCell*>& InCells, const TArray<FWorldPartitionStreamingSource>& InSources, TArray<const UWorldPartitionRuntimeCell*>& OutSortedCells) {}

	/* Returns desired footprint that ShowDebugInfo should take relative to given Canvas size (the value can exceed the given size).
	 * UWorldPartitionSubSystem will re-adapt the size relative to all others UWorldPartitionRuntimeHash and provide the correct size to ShowDebugInfo.
	 *
	 * Return ShowDebugInfo's desired footprint.
	 */
	virtual FVector2D GetShowDebugDesiredFootprint(const FVector2D& CanvasSize) const { return FVector2D::ZeroVector; }

	virtual void ShowDebugInfo(class UCanvas* Canvas, const TArray<FWorldPartitionStreamingSource>& Sources, const FVector2D& PartitionCanvasOffset, const FVector2D& PartitionCanvasSize) const {}
};