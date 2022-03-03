// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertServerSessionTab.h"

#include "IConcertSession.h"
#include "Widgets/ConcertServerTabs.h"
#include "Widgets/StatusBar/SConcertStatusBar.h"
#include "Widgets/Session/SConcertSessionInspector.h"

FConcertServerSessionTab::FConcertServerSessionTab(TSharedRef<IConcertServerSession> InspectedSession, const TSharedRef<SWindow>& ConstructUnderWindow)
	: InspectedSession(MoveTemp(InspectedSession))
	, DockTab(CreateTab(InspectedSession, ConstructUnderWindow))
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

TSharedRef<SDockTab> FConcertServerSessionTab::CreateTab(const TSharedRef<IConcertServerSession>& InspectedSession, const TSharedRef<SWindow>& ConstructUnderWindow)
{
	const FText Title = FText::FromString(InspectedSession->GetSessionInfo().SessionName);
	TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.Label(Title)
		.TabRole(MajorTab);
	DockTab->SetContent(
		SNew(SConcertSessionInspector, DockTab, ConstructUnderWindow)
			.StatusBar()
			[
				SNew(SConcertStatusBar, *GetTabPlayerHolderId(InspectedSession))
			]
		);
	return DockTab;
}

FString FConcertServerSessionTab::GetTabPlayerHolderId(const TSharedRef<IConcertServerSession>& InspectedSession)
{
	return *InspectedSession->GetSessionInfo().SessionId.ToString();
}
