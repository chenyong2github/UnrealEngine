// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelViewportTabContent.h"
#include "Misc/ConfigCacheIni.h"
#include "Framework/Docking/LayoutService.h"
#include "Modules/ModuleManager.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "LevelViewportLayout.h"
#include "LevelEditorViewport.h"


// FLevelViewportTabContent ///////////////////////////

TSharedPtr< FEditorViewportLayout > FLevelViewportTabContent::FactoryViewportLayout(bool bIsSwitchingLayouts)
{
	TSharedPtr<FLevelViewportLayout> ViewportLayout = MakeShareable(new FLevelViewportLayout);
	ViewportLayout->SetIsReplacement(bIsSwitchingLayouts);
	return ViewportLayout;
}

FLevelViewportTabContent::~FLevelViewportTabContent()
{
	if (GEditor)
	{
		GEditor->OnLevelViewportClientListChanged().RemoveAll(this);
	}
}

void FLevelViewportTabContent::Initialize(AssetEditorViewportFactoryFunction Func, TSharedPtr<SDockTab> InParentTab, const FString& InLayoutString)
{
	InParentTab->SetOnPersistVisualState( SDockTab::FOnPersistVisualState::CreateSP(this, &FLevelViewportTabContent::SaveConfig) );

	const FString& IniSection = FLayoutSaveRestore::GetAdditionalLayoutConfigIni();

	FString LayoutTypeString;
	if(InLayoutString.IsEmpty() ||
		!GConfig->GetString(*IniSection, *(InLayoutString + TEXT(".LayoutType")), LayoutTypeString, GEditorPerProjectIni))
	{
		LayoutTypeString = EditorViewportConfigurationNames::FourPanes2x2.ToString();
	}

	OnViewportTabContentLayoutStartChangeEvent.AddSP(this, &FLevelViewportTabContent::OnLayoutStartChange);
	OnViewportTabContentLayoutChangedEvent.AddSP(this, &FLevelViewportTabContent::OnLayoutChanged);

	FEditorViewportTabContent::Initialize(Func, InParentTab, LayoutTypeString);
}

void FLevelViewportTabContent::OnLayoutStartChange(bool bSwitchingLayouts)
{
	GCurrentLevelEditingViewportClient = nullptr;
	GLastKeyLevelEditingViewportClient = nullptr;
}

void FLevelViewportTabContent::OnLayoutChanged()
{
	// Set the global level editor to the first, valid perspective viewport found
	if (GEditor)
	{
		const TArray<FLevelEditorViewportClient*>& LevelViewportClients = GEditor->GetLevelViewportClients();
		for (FLevelEditorViewportClient* LevelViewport : LevelViewportClients)
		{
			if (LevelViewport->IsPerspective())
			{
				LevelViewport->SetCurrentViewport();
				break;
			}
		}
		// Otherwise just make sure it's set to something
		if (!GCurrentLevelEditingViewportClient && LevelViewportClients.Num())
		{
			GCurrentLevelEditingViewportClient = LevelViewportClients[0];
		}
	}

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.OnTabContentChanged().Broadcast();
}
