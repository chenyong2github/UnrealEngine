// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * The module holding all of the UI related pieces for WorldPartition
 */
class IWorldPartitionEditorModule : public IModuleInterface
{
public:
	virtual ~IWorldPartitionEditorModule() {}

	virtual bool IsWorldPartitionEnabled() const = 0;
	virtual bool IsConversionPromptEnabled() const = 0;
	virtual void SetConversionPromptEnabled(bool bEnabled) = 0;
	virtual float GetAutoCellLoadingMaxWorldSize() const = 0;
	virtual bool ConvertMap(const FString& InLongPackageName) = 0;

	/** Triggered when a world is added. */
	DECLARE_EVENT_OneParam(IWorldPartitionEditorModule, FWorldPartitionCreated, UWorld*);

	/** Return the world added event. */
	virtual FWorldPartitionCreated& OnWorldPartitionCreated() = 0;
};