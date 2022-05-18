// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertClientsTabController.h"

#include "SConcertClientsTabView.h"
#include "Window/ConcertServerTabs.h"
#include "Window/ConcertServerWindowController.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

void FConcertClientsTabController::Init(const FConcertComponentInitParams& Params)
{
	FGlobalTabmanager::Get()->RegisterTabSpawner(
				ConcertServerTabs::GetClientsTabID(),
				FOnSpawnTab::CreateRaw(this, &FConcertClientsTabController::SpawnClientsTab, Params.WindowController->GetRootWindow(), Params.Server)
			)
			.SetDisplayName(LOCTEXT("ClientsTabTitle", "Clients"))
			.SetTooltipText(LOCTEXT("ClientsTooltipText", "View network statistics for connected clients.")
		);
	Params.MainStack->AddTab(ConcertServerTabs::GetClientsTabID(), ETabState::OpenedTab);
}

TSharedRef<SDockTab> FConcertClientsTabController::SpawnClientsTab(const FSpawnTabArgs& Args, TSharedPtr<SWindow> RootWindow, TSharedRef<IConcertSyncServer> Server)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.Label(LOCTEXT("ClientsTabTitle", "Clients"))
		.TabRole(MajorTab)
		.CanEverClose(false);
	DockTab->SetContent(
		SNew(SConcertClientsTabView, ConcertServerTabs::GetClientsTabID(), Server)
			.ConstructUnderMajorTab(DockTab)
			.ConstructUnderWindow(RootWindow)
		);
	return DockTab;
}

#undef LOCTEXT_NAMESPACE