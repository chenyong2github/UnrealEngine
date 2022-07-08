// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaSourceManagerEditorModule.h"

#include "AssetToolsModule.h"
#include "AssetTools/MediaSourceManagerActions.h"
#include "MediaSourceManagerEditorStyle.h"
#include "Modules/ModuleManager.h"
#include "UObject/NameTypes.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SMediaSourceManager.h"
#include "Widgets/SMediaSourceManagerPreviews.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

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
		RegisterTabSpawners();
	}

	virtual void ShutdownModule() override
	{
		UnregisterTabSpawners();
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

	/**
	 * Register functions to spawn tabs.
	 */
	void RegisterTabSpawners()
	{
		// Add Media group.
		const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
		TSharedRef<FWorkspaceItem> MediaGroup = MenuStructure.GetLevelEditorCategory()->AddGroup(
			LOCTEXT("WorkspaceMenu_MediaCategory", "Media"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "SequenceRecorder.TabIcon"),
			true);
		TSharedRef<FWorkspaceItem> MediaSourceManagerGroup = MediaGroup->AddGroup(
			LOCTEXT("WorkspaceMenu_MediaSourceManagerCategory", "Media Source Manager"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "SequenceRecorder.TabIcon"),
			true);

		// Add media source manager tab.
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ManagerTabName,
			FOnSpawnTab::CreateStatic(&FMediaSourceManagerEditorModule::SpawnMediaSourceManagerTab))
			.SetGroup(MediaSourceManagerGroup)
			.SetDisplayName(LOCTEXT("ManagerTabTitle", "Media Source Manager"))
			.SetTooltipText(LOCTEXT("ManagerTooltipText", "Open the Media Source Manager tab."))
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "SequenceRecorder.TabIcon"));

		// Add preview tab.
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PreviewTabName,
			FOnSpawnTab::CreateStatic(&FMediaSourceManagerEditorModule::SpawnMediaSourceManagerPreviewTab))
			.SetGroup(MediaSourceManagerGroup)
			.SetDisplayName(LOCTEXT("PreviewTabTitle", "Media Source Manager Preview"))
			.SetTooltipText(LOCTEXT("PreviewTooltipText", "Open the Media Source Manager Preview tab."))
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));
	}

	/**
	 * Remove functions that spawned tabs.
	 */
	void UnregisterTabSpawners()
	{
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PreviewTabName);
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ManagerTabName);
		}
	}

	/**
	 * Callback to spawn the manager tab.
	 */
	static TSharedRef<SDockTab> SpawnMediaSourceManagerTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SMediaSourceManager)
			];
	}

	/**
	 * Callback to spawn the preview tab.
	 */
	static TSharedRef<SDockTab> SpawnMediaSourceManagerPreviewTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SMediaSourceManagerPreviews)
			];
	}

	/** Holds the plug-ins style set. */
	TSharedPtr<ISlateStyle> Style;
	/** The collection of registered asset type actions. */
	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;
	/** Names for tabs. */
	static FLazyName ManagerTabName;
	static FLazyName PreviewTabName;
};

FLazyName FMediaSourceManagerEditorModule::ManagerTabName(TEXT("MediaSourceManager"));
FLazyName FMediaSourceManagerEditorModule::PreviewTabName(TEXT("MediaSourceManagerPreview"));

IMPLEMENT_MODULE(FMediaSourceManagerEditorModule, MediaSourceManagerEditor);

#undef LOCTEXT_NAMESPACE
