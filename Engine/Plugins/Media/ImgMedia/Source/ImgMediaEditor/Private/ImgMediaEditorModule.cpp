// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaEditorModule.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "AssetToolsModule.h"
#include "AssetTools/ImgMediaSourceActions.h"
#include "Customizations/ImgMediaSourceCustomization.h"
#include "Customizations/ImgMediaSourceCustomizationImportInfo.h"
#include "IAssetTools.h"
#include "ImgMediaSource.h"
#include "PropertyEditorModule.h"
#include "UObject/NameTypes.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SImgMediaCache.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "ImgMediaEditorModule"

DEFINE_LOG_CATEGORY(LogImgMediaEditor);

static const FName ImgMediaCacheTabName(TEXT("ImgMediaCache"));

/**
 * Implements the ImgMediaEditor module.
 */
class FImgMediaEditorModule
	: public IModuleInterface
{
public:

	//~ IModuleInterface interface

	virtual void StartupModule() override
	{
		RegisterCustomizations();
		RegisterAssetTools();
		RegisterTabSpawners();
	}

	virtual void ShutdownModule() override
	{
		UnregisterTabSpawners();
		UnregisterAssetTools();
		UnregisterCustomizations();
	}

protected:

	/** Register details view customizations. */
	void RegisterCustomizations()
	{
		CustomizedStructName = FImgMediaSourceCustomizationSequenceProxy::StaticStruct()->GetFName();
		ImportInfoStructName = FImgMediaSourceImportInfo::StaticStruct()->GetFName();

		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		{
#if WITH_EDITORONLY_DATA
			PropertyModule.RegisterCustomPropertyTypeLayout(CustomizedStructName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FImgMediaSourceCustomization::MakeInstance));
			PropertyModule.RegisterCustomPropertyTypeLayout(ImportInfoStructName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FImgMediaSourceCustomizationImportInfo::MakeInstance));
#endif // WITH_EDITORONLY_DATA
		}
	}

	/** Unregister details view customizations. */
	void UnregisterCustomizations()
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		{
#if WITH_EDITORONLY_DATA
			PropertyModule.UnregisterCustomPropertyTypeLayout(ImportInfoStructName);
			PropertyModule.UnregisterCustomPropertyTypeLayout(CustomizedStructName);
#endif // WITH_EDITORONLY_DATA
		}
	}

	void RegisterAssetTools()
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		TSharedRef<IAssetTypeActions> Action = MakeShared<FImgMediaSourceActions>();
		AssetTools.RegisterAssetTypeActions(Action);
		RegisteredAssetTypeActions.Add(Action);
	}

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

	void RegisterTabSpawners()
	{
		// Add ImgMedia group.
		const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
		TSharedRef<FWorkspaceItem> MediaBrowserGroup = MenuStructure.GetLevelEditorCategory()->AddGroup(
			LOCTEXT("WorkspaceMenu_ImgMediaCategory", "ImgMedia"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SequenceRecorder.TabIcon"),
			true);

		// Add cache tab.
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ImgMediaCacheTabName,
			FOnSpawnTab::CreateStatic(&FImgMediaEditorModule::SpawnCacheTab))
			.SetGroup(MediaBrowserGroup)
			.SetDisplayName(LOCTEXT("ImgMediaCacheTabTitle", "Cache"))
			.SetTooltipText(LOCTEXT("ImgMediaCacheTooltipText", "Open the cache tab."))
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "SequenceRecorder.TabIcon"));
	}

	void UnregisterTabSpawners()
	{
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ImgMediaCacheTabName);
		}
	}

	static TSharedRef<SDockTab> SpawnCacheTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SImgMediaCache)
			];
	}

private:

	/** Customization name to avoid reusing staticstruct during shutdown. */
	FName CustomizedStructName;
	FName ImportInfoStructName;

	/** The collection of registered asset type actions. */
	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;
};


IMPLEMENT_MODULE(FImgMediaEditorModule, ImgMediaEditor);

#undef LOCTEXT_NAMESPACE
