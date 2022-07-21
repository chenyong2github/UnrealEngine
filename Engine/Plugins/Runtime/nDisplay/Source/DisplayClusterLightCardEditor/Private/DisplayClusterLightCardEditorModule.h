// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDisplayClusterLightCardEditor.h"
#include "UObject/WeakObjectPtr.h"

/**
 * Display Cluster editor module
 */
class FDisplayClusterLightCardEditorModule :
	public IDisplayClusterLightCardEditor
{
public:
	//~ IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	virtual void ShowLabels(const FLabelArgs& InArgs) override;
	
private:
	void RegisterTabSpawners();
	void UnregisterTabSpawners();

	void RegisterSettings();
	void UnregisterSettings();
};
