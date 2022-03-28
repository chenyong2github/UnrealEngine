// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SessionTabs/Archived/ArchivedConcertSessionTab.h"

#include "ArchivedSessionHistoryController.h"
#include "Session/History/SSessionHistory.h"
#include "Widgets/SessionTabs/Archived/SConcertArchivedSessionInspector.h"
#include "Widgets/StatusBar/SConcertStatusBar.h"

FArchivedConcertSessionTab::FArchivedConcertSessionTab(FGuid InspectedSessionID, TSharedRef<IConcertSyncServer> SyncServer, TAttribute<TSharedRef<SWindow>> ConstructUnderWindow)
	: FConcertSessionTabBase(SyncServer)
	, InspectedSessionID(MoveTemp(InspectedSessionID))
	, ConstructUnderWindow(MoveTemp(ConstructUnderWindow))
	, HistoryController(MakeShared<FArchivedSessionHistoryController>(MoveTemp(InspectedSessionID), MoveTemp(SyncServer)))
{}

FGuid FArchivedConcertSessionTab::GetSessionID() const
{
	return InspectedSessionID;
}

void FArchivedConcertSessionTab::CreateDockContent(const TSharedRef<SDockTab>& InDockTab)
{
	InDockTab->SetContent(
		SNew(SConcertArchivedSessionInspector, SConcertArchivedSessionInspector::FRequiredArgs(InDockTab, ConstructUnderWindow.Get(), HistoryController->GetSessionHistory()))
			.StatusBar()
			[
				SNew(SConcertStatusBar, *GetTabId())
			]
		);
}

void FArchivedConcertSessionTab::OnOpenTab()
{}
