// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "WorldPartition/DataLayer/IDataLayerEditorModule.h"

class UDataLayer;

/**
 * The module holding all of the UI related pieces for DataLayers
 */
class FDataLayerEditorModule : public IDataLayerEditorModule
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
	 * Creates a DataLayer Browser widget
	 */
	virtual TSharedRef<class SWidget> CreateDataLayerBrowser();

	/*
	 * Selected DataLayer in DataLayer Browser widget
	 */
	virtual void SyncDataLayerBrowserToDataLayer(const UDataLayer* DataLayer);

	/** Delegates to be called to extend the DataLayers menus */
	DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<FExtender>, FDataLayersMenuExtender, const TSharedRef<FUICommandList>);
	virtual TArray<FDataLayersMenuExtender>& GetAllDataLayersMenuExtenders() {return DataLayersMenuExtenders;}

private:

	TWeakPtr<SWidget> DataLayerBrowser;

	/** All extender delegates for the DataLayers menus */
	TArray<FDataLayersMenuExtender> DataLayersMenuExtenders;
};


