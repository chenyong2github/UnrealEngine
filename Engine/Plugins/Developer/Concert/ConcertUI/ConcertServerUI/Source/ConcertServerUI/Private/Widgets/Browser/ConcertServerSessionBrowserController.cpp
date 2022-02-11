// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertServerSessionBrowserController.h"

#include "ConcertServerStyle.h"
#include "SConcertServerSessionBrowser.h"
#include "Widgets/ConcertServerTabs.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

void FConcertServerSessionBrowserController::Init(const FConcertComponentInitParams& Params)
{
	FGlobalTabmanager::Get()->RegisterTabSpawner(
			ConcertServerTabs::GetSessionBrowserTabId(),
			FOnSpawnTab::CreateRaw(this, &FConcertServerSessionBrowserController::SpawnSessionBrowserTab)
		)
		.SetDisplayName(LOCTEXT("SessionBrowserTabTitle", "Session Browser"))
		.SetTooltipText(LOCTEXT("SessionBrowserTooltipText", "A section to browse, start, archive, and restore server sessions."))
		.SetIcon(FSlateIcon(FConcertServerStyle::GetStyleSetName(), TEXT("Concert.MultiUser")));
}

TSharedRef<SDockTab> FConcertServerSessionBrowserController::SpawnSessionBrowserTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.Label(LOCTEXT("SessionBrowserTabTitle", "Sessions"))
		.TabRole(MajorTab)
		[
			SAssignNew(ConcertBrowser, SConcertServerSessionBrowser, SharedThis(this))
		];

	FGlobalTabmanager::Get()->SetMainTab(DockTab);
	return DockTab;
}

#undef LOCTEXT_NAMESPACE 
