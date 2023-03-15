// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "Templates/SubclassOf.h"
#include "PackageSourceControlHelper.h"
#include "WorldPartitionBuilderCommandlet.generated.h"

class UWorldPartitionBuilder;

UCLASS(Config=Engine)
class UWorldPartitionBuilderCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

private:
	bool RunBuilder(TSubclassOf<UWorldPartitionBuilder> InBuilderClass, const FString& InWorldPackageName);
};