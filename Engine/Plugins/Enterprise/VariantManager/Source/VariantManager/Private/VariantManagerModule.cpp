// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VariantManagerModule.h"

#include "LevelVariantSets.h"
#include "LevelVariantSetsEditorToolkit.h"
#include "VariantManager.h"
#include "VariantManagerEditorCommands.h"
#include "VariantManagerContentEditorModule.h"

#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "IAssetTypeActions.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "VariantManagerUtils.h"

#define LOCTEXT_NAMESPACE "VariantManagerModule"

class FVariantManagerModule : public IVariantManagerModule
{
public:
	virtual void StartupModule() override
	{
		FVariantManagerEditorCommands::Register();

		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));

		// Register a delegate to detect whenever we should open an editor for a LevelVariantSets asset, and relay the
		// data to LevelVariantSetsEditorToolkit, which will spawn a VariantManager
		IVariantManagerContentEditorModule& ContentEditorModule = FModuleManager::LoadModuleChecked<IVariantManagerContentEditorModule>(VARIANTMANAGERCONTENTEDITORMODULE_MODULE_NAME);
		FOnLevelVariantSetsEditor LevelVariantSetsEditorDelegate = FOnLevelVariantSetsEditor::CreateStatic( FVariantManagerModule::OnLevelVariantSetsEditor );
		ContentEditorModule.RegisterOnLevelVariantSetsDelegate( LevelVariantSetsEditorDelegate );

		// We need to register a tab spawner now so that old tabs that were open when we closed the editor can be reopened
		// correctly displaying the "Variant Manager" title
		// Sadly this code runs after the LevelEditorModule is loaded, but before it has created its TabManager. We need to
		// subscribe to this event then, so that as soon as its created we'll register this spawner
		OnTabManagerChangedSubscription = LevelEditorModule.OnTabManagerChanged().AddLambda([ &LevelEditorModule ]()
		{
			RegisterTabSpawner( LevelEditorModule.GetLevelEditorTabManager() );
		});

		// Make sure we update the cached UProperty pointers we use for exception properties whenever hot reload happens to a relevant class
		FVariantManagerUtils::RegisterForHotReload();
	}

	virtual void ShutdownModule() override
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LevelEditorModule.OnTabManagerChanged().Remove(OnTabManagerChangedSubscription);

		IVariantManagerContentEditorModule& ContentEditorModule = FModuleManager::LoadModuleChecked<IVariantManagerContentEditorModule>(VARIANTMANAGERCONTENTEDITORMODULE_MODULE_NAME);
		ContentEditorModule.UnregisterOnLevelVariantSetsDelegate();

		UnregisterTabSpawner( LevelEditorModule.GetLevelEditorTabManager() );

		FVariantManagerUtils::UnregisterForHotReload();

		FVariantManagerEditorCommands::Unregister();
	}

	virtual TSharedRef<FVariantManager> CreateVariantManager(ULevelVariantSets* InLevelVariantSets) override
	{
		TSharedRef<FVariantManager> VariantManager = MakeShareable(new FVariantManager);
		VariantManager->InitVariantManager(InLevelVariantSets);

		return VariantManager;
	}

	static void RegisterTabSpawner( const TSharedPtr< FTabManager >& TabManager )
	{
		if ( !TabManager )
		{
			return;
		}

		if ( TabManager->HasTabSpawner( FLevelVariantSetsEditorToolkit::GetVariantManagerTabID() ) )
		{
			UnregisterTabSpawner( TabManager );
		}

		TabManager->RegisterTabSpawner( FLevelVariantSetsEditorToolkit::GetVariantManagerTabID(), FOnSpawnTab::CreateStatic( &FVariantManagerModule::HandleTabManagerSpawnTab ) )
			.SetDisplayName( LOCTEXT("VariantManagerMainTab", "Variant Manager") )
			.SetGroup( WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory() )
			.SetIcon( FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.GameSettings.Small") );
	}

	static void UnregisterTabSpawner( const TSharedPtr< FTabManager >& TabManager )
	{
		if ( !TabManager )
		{
			return;
		}

		TabManager->UnregisterTabSpawner( FLevelVariantSetsEditorToolkit::GetVariantManagerTabID() );
	}

	static TSharedRef<SDockTab> HandleTabManagerSpawnTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.Label(LOCTEXT("VariantManagerMainTitle", "VariantManager"))
			.TabColorScale( FLevelVariantSetsEditorToolkit::GetWorldCentricTabColorScaleStatic() )
			.TabRole(ETabRole::PanelTab);
	}

	static void OnLevelVariantSetsEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& EditWithinLevelEditor, class ULevelVariantSets* LevelVariantSets)
	{
		TSharedPtr<ISlateStyle> Style = MakeShareable(new FSlateStyleSet(TEXT("EditorStyle")));
		TSharedRef<FLevelVariantSetsEditorToolkit> Toolkit = MakeShareable(new FLevelVariantSetsEditorToolkit(Style.ToSharedRef()));
		Toolkit->Initialize(Mode, EditWithinLevelEditor, LevelVariantSets);
	}

private:
	FDelegateHandle OnTabManagerChangedSubscription;
};

IMPLEMENT_MODULE(FVariantManagerModule, VariantManager);

#undef LOCTEXT_NAMESPACE