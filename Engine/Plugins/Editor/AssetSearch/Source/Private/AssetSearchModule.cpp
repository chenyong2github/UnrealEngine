// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IAssetSearchModule.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "WorkspaceMenuStructure.h"
#include "EditorStyleSet.h"
#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructureModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SSearchBrowser.h"
#include "AssetRegistryModule.h"
#include "AssetSearchManager.h"

#define LOCTEXT_NAMESPACE "FAssetSearchModule"

PRAGMA_DISABLE_OPTIMIZATION

static const FName SearchTabName("Search");

class FAssetSearchModule : public IAssetSearchModule
{
public:

	// IAssetSearchModule interface


public:

	// IModuleInterface interface
	
	virtual void StartupModule() override
	{
		SearchManager = MakeUnique<FAssetSearchManager>();
		SearchManager->Start();

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(SearchTabName, FOnSpawnTab::CreateRaw(this, &FAssetSearchModule::HandleSpawnSettingsTab))
			.SetDisplayName(LOCTEXT("Search", "Search"))
			.SetTooltipText(LOCTEXT("Search", "Search Tab"))
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "Symbols.SearchGlass"))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
	}

	virtual void ShutdownModule() override
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SearchTabName);
	}

	void ExecuteOpenObjectBrowser()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(SearchTabName);
	}

	TSharedRef<SDockTab> HandleSpawnSettingsTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
			.TabRole(ETabRole::NomadTab);

		DockTab->SetContent(SNew(SSearchBrowser));

		return DockTab;
	}

	virtual FSearchStats GetStats() const override
	{
		return SearchManager->GetStats();
	}

	virtual void Search(const FSearchQuery& Query, TFunction<void(TArray<FSearchRecord>&&)> InCallback) override
	{
		SearchManager->Search(Query, InCallback);
	}

	virtual void ForceIndexOnAssetsMissingIndex() override
	{
		SearchManager->ForceIndexOnAssetsMissingIndex();
	}

	virtual void RegisterIndexer(FName AssetClassName, IAssetIndexer* Indexer) override
	{
		SearchManager->RegisterIndexer(AssetClassName, Indexer);
	}

	virtual void UnregisterIndexer(IAssetIndexer* Indexer) override
	{
		
	}

private:
	TUniquePtr<FAssetSearchManager> SearchManager;
};

#undef LOCTEXT_NAMESPACE

PRAGMA_ENABLE_OPTIMIZATION

IMPLEMENT_MODULE(FAssetSearchModule, AssetSearch);
