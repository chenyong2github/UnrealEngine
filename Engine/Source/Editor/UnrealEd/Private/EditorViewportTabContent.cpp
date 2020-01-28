// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorViewportTabContent.h"
#include "SEditorViewport.h"
#include "EditorViewportLayoutOnePane.h"
#include "EditorViewportLayoutTwoPanes.h"
#include "EditorViewportLayoutThreePanes.h"
#include "EditorViewportLayoutFourPanes.h"
#include "EditorViewportLayout2x2.h"
#include "Framework/Docking/LayoutService.h"
#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"




TSharedPtr< class FEditorViewportLayout > FEditorViewportTabContent::ConstructViewportLayoutByTypeName(const FName& TypeName, bool bSwitchingLayouts)
{
	TSharedPtr< class FEditorViewportLayout > ViewportLayout;

	//The items in these ifs should match the names in namespace EditorViewportConfigurationNames
	if (TypeName == EditorViewportConfigurationNames::TwoPanesHoriz) ViewportLayout = MakeShareable(new FEditorViewportLayoutTwoPanesHoriz);
	else if (TypeName == EditorViewportConfigurationNames::TwoPanesVert) ViewportLayout = MakeShareable(new FEditorViewportLayoutTwoPanesVert);
 	else if (TypeName == EditorViewportConfigurationNames::FourPanes2x2) ViewportLayout = MakeShareable(new FEditorViewportLayout2x2);
	else if (TypeName == EditorViewportConfigurationNames::ThreePanesLeft) ViewportLayout = MakeShareable(new FEditorViewportLayoutThreePanesLeft);
	else if (TypeName == EditorViewportConfigurationNames::ThreePanesRight) ViewportLayout = MakeShareable(new FEditorViewportLayoutThreePanesRight);
	else if (TypeName == EditorViewportConfigurationNames::ThreePanesTop) ViewportLayout = MakeShareable(new FEditorViewportLayoutThreePanesTop);
	else if (TypeName == EditorViewportConfigurationNames::ThreePanesBottom) ViewportLayout = MakeShareable(new FEditorViewportLayoutThreePanesBottom);
	else if (TypeName == EditorViewportConfigurationNames::FourPanesLeft) ViewportLayout = MakeShareable(new FEditorViewportLayoutFourPanesLeft);
	else if (TypeName == EditorViewportConfigurationNames::FourPanesRight) ViewportLayout = MakeShareable(new FEditorViewportLayoutFourPanesRight);
	else if (TypeName == EditorViewportConfigurationNames::FourPanesBottom) ViewportLayout = MakeShareable(new FEditorViewportLayoutFourPanesBottom);
  	else if (TypeName == EditorViewportConfigurationNames::FourPanesTop) ViewportLayout = MakeShareable(new FEditorViewportLayoutFourPanesTop);
	else /*(TypeName == EditorViewportConfigurationNames::OnePane)*/ ViewportLayout = MakeShareable(new FEditorViewportLayoutOnePane);

	if (!ensure(ViewportLayout.IsValid()))
	{
		ViewportLayout = MakeShareable(new FEditorViewportLayoutOnePane);
	}
	return ViewportLayout;
}

void FEditorViewportTabContent::Initialize(TFunction<TSharedRef<SEditorViewport>(void)> Func, TSharedPtr<SDockTab> InParentTab, const FString& InLayoutString)
{
	check(!InLayoutString.IsEmpty());

	ParentTab = InParentTab;
	LayoutString = InLayoutString;

	FName LayoutType(*LayoutString);
	SetViewportConfiguration(Func, LayoutType);
}

void FEditorViewportTabContent::SetViewportConfiguration(TFunction<TSharedRef<SEditorViewport>(void)> &Func, const FName& ConfigurationName)
{
	ViewportCreationFunc = Func;
	SetViewportConfiguration(ConfigurationName);
}

void FEditorViewportTabContent::SetViewportConfiguration(const FName& ConfigurationName)
{
	check(ViewportCreationFunc != nullptr);

	bool bSwitchingLayouts = ActiveViewportLayout.IsValid();

	if (bSwitchingLayouts)
	{
		SaveConfig();
		ActiveViewportLayout.Reset();
	}

	ActiveViewportLayout = ConstructViewportLayoutByTypeName(ConfigurationName, bSwitchingLayouts);
	check(ActiveViewportLayout.IsValid());

	UpdateViewportTabWidget(ViewportCreationFunc);

	OnViewportTabContentLayoutChangedEvent.Broadcast();
}

void FEditorViewportTabContent::SaveConfig() const
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

TSharedPtr<SEditorViewport> FEditorViewportTabContent::GetFirstViewport()
{
 	const TMap< FName, TSharedPtr< IEditorViewportLayoutEntity > >& EditorViewports = ActiveViewportLayout->GetViewports();
 
	for (auto& Pair : EditorViewports)
	{
		TSharedPtr<SWidget> ViewportWidget = StaticCastSharedPtr<IEditorViewportLayoutEntity>(Pair.Value)->AsWidget();
		TSharedPtr<SEditorViewport> Viewport = StaticCastSharedPtr<SEditorViewport>(ViewportWidget);
		if (Viewport.IsValid())
		{
			return Viewport;
			break;
		}
	}

	return nullptr;
}


void FEditorViewportTabContent::UpdateViewportTabWidget(TFunction<TSharedRef<SEditorViewport>(void)> &Func)
{
	TSharedPtr<SDockTab> ParentTabPinned = ParentTab.Pin();
	if (ParentTabPinned.IsValid() && ActiveViewportLayout.IsValid())
	{
		TSharedRef<SWidget> LayoutWidget = StaticCastSharedPtr<FAssetEditorViewportLayout>(ActiveViewportLayout)->BuildViewportLayout(Func, ParentTabPinned, SharedThis(this), LayoutString);
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
}

