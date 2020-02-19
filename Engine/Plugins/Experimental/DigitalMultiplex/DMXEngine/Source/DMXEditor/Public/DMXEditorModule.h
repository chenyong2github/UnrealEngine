// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "AssetTypeCategories.h"

class FDMXEditor;
class UDMXLibrary;
class IAssetTools;
class IAssetTypeActions;

class DMXEDITOR_API FDMXEditorModule
	: public IModuleInterface
	, public IHasMenuExtensibility			// Extender for adds or removes extenders for menu
	, public IHasToolBarExtensibility		// Extender for adds or removes extenders for toolbar
{
public:

	//~ Begin IModuleInterface implementation
	virtual void StartupModule() override;

	virtual void ShutdownModule() override;

	//~ End IModuleInterface implementation

	//~ Begin IHasMenuExtensibility implementation
	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() override { return MenuExtensibilityManager; }
	//~ End IHasMenuExtensibility implementation

	//~ Begin IHasToolBarExtensibility implementation
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() override { return ToolBarExtensibilityManager; }
	//~ End IHasToolBarExtensibility implementation

	/** Get the instance of this module. */
	static FDMXEditorModule& Get();

	/**
	 * Creates an instance of a DMX editor object.
	 *
	 * Note: This function should not be called directly. It should be called from AssetTools handler
	 *
	 * @param	Mode					Mode that this editor should operate in
	 * @param	InitToolkitHost			When Mode is WorldCentric, this is the level editor instance to spawn this editor within
	 * @param	DMXLibrary				The DMX object to start editing
	 *
	 * @return	Interface to the new DMX editor
	 */
	TSharedRef<FDMXEditor> CreateEditor( const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UDMXLibrary* DMXLibrary );

	static EAssetTypeCategories::Type GetAssetCategory() { return DMXEditorAssetCategory; }

	/**
	 * Exposes a way for other modules to add in their own DMX editor
	 * commands (appended to other DMX editor commands, when the editor is
	 * first opened).
	 */
	virtual const TSharedRef<FUICommandList> GetsSharedDMXEditorCommands() const { return SharedDMXEditorCommands.ToSharedRef(); }

public:
	/** DataTable Editor app identifier string */
	static const FName DMXEditorAppIdentifier;

	static const FName ModuleName;

private:
	void RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action);

private:
	void RegisterPropertyTypeCustomizations();
	void RegisterObjectCustomizations();

	/**
	 * Registers a custom class
	 *
	 * @param ClassName				The class name to register for property customization
	 * @param DetailLayoutDelegate	The delegate to call to get the custom detail layout instance
	 */
	void RegisterCustomClassLayout(FName ClassName, FOnGetDetailCustomizationInstance DetailLayoutDelegate);

	/**
	* Registers a custom struct
	*
	* @param StructName				The name of the struct to register for property customization
	* @param StructLayoutDelegate	The delegate to call to get the custom detail layout instance
	*/
	void RegisterCustomPropertyTypeLayout(FName PropertyTypeName, FOnGetPropertyTypeCustomizationInstance PropertyTypeLayoutDelegate);

private:
	//~ Gets the extensibility managers for outside entities to DMX editor's menus and toolbars
	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;

	/** All created asset type actions.  Cached here so that we can unregister it during shutdown. */
	TArray< TSharedPtr<IAssetTypeActions> > CreatedAssetTypeActions;

	/**
	 * A command list that can be passed around and isn't bound to an instance
	 * of the DMX editor.
	 */
	TSharedPtr<FUICommandList> SharedDMXEditorCommands;

	static EAssetTypeCategories::Type DMXEditorAssetCategory;

	/** List of registered class that we must unregister when the module shuts down */
	TSet< FName > RegisteredClassNames;
	TSet< FName > RegisteredPropertyTypes;
};