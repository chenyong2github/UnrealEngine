// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tabs/DMXEditorTabFactories.h"
#include "DMXEditorTabs.h"
#include "DMXEditor.h"

#include "Widgets/SDMXInputConsole.h"
#include "Widgets/SDMXEntityEditor.h"

#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "DMXWorkflowTabFactory"

TSharedRef<SDockTab> FDMXEditorPropertyTabSummoner::SpawnTab(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedRef<SDockTab> NewTab = FWorkflowTabFactory::SpawnTab(Info);
	NewTab->SetTag(GetIdentifier());

	TSharedPtr<FDMXEditor> DMXEditor = StaticCastSharedPtr<FDMXEditor>(HostingApp.Pin());

	return NewTab;
}


FDMXEditorControllersSummoner::FDMXEditorControllersSummoner(TSharedPtr<FAssetEditorToolkit> InHostingApp)
	: FDMXEditorPropertyTabSummoner(FDMXEditorTabs::DMXControllersId, InHostingApp)
{
	TabLabel = LOCTEXT("DMXControllersTabLabel", "Controllers");
	TabIcon = FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Tabs.BlueprintDefaults");

	bIsSingleton = true;

	ViewMenuDescription = LOCTEXT("DMXControllersView", "Controllers");
	ViewMenuTooltip = LOCTEXT("DMXControllersViewTooltip", "Show the controllers view");
}

TSharedRef<SWidget> FDMXEditorControllersSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedPtr<FDMXEditor> DMXEditor = StaticCastSharedPtr<FDMXEditor>(HostingApp.Pin());

	return SNew(SBox)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			DMXEditor->GetControllersTab()
		];
}

FDMXEditorFixtureTypesSummoner::FDMXEditorFixtureTypesSummoner(TSharedPtr<FAssetEditorToolkit> InHostingApp)
	: FDMXEditorPropertyTabSummoner(FDMXEditorTabs::DMXFixtureTypesEditorTabId, InHostingApp)
{
	TabLabel = LOCTEXT("DMXFixtureTypesTabLabel", "Fixture Types");
	TabIcon = FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Tabs.BlueprintDefaults");

	bIsSingleton = true;

	ViewMenuDescription = LOCTEXT("DMXFixtureTypesView", "Fixture Types");
	ViewMenuTooltip = LOCTEXT("DMXFixtureTypesViewTooltip", "Show the fixture types view");
}

TSharedRef<SWidget> FDMXEditorFixtureTypesSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedPtr<FDMXEditor> DMXEditor = StaticCastSharedPtr<FDMXEditor>(HostingApp.Pin());

	return SNew(SBox)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			DMXEditor->GetFixtureTypesTab()
		];
}

FDMXEditorFixturePatchSummoner::FDMXEditorFixturePatchSummoner(TSharedPtr<FAssetEditorToolkit> InHostingApp)
	: FDMXEditorPropertyTabSummoner(FDMXEditorTabs::DMXFixturePatchEditorTabId, InHostingApp)
{
	TabLabel = LOCTEXT("DMXFixturePatchTabLabel", "Fixture Patch");
	TabIcon = FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Tabs.BlueprintDefaults");

	bIsSingleton = true;

	ViewMenuDescription = LOCTEXT("DMXFixturePatchView", "Fixture Patch");
	ViewMenuTooltip = LOCTEXT("DMXFixturePatchViewTooltip", "Show the fixture patch view");
}

TSharedRef<SWidget> FDMXEditorFixturePatchSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedPtr<FDMXEditor> DMXEditor = StaticCastSharedPtr<FDMXEditor>(HostingApp.Pin());

	return SNew(SBox)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			DMXEditor->GetFixturePatchTab()
		];
}

FDMXEditorInputConsoleSummoner::FDMXEditorInputConsoleSummoner(TSharedPtr<FAssetEditorToolkit> InHostingApp)
	: FWorkflowTabFactory(FDMXEditorTabs::DMXInputConsoleEditorTabId, InHostingApp)
{
	TabLabel = LOCTEXT("DMXInputConsoleTabLabel", "Monitor");
	TabIcon = FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Tabs.BlueprintDefaults");

	bIsSingleton = true;

	ViewMenuDescription = LOCTEXT("DMXInputConsoleView", "Monitor");
	ViewMenuTooltip = LOCTEXT("DMXInputConsoleViewTooltip", "Show the monitor view");
}

TSharedRef<SWidget> FDMXEditorInputConsoleSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedPtr<FDMXEditor> DMXEditor = StaticCastSharedPtr<FDMXEditor>(HostingApp.Pin());

	return SNew(SBox)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			DMXEditor->GetInputConsoleTab()
		];
}

FDMXEditorOutputConsoleSummoner::FDMXEditorOutputConsoleSummoner(TSharedPtr<FAssetEditorToolkit> InHostingApp)
	: FWorkflowTabFactory(FDMXEditorTabs::DMXOutputConsoleEditorTabId, InHostingApp)
{
	TabLabel = LOCTEXT("DMXOutputConsoleTabLabel", "Output Console");
	TabIcon = FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Tabs.BlueprintDefaults");

	bIsSingleton = true;

	ViewMenuDescription = LOCTEXT("DMXOutputConsoleView", "Output Console");
	ViewMenuTooltip = LOCTEXT("DMXOutputConsoleViewTooltip", "Show the output console view");
}

TSharedRef<SWidget> FDMXEditorOutputConsoleSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedPtr<FDMXEditor> DMXEditor = StaticCastSharedPtr<FDMXEditor>(HostingApp.Pin());

	return DMXEditor->GetOutputConsoleTab();
}

#undef LOCTEXT_NAMESPACE

