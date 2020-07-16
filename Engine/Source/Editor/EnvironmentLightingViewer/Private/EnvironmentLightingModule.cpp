// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnvironmentLightingModule.h"
#include "SEnvironmentLightingViewer.h"
#include "Widgets/SWidget.h"
#include "Modules/ModuleManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructure.h"
#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructureModule.h"
#include "Textures/SlateIcon.h"
#include "Framework/Docking/TabManager.h"
#include "EditorStyleSet.h"
#include "Widgets/Docking/SDockTab.h"
#include "Modules/ModuleManager.h"
#include "ISettingsModule.h"

#define LOCTEXT_NAMESPACE "EnvironmentLightingViewer"

IMPLEMENT_MODULE(FEnvironmentLightingViewerModule, EnvironmentLightingViewer);


namespace EnvironmentLightingViewerModule
{
	static const FName EnvironmentLightingViewerApp = FName("EnvironmentLightingViewerApp");
}



TSharedRef<SDockTab> CreateEnvLightTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.EnvironmentLightingViewer"))
		[
			SNew(SEnvironmentLightingViewer)
		];
}



void FEnvironmentLightingViewerModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(EnvironmentLightingViewerModule::EnvironmentLightingViewerApp, FOnSpawnTab::CreateStatic(&CreateEnvLightTab))//TODO picker tab
		.SetDisplayName(NSLOCTEXT("EnvironmentLightingViewerApp", "TabTitle", "EnvironmentLighting Viewer"))
		.SetTooltipText(NSLOCTEXT("EnvironmentLightingViewerApp", "TooltipText", "Environment lighting window."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassViewer.TabIcon"));

	// TODO setting module ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
}

void FEnvironmentLightingViewerModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(EnvironmentLightingViewerModule::EnvironmentLightingViewerApp);
	}

	// Unregister the setting
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	if (SettingsModule)
	{
		SettingsModule->UnregisterSettings("Project", "Editor", "EnvironmentLightingViewer");
	}
}

TSharedRef<SWidget> FEnvironmentLightingViewerModule::CreateEnvironmentLightingViewer()
{
	return SNew(SEnvironmentLightingViewer);
}

#undef LOCTEXT_NAMESPACE
