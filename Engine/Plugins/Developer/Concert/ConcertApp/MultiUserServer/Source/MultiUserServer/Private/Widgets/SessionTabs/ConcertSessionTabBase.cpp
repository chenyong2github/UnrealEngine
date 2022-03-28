// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertSessionTabBase.h"

#include "IConcertServer.h"
#include "IConcertSyncServer.h"
#include "Widgets/ConcertServerTabs.h"

#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"

namespace UE::ConcertServerUI::Private
{
	static TOptional<FString> GetSessionName(const TSharedRef<IConcertSyncServer>& SyncServer, const FGuid SessionId)
	{
		if (const TSharedPtr<IConcertServerSession> LiveSessionInfo = SyncServer->GetConcertServer()->GetLiveSession(SessionId))
		{
			return LiveSessionInfo->GetSessionInfo().SessionName;
		}
		if (const TOptional<FConcertSessionInfo> ArchivedSessionInfo = SyncServer->GetConcertServer()->GetArchivedSessionInfo(SessionId))
		{
			return ArchivedSessionInfo->SessionName;
		}
		return {};
	}
}

FConcertSessionTabBase::FConcertSessionTabBase(TSharedRef<IConcertSyncServer> SyncServer)
	: SyncServer(MoveTemp(SyncServer))
{}

void FConcertSessionTabBase::OpenSessionTab()
{
	const TSharedRef<FGlobalTabmanager>& TabManager = FGlobalTabmanager::Get();
	const FTabId TabId { *GetTabId() };

	EnsureInitDockTab();
	if (TabManager->FindExistingLiveTab(TabId))
	{
		TabManager->DrawAttention(DockTab.ToSharedRef());
	}
	else
	{
		const FTabManager::FLastMajorOrNomadTab Search(ConcertServerTabs::GetSessionBrowserTabId());
		FGlobalTabmanager::Get()->InsertNewDocumentTab(*GetTabId(), Search, DockTab.ToSharedRef());

		OnOpenTab();
	}
}

void FConcertSessionTabBase::EnsureInitDockTab()
{
	if (!DockTab.IsValid())
	{
		const TOptional<FString> SessionName = UE::ConcertServerUI::Private::GetSessionName(SyncServer, GetSessionID());
		check(SessionName);
		
		const FText Title = FText::FromString(*SessionName);
		DockTab = SNew(SDockTab)
			.Label(Title)
			.TabRole(MajorTab);
		
		CreateDockContent(DockTab.ToSharedRef());
	}
}
