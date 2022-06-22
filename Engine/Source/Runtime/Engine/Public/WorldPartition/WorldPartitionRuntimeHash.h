// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "WorldPartition.h"
#include "WorldPartitionActorDescView.h"
#include "WorldPartition/ActorDescList.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionActorDescViewProxy.h"
#if WITH_EDITOR
#include "CookPackageSplitter.h"
#include "Misc/HierarchicalLogArchive.h"
#endif
#include "WorldPartitionRuntimeHash.generated.h"

class FActorClusterContext;
struct FHierarchicalLogArchive;

UENUM()
enum class EWorldPartitionStreamingPerformance : uint8
{
	Good,
	Slow,
	Critical
};

UCLASS(Abstract, Config=Engine, AutoExpandCategories=(WorldPartition), Within = WorldPartition)
class ENGINE_API UWorldPartitionRuntimeHash : public UObject
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
	virtual void SetDefaultValues() {}
	virtual bool PopulateGeneratorPackageForCook(const TArray<ICookPackageSplitter::FGeneratedPackageForPreSave>& InGeneratedPackages, TArray<UPackage*>& OutModifiedPackages) { return false; }
	virtual bool PopulateGeneratedPackageForCook(UPackage* InPackage, const FString& InPackageRelativePath, TArray<UPackage*>& OutModifiedPackages) { return false; }
	virtual bool GenerateStreaming(class UWorldPartitionStreamingPolicy* StreamingPolicy, const FActorClusterContext& ActorClusterContext, TArray<FString>* OutPackagesToGenerate) { return false; }
	virtual void FlushStreaming() {}
	virtual bool GenerateHLOD(ISourceControlHelper* SourceControlHelper, FActorClusterContext& ActorClusterContext, bool bCreateActorsOnly) { return false; }
	virtual void DrawPreview() const {}

	virtual void DumpStateLog(FHierarchicalLogArchive& Ar);

	// PIE/Game methods
	void OnBeginPlay();
	void OnEndPlay();
#endif

	class FStreamingSourceCells
	{
	public:
		void AddCell(const UWorldPartitionRuntimeCell* Cell, const UWorldPartitionRuntimeCell::FStreamingSourceInfo& Info);
		void Reset() { Cells.Reset(); }
		int32 Num() const { return Cells.Num(); }
		TSet<const UWorldPartitionRuntimeCell*>& GetCells() { return Cells; }

	private:
		TSet<const UWorldPartitionRuntimeCell*> Cells;
	};

	// Streaming interface
	virtual int32 GetAllStreamingCells(TSet<const UWorldPartitionRuntimeCell*>& Cells, bool bAllDataLayers = false, bool bDataLayersOnly = false, const TSet<FName>& InDataLayers = TSet<FName>()) const { return 0; }
	virtual bool GetStreamingCells(const FWorldPartitionStreamingQuerySource& QuerySource, TSet<const UWorldPartitionRuntimeCell*>& OutCells) const { return false; }
	virtual bool GetStreamingCells(const TArray<FWorldPartitionStreamingSource>& Sources, UWorldPartitionRuntimeHash::FStreamingSourceCells& OutActivateCells, UWorldPartitionRuntimeHash::FStreamingSourceCells& OutLoadCells) const { return false; };
	virtual void SortStreamingCellsByImportance(const TSet<const UWorldPartitionRuntimeCell*>& InCells, const TArray<FWorldPartitionStreamingSource>& InSources, TArray<const UWorldPartitionRuntimeCell*, TInlineAllocator<256>>& OutSortedCells) const;
	EWorldPartitionStreamingPerformance GetStreamingPerformance(const TSet<const UWorldPartitionRuntimeCell*>& CellToActivate) const;

	/* Returns desired footprint that Draw2D should take relative to given Canvas size (the value can exceed the given size).
	 * UWorldPartitionSubSystem will re-adapt the size relative to all others UWorldPartitionRuntimeHash and provide the correct size to Draw2D.
	 *
	 * Return Draw2D's desired footprint.
	 */
	virtual FVector2D GetDraw2DDesiredFootprint(const FVector2D& CanvasSize) const { return FVector2D::ZeroVector; }

	virtual void Draw2D(class UCanvas* Canvas, const TArray<FWorldPartitionStreamingSource>& Sources, const FVector2D& PartitionCanvasSize, const FVector2D& Offset) const {}
	virtual void Draw3D(const TArray<FWorldPartitionStreamingSource>& Sources) const {}
	virtual bool ContainsRuntimeHash(const FString& Name) const { return false; }

protected:
	virtual EWorldPartitionStreamingPerformance GetStreamingPerformanceForCell(const UWorldPartitionRuntimeCell* Cell) const { return EWorldPartitionStreamingPerformance::Good; }

private:
#if WITH_EDITOR
	void ForceExternalActorLevelReference(bool bForceExternalActorLevelReferenceForPIE);
#endif

protected:
#if WITH_EDITORONLY_DATA
	struct FAlwaysLoadedActorForPIE
	{
		FAlwaysLoadedActorForPIE(const FWorldPartitionReference& InReference, AActor* InActor)
			: Reference(InReference), Actor(InActor)
		{}

		FWorldPartitionReference Reference;
		AActor* Actor;
	};

	TArray<FAlwaysLoadedActorForPIE> AlwaysLoadedActorsForPIE;

public:
	mutable FActorDescList ModifiedActorDescListForPIE;
#endif
};