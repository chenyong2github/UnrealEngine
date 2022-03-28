// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveConcertSessionTab.h"

#include "IConcertSession.h"
#include "PackageViewer/ConcertSessionPackageViewerController.h"
#include "SConcertSessionInspector.h"
#include "Widgets/StatusBar/SConcertStatusBar.h"

#include "Widgets/Docking/SDockTab.h"

FLiveConcertSessionTab::FLiveConcertSessionTab(TSharedRef<IConcertServerSession> InspectedSession, TSharedRef<IConcertSyncServer> SyncServer, TAttribute<TSharedRef<SWindow>> ConstructUnderWindow)
	: FConcertSessionTabBase(SyncServer)
	, InspectedSession(MoveTemp(InspectedSession))
	, ConstructUnderWindow(MoveTemp(ConstructUnderWindow))
	, SessionHistoryController(MakeShared<FServerSessionHistoryController>(InspectedSession, SyncServer))
	, PackageViewerController(MakeShared<FConcertSessionPackageViewerController>(InspectedSession, SyncServer))
{}

FGuid FLiveConcertSessionTab::GetSessionID() const
{
	return InspectedSession->GetSessionInfo().SessionId;
}

void FLiveConcertSessionTab::CreateDockContent(const TSharedRef<SDockTab>& InDockTab)
{
	const SConcertSessionInspector::FRequiredArgs WidgetArgs
	{
		InDockTab,
		ConstructUnderWindow.Get(),
		SessionHistoryController->GetSessionHistory(),
		PackageViewerController->GetPackageViewer()
	};
	InDockTab->SetContent(
		SNew(SConcertSessionInspector, WidgetArgs)
			.StatusBar()
			[
				SNew(SConcertStatusBar, *GetTabId())
			]
		);
}

void FLiveConcertSessionTab::OnOpenTab()
{
	SessionHistoryController->ReloadActivities();
	PackageViewerController->ReloadActivities();
}

