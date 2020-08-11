// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tabs/DMXEditorTabFactories.h"

#include "DMXEditorTabs.h"
#include "DMXEditor.h"
#include "Library/DMXEntityFixtureType.h"
#include "Widgets/SDMXEntityEditor.h"
#include "Widgets/Controller/SDMXControllerEditor.h"
#include "Widgets/FixtureType/SDMXFixtureTypeEditor.h"
#include "Widgets/FixturePatch/SDMXFixturePatchEditor.h"

#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"


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
			DMXEditor->GetControllerEditor()
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
			DMXEditor->GetFixtureTypeEditor()
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
			DMXEditor->GetFixturePatchEditor()
		];
}

#undef LOCTEXT_NAMESPACE

