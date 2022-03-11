// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SubclassOf.h"

class UWorldPartitionBuilder;

/**
 * The module holding all of the UI related pieces for WorldPartition
 */
class IWorldPartitionEditorModule : public IModuleInterface
{
public:
	virtual ~IWorldPartitionEditorModule() {}

	virtual bool ConvertMap(const FString& InLongPackageName) = 0;

	virtual bool RunBuilder(TSubclassOf<UWorldPartitionBuilder> BuilderClass, const FString& InLongPackageName) = 0;

	virtual int32 GetPlacementGridSize() const = 0;
	virtual int32 GetInstancedFoliageGridSize() const = 0;

	/** Triggered when a world is added. */
	DECLARE_EVENT_OneParam(IWorldPartitionEditorModule, FWorldPartitionCreated, UWorld*);

	/** Return the world added event. */
	virtual FWorldPartitionCreated& OnWorldPartitionCreated() = 0;
};