// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modes/DMXEditorApplicationMode.h"
#include "DMXEditor.h"
#include "Toolbars/DMXEditorToolbar.h"
#include "DMXEditorTabs.h"
#include "Tabs/DMXEditorTabFactories.h"

#define LOCTEXT_NAMESPACE "DMXEditorApplicationMode"

const FName FDMXEditorApplicationMode::DefaultsMode(TEXT("DefaultsName"));

FText FDMXEditorApplicationMode::GetLocalizedMode(const FName InMode)
{
	static TMap< FName, FText > LocModes;

	if (LocModes.Num() == 0)
	{
		LocModes.Add(DefaultsMode, LOCTEXT("DMXDefaultsMode", "Defaults"));
	}

	check(InMode != NAME_None);
	return LocModes[InMode];
}

FDMXEditorDefaultApplicationMode::FDMXEditorDefaultApplicationMode(TSharedPtr<FDMXEditor> InDMXEditor)
	: FApplicationMode(FDMXEditorApplicationMode::DefaultsMode, FDMXEditorApplicationMode::GetLocalizedMode)
	, DMXEditorCachedPtr(InDMXEditor)
{
	// 1. Create and register Tabs
	DefaultsTabFactories.RegisterFactory(MakeShared<FDMXEditorControllersSummoner>(InDMXEditor));
	DefaultsTabFactories.RegisterFactory(MakeShared<FDMXEditorFixtureTypesSummoner>(InDMXEditor));
	DefaultsTabFactories.RegisterFactory(MakeShared<FDMXEditorFixturePatchSummoner>(InDMXEditor));
	DefaultsTabFactories.RegisterFactory(MakeShared<FDMXEditorInputConsoleSummoner>(InDMXEditor));
	DefaultsTabFactories.RegisterFactory(MakeShared<FDMXEditorOutputConsoleSummoner>(InDMXEditor));

	// 2. REGISATER TAB LAYOUT
	TabLayout = FTabManager::NewLayout("Standalone_SimpleAssetEditor_Layout_v5")
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->SetHideTabWell(true)
				->AddTab(InDMXEditor->GetToolbarTabId(), ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(FDMXEditorTabs::DMXControllersId, ETabState::OpenedTab)
				->AddTab(FDMXEditorTabs::DMXFixtureTypesEditorTabId, ETabState::OpenedTab)
				->AddTab(FDMXEditorTabs::DMXFixturePatchEditorTabId, ETabState::OpenedTab)
				->AddTab(FDMXEditorTabs::DMXInputConsoleEditorTabId, ETabState::OpenedTab)
				->AddTab(FDMXEditorTabs::DMXOutputConsoleEditorTabId, ETabState::OpenedTab)
				->SetForegroundTab(FDMXEditorTabs::DMXControllersId)
			)
		);

	// 3. SETUP TOOLBAR
	InDMXEditor->GetToolbarBuilder()->AddCompileToolbar(ToolbarExtender);
}

void FDMXEditorDefaultApplicationMode::RegisterTabFactories(TSharedPtr<FTabManager> InTabManager)
{
	if (TSharedPtr<FDMXEditor> DMXEditorPtr = DMXEditorCachedPtr.Pin())
	{
		DMXEditorPtr->RegisterToolbarTab(InTabManager.ToSharedRef());

		// Setup all tab factories
		DMXEditorPtr->PushTabFactories(DefaultsTabFactories);

		FApplicationMode::RegisterTabFactories(InTabManager);
	}
}

#undef LOCTEXT_NAMESPACE
