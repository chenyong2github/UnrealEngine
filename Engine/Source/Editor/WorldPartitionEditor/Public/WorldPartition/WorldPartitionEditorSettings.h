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

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

	UPROPERTY(config, EditAnywhere, Category = WorldPartition, meta = (ToolTip = "This option is used to enable World Partition support"))
	bool bEnableWorldPartition = false;

	UPROPERTY(config, EditAnywhere, Category = MapConversion, meta = (ToolTip = "This option when enabled will show a conversion prompt when opening non World Partition maps"))
	bool bEnableConversionPrompt = false;

	UPROPERTY(config, EditAnywhere, Category = MapConversion, meta = (ToolTip = "Commandlet class to use for World Parition conversion"))
	TSubclassOf<UWorldPartitionConvertCommandlet> CommandletClass;

	UPROPERTY(config, EditAnywhere, Category = WorldPartition, meta = (ToolTip = "Automatically load all cells when the world is smaller than this value"))
	float AutoCellLoadingMaxWorldSize = 100000.0f;
};