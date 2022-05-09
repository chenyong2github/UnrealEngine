// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaEditorModule.h"
#include "Modules/ModuleManager.h"

#include "AssetToolsModule.h"
#include "AssetTools/ImgMediaSourceActions.h"
#include "Customizations/ImgMediaSourceCustomization.h"
#include "IAssetTools.h"
#include "ImgMediaEditorModule.h"
#include "IImgMediaModule.h"
#include "ImgMediaSource.h"
#include "PropertyEditorModule.h"
#include "UObject/NameTypes.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SImgMediaBandwidth.h"
#include "Widgets/SImgMediaCache.h"
#include "Widgets/SImgMediaProcessImages.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ImgMediaEditorModule"

DEFINE_LOG_CATEGORY(LogImgMediaEditor);

static const FName ImgMediaBandwidthTabName(TEXT("ImgMediaBandwidth"));
static const FName ImgMediaCacheTabName(TEXT("ImgMediaCache"));

/**
 * Implements the ImgMediaEditor module.
 */
class FImgMediaEditorModule
	: public IImgMediaEditorModule
{
public:
	//~ IImgMediaEditorModule interface
	const TArray<TWeakPtr<FImgMediaPlayer>>& GetMediaPlayers()
	{
		return MediaPlayers;
	}

	//~ IModuleInterface interface

	virtual void StartupModule() override
	{
		RegisterCustomizations();
		RegisterAssetTools();
		RegisterTabSpawners();

		IImgMediaModule* ImgMediaModule = FModuleManager::LoadModulePtr<IImgMediaModule>("ImgMedia");
		if (ImgMediaModule != nullptr)
		{
			ImgMediaModule->OnImgMediaPlayerCreated.AddRaw(this, &FImgMediaEditorModule::OnImgMediaPlayerCreated);

		}
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

		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		{
#if WITH_EDITORONLY_DATA
			PropertyModule.RegisterCustomPropertyTypeLayout(CustomizedStructName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FImgMediaSourceCustomization::MakeInstance));
#endif // WITH_EDITORONLY_DATA
		}
	}

	/** Unregister details view customizations. */
	void UnregisterCustomizations()
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		{
#if WITH_EDITORONLY_DATA
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
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "SequenceRecorder.TabIcon"),
			true);

		// Add bandwidth tab.
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ImgMediaBandwidthTabName,
			FOnSpawnTab::CreateStatic(&FImgMediaEditorModule::SpawnBandwidthTab))
			.SetGroup(MediaBrowserGroup)
			.SetDisplayName(LOCTEXT("ImgMediaBandwidthTabTitle", "Bandwidth"))
			.SetTooltipText(LOCTEXT("ImgMediaBandwidthTooltipText", "Open the bandwidth tab."))
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "SequenceRecorder.TabIcon"));

		// Add cache tab.
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ImgMediaCacheTabName,
			FOnSpawnTab::CreateStatic(&FImgMediaEditorModule::SpawnCacheTab))
			.SetGroup(MediaBrowserGroup)
			.SetDisplayName(LOCTEXT("ImgMediaCacheTabTitle", "Cache"))
			.SetTooltipText(LOCTEXT("ImgMediaCacheTooltipText", "Open the cache tab."))
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "SequenceRecorder.TabIcon"));

		// Add process images tab.
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ImgMediaProcessImagesTabName,
			FOnSpawnTab::CreateStatic(&FImgMediaEditorModule::SpawnProcessImagesTab))
			.SetGroup(MediaBrowserGroup)
			.SetDisplayName(LOCTEXT("ImgMediaProcessImagesTabTitle", "Process Images"))
			.SetTooltipText(LOCTEXT("ImgMediaProcessImagesTooltipText", "Open the Process Images tab."))
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));
	}

	void UnregisterTabSpawners()
	{
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ImgMediaProcessImagesTabName);
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ImgMediaCacheTabName);
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ImgMediaBandwidthTabName);
		}
	}

	static TSharedRef<SDockTab> SpawnBandwidthTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SImgMediaBandwidth)
			];
	}

	static TSharedRef<SDockTab> SpawnCacheTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SImgMediaCache)
			];
	}

	static TSharedRef<SDockTab> SpawnProcessImagesTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SImgMediaProcessImages)
			];
	}

	void OnImgMediaPlayerCreated(const TSharedPtr<FImgMediaPlayer>& Player)
	{
		// Try and replace an expired player.
		bool bIsAdded = false;
		for (TWeakPtr<FImgMediaPlayer>& PlayerPointer : MediaPlayers)
		{
			if (PlayerPointer.IsValid() == false)
			{
				PlayerPointer = Player;
				bIsAdded = true;
				break;
			}
		}

		// If we were not able to add it, just add it now.
		if (bIsAdded == false)
		{
			MediaPlayers.Add(Player);
		}

		// Send out the message.
		OnImgMediaEditorPlayersUpdated.Broadcast();
	}

private:

	/** Customization name to avoid reusing staticstruct during shutdown. */
	FName CustomizedStructName;

	/** Names for tabs. */
	static FLazyName ImgMediaProcessImagesTabName;

	/** The collection of registered asset type actions. */
	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;

	/** Array of all our players. */
	TArray<TWeakPtr<FImgMediaPlayer>> MediaPlayers;
};

FLazyName FImgMediaEditorModule::ImgMediaProcessImagesTabName(TEXT("ImgMediaProcessImages"));

IMPLEMENT_MODULE(FImgMediaEditorModule, ImgMediaEditor);

#undef LOCTEXT_NAMESPACE
