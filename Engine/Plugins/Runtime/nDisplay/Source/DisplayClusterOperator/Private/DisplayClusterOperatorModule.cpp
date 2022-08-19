// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterOperatorModule.h"

#include "DisplayClusterOperatorViewModel.h"
#include "SDisplayClusterOperatorPanel.h"
#include "DisplayClusterRootActor.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "LevelEditorViewport.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "DisplayClusterOperator"

const FName FDisplayClusterOperatorModule::OperatorPanelTabName = TEXT("DisplayClusterOperatorTab");

void FDisplayClusterOperatorModule::StartupModule()
{
	OperatorViewModel = MakeShared<FDisplayClusterOperatorViewModel>();
	OperatorToolBarExtensibilityManager = MakeShared<FExtensibilityManager>();

	RegisterTabSpawners();
}

void FDisplayClusterOperatorModule::ShutdownModule()
{
	UnregisterTabSpawners();
}

TSharedRef<IDisplayClusterOperatorViewModel> FDisplayClusterOperatorModule::GetOperatorViewModel()
{
	return OperatorViewModel.ToSharedRef();
}

FName FDisplayClusterOperatorModule::GetPrimaryOperatorExtensionId()
{
	return SDisplayClusterOperatorPanel::PrimaryTabExtensionId;
}

FName FDisplayClusterOperatorModule::GetAuxilliaryOperatorExtensionId()
{
	return SDisplayClusterOperatorPanel::AuxilliaryTabExtensionId;
}

void FDisplayClusterOperatorModule::GetRootActorLevelInstances(TArray<ADisplayClusterRootActor*>& OutRootActorInstances)
{
	UWorld* World = GCurrentLevelEditingViewportClient ? GCurrentLevelEditingViewportClient->GetWorld() : nullptr;
	if (World)
	{
		for (TActorIterator<ADisplayClusterRootActor> Iter(World); Iter; ++Iter)
		{
			ADisplayClusterRootActor* RootActor = *Iter;
			if (IsValid(RootActor))
			{
				OutRootActorInstances.Add(RootActor);
			}
		}
	}
}

void FDisplayClusterOperatorModule::ShowDetailsForObject(UObject* Object)
{
	TArray<UObject*> Objects;
	Objects.Add(Object);
	ShowDetailsForObjects(Objects);
}

void FDisplayClusterOperatorModule::ShowDetailsForObjects(const TArray<UObject*>& Objects)
{
	DetailObjectsChanged.Broadcast(Objects);
}

void FDisplayClusterOperatorModule::ForceDismissDrawers()
{
	if (ActiveOperatorPanel.IsValid())
	{
		ActiveOperatorPanel.Pin()->ForceDismissDrawers();
	}
}

void FDisplayClusterOperatorModule::RegisterTabSpawners()
{
	// Register the nDisplay operator panel tab spawner
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(OperatorPanelTabName, FOnSpawnTab::CreateRaw(this, &FDisplayClusterOperatorModule::SpawnOperatorPanelTab))
		.SetDisplayName(LOCTEXT("TabDisplayName", "nDisplay Operator"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Open the nDisplay Operator tab."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorVirtualProductionCategory());
}

void FDisplayClusterOperatorModule::UnregisterTabSpawners()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(OperatorPanelTabName);
}

TSharedRef<SDockTab> FDisplayClusterOperatorModule::SpawnOperatorPanelTab(const FSpawnTabArgs& SpawnTabArgs)
{
	const TSharedRef<SDockTab> MajorTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.OnTabClosed_Raw(this, &FDisplayClusterOperatorModule::OnOperatorPanelTabClosed);

	MajorTab->SetContent(SAssignNew(ActiveOperatorPanel, SDisplayClusterOperatorPanel, OperatorViewModel->CreateTabManager(MajorTab), SpawnTabArgs.GetOwnerWindow()));

	return MajorTab;
}

void FDisplayClusterOperatorModule::OnOperatorPanelTabClosed(TSharedRef<SDockTab> Tab)
{
	OperatorViewModel->ResetTabManager();
	OperatorViewModel->SetRootActor(nullptr);

	ActiveOperatorPanel.Reset();
}

IMPLEMENT_MODULE(FDisplayClusterOperatorModule, DisplayClusterOperator);

#undef LOCTEXT_NAMESPACE
