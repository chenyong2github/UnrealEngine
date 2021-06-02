// Copyright Epic Games, Inc. All Rights Reserved.

#include "InsightsMenuBuilder.h"

#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "InsightsMenuBuilder"

////////////////////////////////////////////////////////////////////////////////////////////////////

FInsightsMenuBuilder::FInsightsMenuBuilder()
#if !WITH_EDITOR
	: InsightsToolsGroup(FGlobalTabmanager::Get()->AddLocalWorkspaceMenuCategory(NSLOCTEXT("InsightsMenuTools", "InsightTools", "Insights Tools")))
	, WindowsGroup(FGlobalTabmanager::Get()->AddLocalWorkspaceMenuCategory(NSLOCTEXT("InsightsMenuTools", "InsightWindows", "Windows")))
#endif
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<FWorkspaceItem> FInsightsMenuBuilder::GetInsightsToolsGroup()
{
#if !WITH_EDITOR
	return InsightsToolsGroup;
#else
	return WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory();
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<FWorkspaceItem> FInsightsMenuBuilder::GetWindowsGroup()
{
#if !WITH_EDITOR
	return WindowsGroup;
#else
	return WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory();
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsMenuBuilder::PopulateMenu(FMenuBuilder& MenuBuilder)
{
#if !WITH_EDITOR
	FGlobalTabmanager::Get()->PopulateLocalTabSpawnerMenu(MenuBuilder);

	static FName WidgetReflectorTabId("WidgetReflector");
	bool bAllowDebugTools = FGlobalTabmanager::Get()->HasTabSpawner(WidgetReflectorTabId);
	if (bAllowDebugTools)
	{
		MenuBuilder.BeginSection("WidgetTools");
		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenWidgetReflector", "Widget Reflector"),
			LOCTEXT("OpenWidgetReflectorToolTip", "Opens the Widget Reflector, a handy tool for diagnosing problems with live widgets."),
			FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "WidgetReflector.Icon"),
			FUIAction(FExecuteAction::CreateLambda([=] { FGlobalTabmanager::Get()->TryInvokeTab(WidgetReflectorTabId); })));
		MenuBuilder.EndSection();
	}
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
