// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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


// FLevelViewportTabContent ///////////////////////////

TSharedPtr< class FLevelViewportLayout > FLevelViewportTabContent::ConstructViewportLayoutByTypeName(const FName& TypeName, bool bSwitchingLayouts)
{
	TSharedPtr< class FLevelViewportLayout > ViewportLayout;

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

void FLevelViewportTabContent::Initialize(TSharedPtr<ILevelEditor> InParentLevelEditor, TSharedPtr<SDockTab> InParentTab, const FString& InLayoutString)
{
	ParentTab = InParentTab;
	ParentLevelEditor = InParentLevelEditor;
	LayoutString = InLayoutString;

	InParentTab->SetOnPersistVisualState( SDockTab::FOnPersistVisualState::CreateSP(this, &FLevelViewportTabContent::SaveConfig) );

	const FString& IniSection = FLayoutSaveRestore::GetAdditionalLayoutConfigIni();

	FString LayoutTypeString;
	if(LayoutString.IsEmpty() ||
		!GConfig->GetString(*IniSection, *(InLayoutString + TEXT(".LayoutType")), LayoutTypeString, GEditorPerProjectIni))
	{
		LayoutTypeString = LevelViewportConfigurationNames::FourPanes2x2.ToString();
	}
	FName LayoutType(*LayoutTypeString);
	SetViewportConfiguration(LayoutType);
}


bool FLevelViewportTabContent::IsVisible() const
{
	if (ActiveViewportLayout.IsValid())
	{
		TSharedPtr<FLevelViewportLayout> ViewportLayout = StaticCastSharedPtr<FLevelViewportLayout>(ActiveViewportLayout);
		return ViewportLayout->IsVisible();
	}
	return false;
}

const TMap< FName, TSharedPtr< IEditorViewportLayoutEntity > >* FLevelViewportTabContent::GetViewports() const
{
	if (ActiveViewportLayout.IsValid())
	{
		return &ActiveViewportLayout->GetViewports();
	}
	return nullptr;
}

void FLevelViewportTabContent::SetViewportConfiguration(const FName& ConfigurationName)
{
	bool bSwitchingLayouts = ActiveViewportLayout.IsValid();

	if (bSwitchingLayouts)
	{
		SaveConfig();
		ActiveViewportLayout.Reset();
	}

	ActiveViewportLayout = ConstructViewportLayoutByTypeName(ConfigurationName, bSwitchingLayouts);
	check (ActiveViewportLayout.IsValid());

	UpdateViewportTabWidget();
}

void FLevelViewportTabContent::SaveConfig() const
{
	if (ActiveViewportLayout.IsValid())
	{
		if (!LayoutString.IsEmpty())
		{
			FString LayoutTypeString = ActiveViewportLayout->GetLayoutTypeName().ToString();

			const FString& IniSection = FLayoutSaveRestore::GetAdditionalLayoutConfigIni();
			GConfig->SetString(*IniSection, *(LayoutString + TEXT(".LayoutType")), *LayoutTypeString, GEditorPerProjectIni);
		}

		ActiveViewportLayout->SaveLayoutString(LayoutString);
	}
}

void FLevelViewportTabContent::RefreshViewportConfiguration()
{
	check(ActiveViewportLayout.IsValid());

	FName ConfigurationName = ActiveViewportLayout->GetLayoutTypeName();
	for (auto& Pair : ActiveViewportLayout->GetViewports())
	{
		if (Pair.Value->AsWidget()->HasFocusedDescendants())
		{
			PreviouslyFocusedViewport = Pair.Key;
			break;
		}
	}

	ActiveViewportLayout.Reset();

	bool bSwitchingLayouts = false;
	ActiveViewportLayout = ConstructViewportLayoutByTypeName(ConfigurationName, bSwitchingLayouts);
	check(ActiveViewportLayout.IsValid());

	UpdateViewportTabWidget();
}


bool FLevelViewportTabContent::IsViewportConfigurationSet(const FName& ConfigurationName) const
{
	if (ActiveViewportLayout.IsValid())
	{
		return ActiveViewportLayout->GetLayoutTypeName() == ConfigurationName;
	}
	return false;
}

bool FLevelViewportTabContent::BelongsToTab(TSharedRef<class SDockTab> InParentTab) const
{
	TSharedPtr<SDockTab> ParentTabPinned = ParentTab.Pin();
	return ParentTabPinned == InParentTab;
}

void FLevelViewportTabContent::UpdateViewportTabWidget()
{
	TSharedPtr<SDockTab> ParentTabPinned = ParentTab.Pin();
	if (ParentTabPinned.IsValid() && ActiveViewportLayout.IsValid())
	{

		TSharedRef<SWidget> LayoutWidget = StaticCastSharedPtr<FLevelViewportLayout>(ActiveViewportLayout)->BuildViewportLayout(ParentTabPinned, SharedThis(this), LayoutString, ParentLevelEditor);
		ParentTabPinned->SetContent(LayoutWidget);

		if (PreviouslyFocusedViewport.IsSet())
		{
			TSharedPtr<IEditorViewportLayoutEntity> ViewportToFocus = ActiveViewportLayout->GetViewports().FindRef(PreviouslyFocusedViewport.GetValue());
			if (ViewportToFocus.IsValid())
			{
				ViewportToFocus->SetKeyboardFocus();
			}
			PreviouslyFocusedViewport = TOptional<FName>();
		}
	}
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.OnTabContentChanged().Broadcast();
}
