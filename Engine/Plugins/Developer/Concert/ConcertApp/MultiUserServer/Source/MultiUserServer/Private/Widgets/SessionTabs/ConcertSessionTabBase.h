// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IConcertSyncServer;
class SDockTab;
class SWindow;

/** Shared functionality for a tab that manages a session */
class FConcertSessionTabBase
{
public:

    FConcertSessionTabBase(TSharedRef<IConcertSyncServer> SyncServer);
	virtual ~FConcertSessionTabBase() = default;
	
	/** Opens or draws attention to the given tab */
	void OpenSessionTab();
	
protected:

	virtual FGuid GetSessionID() const = 0;
	virtual void CreateDockContent(const TSharedRef<SDockTab>& DockTab) = 0;
	virtual void OnOpenTab() = 0;
	
	/** Generates a tab ID for FTabManager::InsertNewDocumentTab */
	FString GetTabId() const { return GetSessionID().ToString(); }
	const TSharedRef<IConcertSyncServer>& GetSyncServer() const { return SyncServer; }
	
private:

	/** Used to look up session name */
	const TSharedRef<IConcertSyncServer> SyncServer;
	
	/** The tab containing the UI for InspectedSession */
	TSharedPtr<SDockTab> DockTab;

	/** Inits DockTab if it has not yet been inited */
	void EnsureInitDockTab();
};
