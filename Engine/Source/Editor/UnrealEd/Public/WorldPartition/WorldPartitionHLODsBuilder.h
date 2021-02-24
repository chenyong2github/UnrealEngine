// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/WorldPartitionBuilder.h"
#include "WorldPartitionHLODsBuilder.generated.h"

UCLASS()
class UWorldPartitionHLODsBuilder : public UWorldPartitionBuilder
{
	GENERATED_UCLASS_BODY()

public:
	// UWorldPartitionBuilder interface begin
	virtual bool RequiresCommandletRendering() const override;
	virtual bool RequiresEntireWorldLoading() const override { return false; }
	virtual bool Run(UWorld* World, FPackageSourceControlHelper& PackageHelper) override;
	// UWorldPartitionBuilder interface end

protected:
	bool SetupHLODActors(bool bCreateOnly);
	bool BuildHLODActors();
	bool DeleteHLODActors();

private:
	class UWorldPartition* WorldPartition;
	class FSourceControlHelper* SourceControlHelper;

	bool bSetupHLODs;
	bool bBuildHLODs;
	bool bDeleteHLODs;
	bool bForceGC;
};