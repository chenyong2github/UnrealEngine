// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Commandlets/WorldPartitionConvertCommandlet.h"
#include "WorldPartitionEditorSettings.generated.h"

UCLASS(config = Engine, defaultconfig, meta = (DisplayName = "World Partition"))
class UWorldPartitionEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UWorldPartitionEditorSettings();

	UPROPERTY(config, EditAnywhere, Category = MapConversion, meta = (ToolTip = "Commandlet class to use for World Parition conversion"))
	TSubclassOf<UWorldPartitionConvertCommandlet> CommandletClass;

	UPROPERTY(config, EditAnywhere, Category = WorldPartition, meta = (ToolTip = "Automatically load all cells when the world is smaller than this value"))
	float AutoCellLoadingMaxWorldSize = 100000.0f;
};