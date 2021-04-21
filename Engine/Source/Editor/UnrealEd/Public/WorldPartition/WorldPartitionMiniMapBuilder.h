// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/WorldPartitionBuilder.h"
#include "WorldPartitionMiniMapBuilder.generated.h"

UCLASS(config = Engine, defaultconfig)
class UWorldPartitionMiniMapBuilder : public UWorldPartitionBuilder
{
	GENERATED_UCLASS_BODY()

public:
	// UWorldPartitionBuilder interface begin
	virtual bool RequiresCommandletRendering() const override { return true; }
	virtual bool RequiresEntireWorldLoading() const override { return !bUseOnlyHLODs; }
	virtual bool Run(UWorld* World, FPackageSourceControlHelper& PackageHelper) override;
	// UWorldPartitionBuilder interface end

	UPROPERTY(config)
	int32 MiniMapSize = 4096;

private:
	bool bUseOnlyHLODs;
	bool bAutoSubmit;
};