// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "PackageSourceControlHelper.h"
#include "WorldPartitionBuildNavigationDataCommandlet.generated.h"

/**
* Commandlet to build navigation data for a partioned level 
*/
UCLASS()
class UWorldPartitionBuildNavigationDataCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

private:
	FPackageSourceControlHelper PackageHelper;
};
