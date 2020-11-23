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
	virtual void ImportFromWorldComposition(class UWorldComposition* WorldComposition) {}
	virtual bool GenerateStreaming(EWorldPartitionStreamingMode Mode, class UWorldPartitionStreamingPolicy* Policy, TArray<FString>* OutPackagesToGenerate = nullptr) { return false; }
	virtual bool PopulateGeneratedPackageForCook(UPackage* InPackage, const FString& InPackageRelativePath, const FString& InPackageCookName) { return false; }
	virtual void FinalizeGeneratedPackageForCook() {}
	virtual void FlushStreaming() {}
	virtual bool GenerateHLOD() { return false; }
	virtual bool GenerateNavigationData() { return false; }
	virtual FName GetActorRuntimeGrid(const AActor* Actor) const { return NAME_None; }
#endif

	// Streaming interface
	virtual int32 GetAllStreamingCells(TSet<const UWorldPartitionRuntimeCell*>& Cells, bool bIncludeDataLayers = false) const { return 0; }
	virtual int32 GetStreamingCells(const TArray<FWorldPartitionStreamingSource>& Sources, TSet<const UWorldPartitionRuntimeCell*>& Cells) const { return 0; };
	virtual void SortStreamingCellsByImportance(const TSet<const UWorldPartitionRuntimeCell*>& InCells, const TArray<FWorldPartitionStreamingSource>& InSources, TArray<const UWorldPartitionRuntimeCell*, TInlineAllocator<256>>& OutSortedCells) const {}

	/* Returns desired footprint that Draw2D should take relative to given Canvas size (the value can exceed the given size).
	 * UWorldPartitionSubSystem will re-adapt the size relative to all others UWorldPartitionRuntimeHash and provide the correct size to Draw2D.
	 *
	 * Return Draw2D's desired footprint.
	 */
	virtual FVector2D GetDraw2DDesiredFootprint(const FVector2D& CanvasSize) const { return FVector2D::ZeroVector; }

	virtual void Draw2D(class UCanvas* Canvas, const TArray<FWorldPartitionStreamingSource>& Sources, const FVector2D& PartitionCanvasOffset, const FVector2D& PartitionCanvasSize) const {}
	virtual void Draw3D(const TArray<FWorldPartitionStreamingSource>& Sources) const {}
};