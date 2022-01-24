// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintHeaderView.h"
#include "Widgets/SWidget.h"
#include "Modules/ModuleManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/Application/SlateApplication.h"
#include "Textures/SlateIcon.h"
#include "Framework/Docking/TabManager.h"
#include "EditorStyleSet.h"
#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructure.h"
#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructureModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "SBlueprintHeaderView.h"

namespace BlueprintHeaderViewModule
{
	static const FName HeaderViewTabName = "BlueprintHeaderViewApp";

	TSharedRef<SDockTab> CreateHeaderViewTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SBlueprintHeaderView)
			];
	}
}

void FBlueprintHeaderViewModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(BlueprintHeaderViewModule::HeaderViewTabName, FOnSpawnTab::CreateStatic(&BlueprintHeaderViewModule::CreateHeaderViewTab))
		.SetDisplayName(NSLOCTEXT("BlueprintHeaderViewApp", "TabTitle", "Blueprint Header View"))
		.SetTooltipText(NSLOCTEXT("BlueprintHeaderViewApp", "TooltipText", "Displays a Blueprint Class in C++ Header format."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.Class"));
}

void FBlueprintHeaderViewModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}
	
IMPLEMENT_MODULE(FBlueprintHeaderViewModule, BlueprintHeaderView)