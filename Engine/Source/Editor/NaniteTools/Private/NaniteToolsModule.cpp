// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteToolsModule.h"
#include "NaniteToolsArguments.h"
#include "Modules/ModuleManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/Application/SlateApplication.h"
#include "Textures/SlateIcon.h"
#include "Framework/Docking/TabManager.h"
#include "EditorStyleSet.h"
#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructure.h"
#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructureModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "SNaniteTools.h"
#include "ISettingsModule.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "NaniteTools"

IMPLEMENT_MODULE(FNaniteToolsModule, NaniteTools);
DEFINE_LOG_CATEGORY(LogNaniteTools);

namespace FNaniteTools
{
	static const FName NaniteToolsApp = FName(TEXT("NaniteToolsApp"));
}

TSharedRef<SDockTab> FNaniteToolsModule::CreateTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab).TabRole(ETabRole::NomadTab);


	TSharedRef<SNaniteTools> ToolWindowRef = SNew(SNaniteTools, DockTab, Args.GetOwnerWindow());
	DockTab->SetContent(ToolWindowRef);

	AssignToolWindow(ToolWindowRef);
	return DockTab;
}

void FNaniteToolsModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FNaniteTools::NaniteToolsApp, FOnSpawnTab::CreateRaw(this, &FNaniteToolsModule::CreateTab))
		.SetDisplayName(NSLOCTEXT("NaniteToolsApp", "TabTitle", "Nanite Tools"))
		.SetTooltipText(NSLOCTEXT("NaniteToolsApp", "TooltipText", "Tools for auditing and optimizing Nanite assets."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.UserDefinedStruct"));
}

void FNaniteToolsModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FNaniteTools::NaniteToolsApp);
	}
}

#undef LOCTEXT_NAMESPACE
