// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "Commandlets/CommandletPackageHelper.h"
#include "WorldPartitionBuildHLODsCommandlet.generated.h"

UCLASS()
class UWorldPartitionBuildHLODsCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

private:
	FCommandletPackageHelper PackageHelper;
};