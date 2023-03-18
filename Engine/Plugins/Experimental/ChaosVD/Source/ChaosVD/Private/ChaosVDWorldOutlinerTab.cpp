// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDWorldOutlinerTab.h"

#include "ChaosVDStyle.h"
#include "ChaosVDTabsIDs.h"
#include "Modules/ModuleManager.h"
#include "SceneOutlinerModule.h"
#include "SceneOutlinerPublicTypes.h"
#include "Widgets/Docking/SDockTab.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

TSharedRef<SDockTab> FChaosVDWorldOutlinerTab::HandleTabSpawned(const FSpawnTabArgs& Args)
{
	FSceneOutlinerInitializationOptions InitOptions;
	InitOptions.bShowTransient = true;

	InitOptions.OutlinerIdentifier = TEXT("ChaosVDOutliner");
	InitOptions.FilterBarOptions.bHasFilterBar = true;

	InitOptions.FilterBarOptions.bUseSharedSettings = false;

	const FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::Get().LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");

	//TODO: See if we can change this to an Actor Picker instead of Actor Browser as it is simpler
	SceneOutlinerWidget = SceneOutlinerModule.CreateActorBrowser(InitOptions, TWeakObjectPtr<UWorld>(GetChaosVDWorld()));

	TSharedRef<SDockTab> OutlinerTab =
		SNew(SDockTab)
		.TabRole(ETabRole::MajorTab)
		.Label(LOCTEXT("Physics World Outliner", "Physics World Outliner"))
		.ToolTipText(LOCTEXT("PhysicsWorldOutlinerTabToolTip", "Hierachy view of the physics objects by category"));
	
	OutlinerTab->SetContent
	(
		SceneOutlinerWidget.ToSharedRef()
	);

	OutlinerTab->SetTabIcon(FChaosVDStyle::Get().GetBrush("TabIconWorldOutliner"));

	return OutlinerTab;
}

#undef LOCTEXT_NAMESPACE
