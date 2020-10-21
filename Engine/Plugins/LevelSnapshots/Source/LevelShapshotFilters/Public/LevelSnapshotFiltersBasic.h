// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LevelSnapshotFiltersBasic.generated.h"

// If you are building your filter in Blueprints then you should inherit this class
UCLASS()
class LEVELSNAPSHOTFILTERS_API ULevelSnapshotFiltersBasic : public ULevelSnapshotFilter
{
	GENERATED_BODY()
};

UCLASS()
class LEVELSNAPSHOTFILTERS_API ULevelSnapshotFilterTag : public ULevelSnapshotFiltersBasic
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Default)
	TArray<FString> Tags;
};

UCLASS()
class LEVELSNAPSHOTFILTERS_API ULevelSnapshotFilterCustom : public ULevelSnapshotFilter
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Default)
	FString Custom;
};