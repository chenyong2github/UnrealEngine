// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SessionTabs/Archived/ArchivedConcertSessionTab.h"

#include "Widgets/SessionTabs/Archived/SConcertArchivedSessionInspector.h"
#include "Widgets/StatusBar/SConcertStatusBar.h"

FArchivedConcertSessionTab::FArchivedConcertSessionTab(const FGuid& InspectedSessionID, TSharedRef<IConcertSyncServer> SyncServer, TAttribute<TSharedRef<SWindow>> ConstructUnderWindow)
	: FAbstractConcertSessionTab(SyncServer)
	, InspectedSessionID(InspectedSessionID)
	, ConstructUnderWindow(ConstructUnderWindow)
{}

FGuid FArchivedConcertSessionTab::GetSessionID() const
{
	return InspectedSessionID;
}

void FArchivedConcertSessionTab::CreateDockContent(const TSharedRef<SDockTab>& InDockTab)
{
	InDockTab->SetContent(
		SNew(SConcertArchivedSessionInspector, SConcertArchivedSessionInspector::FRequiredArgs(InDockTab, ConstructUnderWindow.Get()))
			.StatusBar()
			[
				SNew(SConcertStatusBar, *GetTabId())
			]
		);
}

void FArchivedConcertSessionTab::OnOpenTab()
{}
