// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertServerSessionTab.h"

#include "Framework/Docking/TabManager.h"
#include "IConcertSession.h"
#include "Widgets/ConcertServerTabs.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/StatusBar/SConcertStatusBar.h"
#include "Widgets/Session/SConcertSessionInspector.h"

FConcertServerSessionTab::FConcertServerSessionTab(TSharedRef<IConcertServerSession> InspectedSession, TSharedRef<IConcertSyncServer> SyncServer, const TSharedRef<SWindow>& ConstructUnderWindow)
	: InspectedSession(MoveTemp(InspectedSession))
	, SessionHistoryController(MakeShared<FServerSessionHistoryController>(InspectedSession, SyncServer))
	, DockTab(CreateTab(ConstructUnderWindow))
{}

void FConcertServerSessionTab::OpenSessionTab() const
{
	const TSharedRef<FGlobalTabmanager>& TabManager = FGlobalTabmanager::Get();
	const FTabId TabId { *GetTabPlayerHolderId(InspectedSession) };
	if (TabManager->FindExistingLiveTab(TabId))
	{
		TabManager->DrawAttention(DockTab);
	}
	else
	{
		const FTabManager::FLastMajorOrNomadTab Search(ConcertServerTabs::GetSessionBrowserTabId());
		FGlobalTabmanager::Get()->InsertNewDocumentTab(*GetTabPlayerHolderId(InspectedSession), Search, DockTab);
	}
}

TSharedRef<SDockTab> FConcertServerSessionTab::CreateTab(const TSharedRef<SWindow>& ConstructUnderWindow) const
{
	const FText Title = FText::FromString(InspectedSession->GetSessionInfo().SessionName);
	TSharedRef<SDockTab> NewDockTab = SNew(SDockTab)
		.Label(Title)
		.TabRole(MajorTab);
	NewDockTab->SetContent(
		SNew(SConcertSessionInspector, SConcertSessionInspector::FRequiredArgs{NewDockTab, ConstructUnderWindow, SessionHistoryController})
			.StatusBar()
			[
				SNew(SConcertStatusBar, *GetTabPlayerHolderId(InspectedSession))
			]
		);
	return NewDockTab;
}

FString FConcertServerSessionTab::GetTabPlayerHolderId(const TSharedRef<IConcertServerSession>& InspectedSession)
{
	return *InspectedSession->GetSessionInfo().SessionId.ToString();
}
