// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/WorldPartitionBuilder.h"
#include "WorldPartitionResaveActorsBuilder.generated.h"

// Example Command Line: ProjectName MapName -run=WorldPartitionBuilderCommandlet -SCCProvider=Perforce -Builder=WorldPartitionResaveActorsBuilder [-ActorClass=StaticMeshActor] [-SwitchActorPackagingSchemeToReduced]

UCLASS()
class UWorldPartitionResaveActorsBuilder : public UWorldPartitionBuilder
{
	GENERATED_UCLASS_BODY()

public:
	// UWorldPartitionBuilder interface begin
	virtual bool RequiresCommandletRendering() const override { return false; }
	virtual ELoadingMode GetLoadingMode() const override { return ELoadingMode::Custom; }
	virtual bool PreRun(UWorld* World, FPackageSourceControlHelper& PackageHelper) override;
	virtual bool RunInternal(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper) override;
	// UWorldPartitionBuilder interface end

private:
	UPROPERTY()
	FString ActorClassName;

	UPROPERTY()
	bool bReportOnly;

	UPROPERTY()
	bool bResaveDirtyActorDescsOnly;

	UPROPERTY()
	bool bSwitchActorPackagingSchemeToReduced;

	UPROPERTY()
	bool bEnableActorFolders;
};