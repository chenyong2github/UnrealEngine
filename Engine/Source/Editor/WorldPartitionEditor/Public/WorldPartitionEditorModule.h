// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WorldPartition/IWorldPartitionEditorModule.h"

/**
 * The module holding all of the UI related pieces for SubLevels management
 */
class FWorldPartitionEditorModule : public IWorldPartitionEditorModule
{
public:
	/**
	 * Called right after the module DLL has been loaded and the module object has been created
	 */
	virtual void StartupModule() override;

	/**
	 * Called before the module is unloaded, right before the module object is destroyed.
	 */
	virtual void ShutdownModule() override;
	
	/**
	 * Creates a world partition widget
	 */
	virtual TSharedRef<class SWidget> CreateWorldPartitionEditor();

	/**
	 *
	 */
	virtual bool IsWorldPartitionEnabled() const override;

	/**
	 *
	 */
	virtual bool IsConversionPromptEnabled() const override;

	/**
	 *
	 */
	virtual void SetConversionPromptEnabled(bool bEnabled) override;

	/**
	 *
	 */
	virtual bool GetEnableLoadingOfLastLoadedCells() const override;

	/**
	 *
	 */
	virtual float GetAutoCellLoadingMaxWorldSize() const override;

	/**
	 * 
	 */
	virtual bool ConvertMap(const FString& InLongPackageName) override;

	/**
	 *
	 */
	virtual FWorldPartitionCreated& OnWorldPartitionCreated() override { return WorldPartitionCreatedEvent; }

private:
	FDelegateHandle LevelEditorExtenderDelegateHandle;

	TSharedPtr<class FHLODLayerAssetTypeActions> HLODLayerAssetTypeActions;

	FWorldPartitionCreated WorldPartitionCreatedEvent;
};
