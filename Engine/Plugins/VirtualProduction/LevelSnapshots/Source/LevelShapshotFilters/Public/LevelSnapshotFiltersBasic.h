// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LevelSnapshotFilters.h"

#include "LevelSnapshotFiltersBasic.generated.h"

// TODO: These are all test filters. They MUST be removed before release of this plugin.

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

// Test visibility in menu

UCLASS(meta = (CommonSnapshotFilter))
class LEVELSNAPSHOTFILTERS_API UCommonTestFilter : public ULevelSnapshotFilter
{
	GENERATED_BODY()
};

UCLASS(meta = (InternalSnapshotFilter))
class LEVELSNAPSHOTFILTERS_API UDoNotShowThisFilterEver : public ULevelSnapshotFilter
{
	GENERATED_BODY()
};

UCLASS()
class LEVELSNAPSHOTFILTERS_API UFakeBlueprintTestFilter : public ULevelSnapshotBlueprintFilter // Make this "seem" like a blueprint filter
{
	GENERATED_BODY()
};