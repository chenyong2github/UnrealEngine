// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaSourceManagerEditorModule.h"

#include "AssetToolsModule.h"
#include "AssetTools/MediaSourceManagerActions.h"
#include "MediaSourceManagerEditorStyle.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "MediaSourceManagerEditorModule"

DEFINE_LOG_CATEGORY(LogMediaSourceManagerEditor);

/**
 * Implements the MediaSourceManagerEditor module.
 */
class FMediaSourceManagerEditorModule
	: public IMediaSourceManagerEditorModule
{
public:
	//~ IMediaSourceManagerEditorModule interface
	TSharedPtr<ISlateStyle> GetStyle() { return Style; }

	//~ IModuleInterface interface

	virtual void StartupModule() override
	{
		Style = MakeShareable(new FMediaSourceManagerEditorStyle());

		RegisterAssetTools();
	}

	virtual void ShutdownModule() override
	{
		UnregisterAssetTools();
	}

protected:

	/**
	 * Register all our asset tools.
	 */
	void RegisterAssetTools()
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		RegisterAssetTypeAction(AssetTools, MakeShareable(new FMediaSourceManagerActions(Style.ToSharedRef())));
	}

	/**
	 * Registers a single asset type action.
	 *
	 * @param AssetTools	The asset tools object to register with.
	 * @param Action		The asset type action to register.
	 */
	void RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action)
	{
		AssetTools.RegisterAssetTypeActions(Action);
		RegisteredAssetTypeActions.Add(Action);
	}

	/**
	 * Unregister all our asset tools.
	 */
	void UnregisterAssetTools()
	{
		FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");

		if (AssetToolsModule != nullptr)
		{
			IAssetTools& AssetTools = AssetToolsModule->Get();
			for (const TSharedRef<IAssetTypeActions>& Action : RegisteredAssetTypeActions)
			{
				AssetTools.UnregisterAssetTypeActions(Action);
			}
		}
	}

	/** Holds the plug-ins style set. */
	TSharedPtr<ISlateStyle> Style;
	/** The collection of registered asset type actions. */
	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;
};

IMPLEMENT_MODULE(FMediaSourceManagerEditorModule, MediaSourceManagerEditor);

#undef LOCTEXT_NAMESPACE
