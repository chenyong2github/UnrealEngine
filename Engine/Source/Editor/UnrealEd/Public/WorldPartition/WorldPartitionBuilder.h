// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PackageSourceControlHelper.h"
#include "WorldPartitionBuilder.generated.h"

/**
 * Structure containing information about a World Partition Builder cell
 */
USTRUCT()
struct FCellInfo
{
	GENERATED_BODY()

	FCellInfo();

	/**
	 * Location of the cell, expressed inside World Partition Builder space
	 * (floor(Coordinate) / IterativeCellSize)
	 */
	UPROPERTY(VisibleAnywhere, Category = WorldPartitionBuilder)
	FIntVector Location;

	/** Bounds of the cell */
	UPROPERTY(VisibleAnywhere, Category = WorldPartitionBuilder)
	FBox Bounds;

	/** Whole space */
	UPROPERTY(VisibleAnywhere, Category = WorldPartitionBuilder)
	FBox EditorBounds;

	/** The size of a cell used by the World Partition Builder */
	UPROPERTY(VisibleAnywhere, Category = WorldPartitionBuilder)
	int32 IterativeCellSize;
};

UCLASS(Abstract, Config=Engine)
class UNREALED_API UWorldPartitionBuilder : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	enum ELoadingMode
	{
		Custom,
		EntireWorld,
		IterativeCells,
		IterativeCells2D,
	};

	bool RunBuilder(UWorld* World);

	virtual bool RequiresCommandletRendering() const PURE_VIRTUAL(UWorldPartitionBuilder::RequiresCommandletRendering, return false;);
	virtual ELoadingMode GetLoadingMode() const PURE_VIRTUAL(UWorldPartitionBuilder::GetLoadingMode, return ELoadingMode::Custom;);
	
	bool Run(UWorld* World, FPackageSourceControlHelper& PackageHelper);

	virtual bool PreWorldInitialization(FPackageSourceControlHelper& PackageHelper) { return true; }

protected:
	/**
	 * Overridable method for derived classes to perform operations when world builder process starts.
	 * This is called before loading data (e.g. data layers, editor cells) and before calling `RunInternal`.
	 */
	virtual bool PreRun(UWorld* World, FPackageSourceControlHelper& PackageHelper) { return true; }

	virtual bool RunInternal(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper) PURE_VIRTUAL(UWorldPartition::RunInternal, return false;);

	/**
	 * Overridable method for derived classes to perform operations when world builder process completes.
	 * This is called after loading all data (e.g. data layers, editor cells) and after calling `RunInternal` for all editor cells.
	 */
	virtual bool PostRun(UWorld* World, FPackageSourceControlHelper& PackageHelper, const bool bInRunSuccess) { return true; }

	/**
	 * Overridable method for derived classes to perform operations when world builder has unloaded the world.
	 */
	virtual bool PostWorldTeardown(FPackageSourceControlHelper& PackageHelper) { return true; }

	int32 IterativeCellSize = 102400;
	int32 IterativeCellOverlapSize = 0;
	TSet<FName> DataLayerLabels;
	TSet<FName> ExcludedDataLayerLabels;
	bool bLoadNonDynamicDataLayers = true;
	bool bLoadInitiallyActiveDataLayers = true;

	bool bSubmit = true;
};