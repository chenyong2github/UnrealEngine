// Copyright Epic Games, Inc. All Rights Reserved.

#include "InsightsMenuBuilder.h"

#include "Framework/Docking/TabManager.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

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
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////
