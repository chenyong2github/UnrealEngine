// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "USDStageEditorModule.h"

#include "SUSDStage.h"
#include "USDMemory.h"
#include "USDStageActor.h"

#include "EditorStyleSet.h"
#include "EngineUtils.h"
#include "Framework/Docking/TabManager.h"
#include "IAssetTools.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "UsdStageEditorModule"

class FUsdStageEditorModule : public IUsdStageEditorModule
{
public:
#if USE_USD_SDK
	static TSharedRef< SDockTab > SpawnUsdStageTab( const FSpawnTabArgs& SpawnTabArgs )
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			.Icon( FEditorStyle::GetBrush("LevelEditor.Tabs.Layers") )
			.Label( LOCTEXT( "USDStage", "USD Stage" ) )
			[
				SNew(SBorder)
				.Padding(0)
				.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
				[
					SNew( SUsdStage )
				]
			];
	}

	virtual void StartupModule() override
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked< FLevelEditorModule >( "LevelEditor" );
		LevelEditorTabManagerChangedHandle = LevelEditorModule.OnTabManagerChanged().AddLambda(
			[]()
			{
				FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked< FLevelEditorModule >( "LevelEditor" );
				TSharedPtr< FTabManager > LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

				const FSlateIcon LayersIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Layers");

				LevelEditorTabManager->RegisterTabSpawner( TEXT("USDStage"), FOnSpawnTab::CreateStatic( &FUsdStageEditorModule::SpawnUsdStageTab ) )
					.SetDisplayName( LOCTEXT( "USDStage", "USD Stage" ) )
					.SetTooltipText( LOCTEXT( "USDStageTab", "Open USD Stage tab" ) )
					.SetGroup( WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory() )
					.SetIcon( LayersIcon );
			});
	}

	virtual void ShutdownModule() override
	{
		if ( LevelEditorTabManagerChangedHandle.IsValid() && FModuleManager::Get().IsModuleLoaded( "LevelEditor" ) )
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>( "LevelEditor" );
			LevelEditorModule.OnTabManagerChanged().Remove( LevelEditorTabManagerChangedHandle );
		}
	}

private:
	FDelegateHandle LevelEditorTabManagerChangedHandle;
#endif // #if USE_USD_SDK
};

IMPLEMENT_MODULE_USD( FUsdStageEditorModule, USDStageEditor );

#undef LOCTEXT_NAMESPACE
