// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PackageSourceControlHelper.h"
#include "WorldPartitionBuilder.generated.h"

UCLASS(Abstract, Config=Engine)
class UWorldPartitionBuilder : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	virtual bool RequiresCommandletRendering() const PURE_VIRTUAL(UWorldPartitionBuilder::RequiresCommandletRendering, return false;);
	virtual bool RequiresEntireWorldLoading() const PURE_VIRTUAL(UWorldPartitionBuilder::RequiresEntireWorldLoading, return false;);
	virtual bool Run(UWorld* World, FPackageSourceControlHelper& PackageHelper) PURE_VIRTUAL(UWorldPartitionBuilder::Run, return false;);
};