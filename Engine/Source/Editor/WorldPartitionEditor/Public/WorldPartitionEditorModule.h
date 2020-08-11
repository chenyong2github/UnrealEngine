// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * The module holding all of the UI related pieces for SubLevels management
 */
class FWorldPartitionEditorModule : public IModuleInterface
{
public:
	/**
	 * Called right after the module DLL has been loaded and the module object has been created
	 */
	virtual void StartupModule();

	/**
	 * Called before the module is unloaded, right before the module object is destroyed.
	 */
	virtual void ShutdownModule();
	
	/**
	 * Creates a world partition widget
	 */
	virtual TSharedRef<class SWidget> CreateWorldPartitionEditor();
	
private:
	FDelegateHandle LevelEditorExtenderDelegateHandle;

	TSharedPtr<class FHLODLayerAssetTypeActions> HLODLayerAssetTypeActions;
};
