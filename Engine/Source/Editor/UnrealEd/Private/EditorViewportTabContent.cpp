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

TSharedPtr< FEditorViewportLayout > FEditorViewportTabContent::ConstructViewportLayoutByTypeName(const FName& TypeName, bool bSwitchingLayouts)
{
	TSharedPtr< FEditorViewportLayout > ViewportLayout;

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

void FEditorViewportTabContent::Initialize(AssetEditorViewportFactoryFunction Func, TSharedPtr<SDockTab> InParentTab, const FString& InLayoutString)
{
	check(!InLayoutString.IsEmpty());
	check(Func);

	ParentTab = InParentTab;
	LayoutString = InLayoutString;

	FName LayoutType(*LayoutString);
	ViewportCreationFactories.Add(AssetEditorViewportCreationFactories::ElementType(NAME_None, Func));
	SetViewportConfiguration(LayoutType);
}

TSharedPtr<SAssetEditorViewport> FEditorViewportTabContent::CreateSlateViewport(FName InTypeName, const FAssetEditorViewportConstructionArgs& ConstructionArgs) const
{
	if (const AssetEditorViewportFactoryFunction* CreateFunc = ViewportCreationFactories.Find(InTypeName))
	{
		return (*CreateFunc)(ConstructionArgs);
	}

	// CreateSlateViewport should not be called before Initialize
	check(ViewportCreationFactories.Find(NAME_None));
	return ViewportCreationFactories[NAME_None](ConstructionArgs);
}

void FEditorViewportTabContent::SetViewportConfiguration(const FName& ConfigurationName)
{
	bool bSwitchingLayouts = ActiveViewportLayout.IsValid();
	OnViewportTabContentLayoutStartChangeEvent.Broadcast(bSwitchingLayouts);

	if (bSwitchingLayouts)
	{
		SaveConfig();
		ActiveViewportLayout.Reset();
	}

	ActiveViewportLayout = ConstructViewportLayoutByTypeName(ConfigurationName, bSwitchingLayouts);
	check(ActiveViewportLayout.IsValid());

	UpdateViewportTabWidget();

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


void FEditorViewportTabContent::UpdateViewportTabWidget()
{
	TSharedPtr<SDockTab> ParentTabPinned = ParentTab.Pin();
	if (ParentTabPinned.IsValid() && ActiveViewportLayout.IsValid())
	{
		TSharedRef<SWidget> LayoutWidget = StaticCastSharedPtr<FAssetEditorViewportLayout>(ActiveViewportLayout)->BuildViewportLayout(ParentTabPinned, SharedThis(this), LayoutString);
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

void FEditorViewportTabContent::RefreshViewportConfiguration()
{
	if (!ActiveViewportLayout.IsValid())
	{
		return;
	}

	FName ConfigurationName = ActiveViewportLayout->GetLayoutTypeName();
	for (auto& Pair : ActiveViewportLayout->GetViewports())
	{
		if (Pair.Value->AsWidget()->HasFocusedDescendants())
		{
			PreviouslyFocusedViewport = Pair.Key;
			break;
		}
	}

	// Since we don't want config to save out, go ahead and clear out the active viewport layout before refreshing the current layout
	ActiveViewportLayout.Reset();
	SetViewportConfiguration(ConfigurationName);
}

const AssetEditorViewportFactoryFunction* FEditorViewportTabContent::FindViewportCreationFactory(FName InTypeName) const
{
	return ViewportCreationFactories.Find(InTypeName);
}
