// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SessionTabs/Archived/ArchivedConcertSessionTab.h"

#include "ArchivedSessionHistoryController.h"
#include "Session/History/SEditableSessionHistory.h"
#include "Session/History/SSessionHistory.h"
#include "Widgets/SessionTabs/Archived/SConcertArchivedSessionInspector.h"
#include "Widgets/StatusBar/SConcertStatusBar.h"

FArchivedConcertSessionTab::FArchivedConcertSessionTab(FGuid InspectedSessionID, TSharedRef<IConcertSyncServer> SyncServer, TAttribute<TSharedRef<SWindow>> ConstructUnderWindow)
	: FConcertSessionTabBase(SyncServer)
	, InspectedSessionID(MoveTemp(InspectedSessionID))
	, SyncServer(MoveTemp(SyncServer))
	, ConstructUnderWindow(MoveTemp(ConstructUnderWindow))
{}

FGuid FArchivedConcertSessionTab::GetSessionID() const
{
	return InspectedSessionID;
}

void FArchivedConcertSessionTab::CreateDockContent(const TSharedRef<SDockTab>& InDockTab)
{
	SEditableSessionHistory::FMakeSessionHistory MakeSessionHistory = SEditableSessionHistory::FMakeSessionHistory::CreateLambda([this](SSessionHistory::FArguments Arguments)
	{
		checkf(!HistoryController.IsValid(), TEXT("Called more than once"));
		HistoryController = MakeShared<FArchivedSessionHistoryController>(InspectedSessionID, SyncServer, MoveTemp(Arguments));
		return HistoryController->GetSessionHistory();
	});
	
	InDockTab->SetContent(
		SNew(SConcertArchivedSessionInspector)
			.ConstructUnderMajorTab(InDockTab)
			.ConstructUnderWindow(ConstructUnderWindow.Get())
			.MakeSessionHistory(MoveTemp(MakeSessionHistory))
			.DeleteActivity_Raw(this, &FArchivedConcertSessionTab::OnRequestDeleteActivity)
			.CanDeleteActivity_Raw(this, &FArchivedConcertSessionTab::CanDeleteActivity)
			.StatusBar()
			[
				SNew(SConcertStatusBar, *GetTabId())
			]
		);
}

void FArchivedConcertSessionTab::OnRequestDeleteActivity(const TSharedRef<FConcertSessionActivity>& DeleteActivity) const
{
	// TODO:
}

bool FArchivedConcertSessionTab::CanDeleteActivity(const TSharedRef<FConcertSessionActivity>& DeleteActivity) const
{
	// TODO:
	return false;
}
