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
	virtual bool RunInternal(UWorld* World, const FBox& Bounds, FPackageSourceControlHelper& PackageHelper) PURE_VIRTUAL(UWorldPartition::RunInternal, return false;);

	void DoCollectGarbage() const;

	int32 IterativeCellSize = 102400;
	int32 IterativeCellOverlapSize = 0;
	TSet<FName> DataLayerLabels;
	bool bLoadNonDynamicDataLayers = true;
	bool bLoadInitiallyActiveDataLayers = true;
};