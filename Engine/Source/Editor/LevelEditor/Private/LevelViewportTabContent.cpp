// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelViewportTabContent.h"
#include "Misc/ConfigCacheIni.h"
#include "Framework/Docking/LayoutService.h"
#include "Modules/ModuleManager.h"
#include "LevelEditor.h"
#include "LevelViewportLayout2x2.h"
#include "LevelViewportLayoutOnePane.h"
#include "LevelViewportLayoutTwoPanes.h"
#include "LevelViewportLayoutThreePanes.h"
#include "LevelViewportLayoutFourPanes.h"
#include "Widgets/Docking/SDockTab.h"
#include "LevelViewportLayout.h"
#include "LevelEditorViewport.h"


// FLevelViewportTabContent ///////////////////////////

TSharedPtr< FEditorViewportLayout > FLevelViewportTabContent::ConstructViewportLayoutByTypeName(const FName& TypeName, bool bSwitchingLayouts)
{
	TSharedPtr< FLevelViewportLayout > ViewportLayout;

	// The items in these ifs should match the names in namespace LevelViewportConfigurationNames
	if (TypeName == LevelViewportConfigurationNames::FourPanes2x2) ViewportLayout = MakeShareable(new FLevelViewportLayout2x2);
	else if (TypeName == LevelViewportConfigurationNames::TwoPanesVert) ViewportLayout = MakeShareable(new FLevelViewportLayoutTwoPanesVert);
	else if (TypeName == LevelViewportConfigurationNames::TwoPanesHoriz) ViewportLayout = MakeShareable(new FLevelViewportLayoutTwoPanesHoriz);
	else if (TypeName == LevelViewportConfigurationNames::ThreePanesLeft) ViewportLayout = MakeShareable(new FLevelViewportLayoutThreePanesLeft);
	else if (TypeName == LevelViewportConfigurationNames::ThreePanesRight) ViewportLayout = MakeShareable(new FLevelViewportLayoutThreePanesRight);
	else if (TypeName == LevelViewportConfigurationNames::ThreePanesTop) ViewportLayout = MakeShareable(new FLevelViewportLayoutThreePanesTop);
	else if (TypeName == LevelViewportConfigurationNames::ThreePanesBottom) ViewportLayout = MakeShareable(new FLevelViewportLayoutThreePanesBottom);
	else if (TypeName == LevelViewportConfigurationNames::FourPanesLeft) ViewportLayout = MakeShareable(new FLevelViewportLayoutFourPanesLeft);
	else if (TypeName == LevelViewportConfigurationNames::FourPanesRight) ViewportLayout = MakeShareable(new FLevelViewportLayoutFourPanesRight);
	else if (TypeName == LevelViewportConfigurationNames::FourPanesBottom) ViewportLayout = MakeShareable(new FLevelViewportLayoutFourPanesBottom);
	else if (TypeName == LevelViewportConfigurationNames::FourPanesTop) ViewportLayout = MakeShareable(new FLevelViewportLayoutFourPanesTop);
	else if (TypeName == LevelViewportConfigurationNames::OnePane) ViewportLayout = MakeShareable(new FLevelViewportLayoutOnePane);

	if (!ensure(ViewportLayout.IsValid()))
	{
		ViewportLayout = MakeShareable(new FLevelViewportLayoutOnePane);
	}
	ViewportLayout->SetIsReplacement(bSwitchingLayouts);
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
		LayoutTypeString = LevelViewportConfigurationNames::FourPanes2x2.ToString();
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
