// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeCategories.h"
#include "CustomizableObjectEditorClasses.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/IToolkit.h"
#include "MuCOE/ICustomizableObjectEditor.h"
#include "MuCOE/ICustomizableObjectDebugger.h"
#include "MuCOE/CustomizableObjectEditorLogger.h"

extern const FName CustomizableObjectEditorAppIdentifier;
extern const FName CustomizableObjectInstanceEditorAppIdentifier;
extern const FName CustomizableObjectPopulationEditorAppIdentifier;
extern const FName CustomizableObjectPopulationClassEditorAppIdentifier;
extern const FName CustomizableObjectDebuggerAppIdentifier;

/**
 * Customizable object editor module interface
 */
class ICustomizableObjectEditorModule : public IModuleInterface
{
public:
	static ICustomizableObjectEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked< ICustomizableObjectEditorModule >("CustomizableObjectEditor");
	}

	/**
	 * Creates a new customizable object editor.
	 */
	virtual TSharedRef<ICustomizableObjectEditor> CreateCustomizableObjectEditor( const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UCustomizableObject* CustomizableObject ) = 0;
	virtual TSharedRef<ICustomizableObjectInstanceEditor> CreateCustomizableObjectInstanceEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UCustomizableObjectInstance* CustomizableObjectInstance) = 0;
	virtual TSharedRef<ICustomizableObjectDebugger> CreateCustomizableObjectDebugger(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UCustomizableObject* CustomizableObjectInstance) = 0;
	
	virtual TSharedPtr<class FExtensibilityManager> GetCustomizableObjectEditorToolBarExtensibilityManager() { return nullptr; }
	virtual TSharedPtr<class FExtensibilityManager> GetCustomizableObjectEditorMenuExtensibilityManager() { return nullptr; }
	
	/** Get the Customizable Object custom asset category. */
   	virtual EAssetTypeCategories::Type GetAssetCategory() const = 0;

	/** Returns the module logger. */
	virtual FCustomizableObjectEditorLogger& GetLogger() = 0;
};
 