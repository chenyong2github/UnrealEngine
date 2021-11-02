// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PackageSourceControlHelper.h"
#include "WorldPartitionBuilder.generated.h"

UCLASS(Abstract, Config=Engine)
class UNREALED_API UWorldPartitionBuilder : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	enum ELoadingMode
	{
		Custom,
		EntireWorld,
		IterativeCells
	};

	static bool RunBuilder(UWorldPartitionBuilder* BuilderClass, UWorld* World);
	static bool RunBuilder(TSubclassOf<UWorldPartitionBuilder> BuilderClass, UWorld* World);

	virtual bool RequiresCommandletRendering() const PURE_VIRTUAL(UWorldPartitionBuilder::RequiresCommandletRendering, return false;);
	virtual ELoadingMode GetLoadingMode() const PURE_VIRTUAL(UWorldPartitionBuilder::GetLoadingMode, return ELoadingMode::Custom;);
	
	bool Run(UWorld* World, FPackageSourceControlHelper& PackageHelper);

	virtual bool PreWorldInitialization(FPackageSourceControlHelper& PackageHelper) { return true; }

protected:

	/**
	 * Overridable method for derived classed to perform operations when partition building process starts.
	 * This is called before loading data (e.g. data layers, editor cells) and before calling `RunInternal`.
	 */
	virtual void OnPartitionBuildStarted(const UWorld* World, FPackageSourceControlHelper& PackageHelper) {}

	virtual bool RunInternal(UWorld* World, const FBox& Bounds, FPackageSourceControlHelper& PackageHelper) PURE_VIRTUAL(UWorldPartition::RunInternal, return false;);

	/**
	 * Overridable method for derived classed to perform operations when partition building process completes.
	 * This is called after loading all data (e.g. data layers, editor cells) and after calling `RunInternal` for all editor cells.
	 */
	virtual void OnPartitionBuildCompleted(const UWorld* World, FPackageSourceControlHelper& PackageHelper) {}

	int32 IterativeCellSize = 102400;
	int32 IterativeCellOverlapSize = 0;
	TSet<FName> DataLayerLabels;
	bool bLoadNonDynamicDataLayers = true;
	bool bLoadInitiallyActiveDataLayers = true;

	bool bSubmit = true;
};