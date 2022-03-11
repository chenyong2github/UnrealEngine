// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/WorldPartitionBuilder.h"
#include "WorldPartition/WorldPartitionMiniMap.h"

#include "WorldPartitionMiniMapBuilder.generated.h"

UCLASS(config = Engine, defaultconfig)
class UNREALED_API UWorldPartitionMiniMapBuilder : public UWorldPartitionBuilder
{
	GENERATED_UCLASS_BODY()

public:
	// UWorldPartitionBuilder interface begin
	virtual bool RequiresCommandletRendering() const override { return true; }
	virtual ELoadingMode GetLoadingMode() const override { return ELoadingMode::IterativeCells2D; }

protected:
	virtual bool PreRun(UWorld* World, FPackageSourceControlHelper& PackageHelper) override;
	virtual bool RunInternal(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper) override;
	virtual bool PostRun(UWorld* World, FPackageSourceControlHelper& PackageHelper, const bool bInRunSuccess) override;

	// UWorldPartitionBuilder interface end

private:
	/*MiniMap Texture Tiles for displaying on world partition window*/
	UPROPERTY(Transient, VisibleAnywhere, Category = WorldPartitionMiniMap)
	TArray<FMinimapTile> MiniMapTiles;

	UPROPERTY(Transient, VisibleAnywhere, Category = WorldPartitionMiniMap)
	TObjectPtr<AWorldPartitionMiniMap> WorldMiniMap = nullptr;
	FBox EditorBounds;
	bool bAutoSubmit;
};