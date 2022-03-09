// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ServerSessionHistoryController.h"

class IConcertSyncServer;
class FServerSessionHistoryController;
class IConcertServerSession;
class SDockTab;
class SWindow;

/**
 * Manages the tab that contains the UI for a session.
 * It has access to the controllers and views needed for displaying a session.
 */
class FConcertServerSessionTab
{
public:

	FConcertServerSessionTab(TSharedRef<IConcertServerSession> InspectedSession, TSharedRef<IConcertSyncServer> SyncServer, const TSharedRef<SWindow>& ConstructUnderWindow);

	/** Opens or draws attention to the given tab */
	void OpenSessionTab() const;

private:

	/** The session being inspected */
	const TSharedRef<IConcertServerSession> InspectedSession;
	
	const TSharedRef<FServerSessionHistoryController> SessionHistoryController;
	
	/** The tab containing the UI for InspectedSession */
	const TSharedRef<SDockTab> DockTab;


	/** Creates a tab widget */
	TSharedRef<SDockTab> CreateTab(const TSharedRef<SWindow>& ConstructUnderWindow) const;
	
	/** Generates a tab ID for FTabManager::InsertNewDocumentTab */
	static FString GetTabPlayerHolderId(const TSharedRef<IConcertServerSession>& InspectedSession);
};
