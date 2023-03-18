// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDModule.h"
#include "ChaosVDStyle.h"
#include "ChaosVDParticleActorCustomization.h"
#include "ChaosVDTabsIDs.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SChaosVDMainTab.h"
#include "PropertyEditorModule.h"
#include "ChaosVDEngine.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

void FChaosVDModule::StartupModule()
{	
	FChaosVDStyle::Initialize();
	FChaosVDStyle::ReloadTextures();

	RegisterClassesCustomDetails();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FChaosVDTabID::ChaosVisualDebuggerTab, FOnSpawnTab::CreateRaw(this, &FChaosVDModule::SpawnMainTab))
								.SetDisplayName(LOCTEXT("VisualDebuggerTabTitle", "Chaos Visual Debugger"))
								.SetTooltipText(LOCTEXT("VisualDebuggerTabDesc", "Opens the Chaos Visual Debugger window"))
								//TODO: Hook up the final icon
								.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "CollisionAnalyzer.TabIcon"))
								.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory());
}

void FChaosVDModule::ShutdownModule()
{
	FChaosVDStyle::Shutdown();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FChaosVDTabID::ChaosVisualDebuggerTab);
}

void FChaosVDModule::RegisterClassesCustomDetails() const
{
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout("ChaosVDParticleActor", FOnGetDetailCustomizationInstance::CreateStatic(&FChaosVDParticleActorCustomization::MakeInstance));
}

TSharedRef<SDockTab> FChaosVDModule::SpawnMainTab(const FSpawnTabArgs& Args) const
{
	TSharedRef<SDockTab> MainTabInstance =
		SNew(SDockTab)
		.TabRole(ETabRole::MajorTab)
		.Label(LOCTEXT("MainTabLabel", "Chaos Visual Debugger"))
		.ToolTipText(LOCTEXT("MainTabToolTip", "The Chaos Visual debugger is under development"));

	// Initialize the Chaos VD Engine instance this tab will represent
	// For now its lifetime will be controlled by this tab
	const TSharedPtr<FChaosVDEngine> ChaosVDEngineInstance = MakeShared<FChaosVDEngine>();
	ChaosVDEngineInstance->Initialize();

	MainTabInstance->SetContent
	(
		SNew(SChaosVDMainTab, ChaosVDEngineInstance)
			.OwnerTab(MainTabInstance.ToSharedPtr())
	);

	MainTabInstance->SetTabIcon(FChaosVDStyle::Get().GetBrush("TabIconPlaybackViewport"));

	return MainTabInstance;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FChaosVDModule, ChaosVD)