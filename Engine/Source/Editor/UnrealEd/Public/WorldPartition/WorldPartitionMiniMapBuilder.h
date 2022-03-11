// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "WorldPartition/WorldPartitionBuilder.h"
#include "WorldPartition/WorldPartitionMiniMap.h"

#include "WorldPartitionMiniMapBuilder.generated.h"


/** Structure holding Minimap's tile resources */
struct FMinimapTile
{
	/** Tile's texture */
	TStrongObjectPtr<UTexture2D> Texture;

	/** Coordinates of the tile in the Minimap's Virtual Texture */
	FIntVector2 Coordinates;
};

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
	TArray<FMinimapTile> MiniMapTiles;
	TObjectPtr<AWorldPartitionMiniMap> WorldMiniMap;

	FBox EditorBounds;
	bool bAutoSubmit;
};