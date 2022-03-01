// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"

class FConcertServerSessionTab;
class IConcertServerSession;
class IConcertSyncServer;
class FConcertServerSessionBrowserController;
class FOutputLogController;
class SWindow;

struct FConcertServerWindowInitParams
{
	/** The server that the window is supposed to manage */
	TSharedRef<IConcertSyncServer> Server;
	
	/** Config path for server layout ini */
	FString MultiUserServerLayoutIni;
	
	FConcertServerWindowInitParams(TSharedRef<IConcertSyncServer> Server, FString MultiUserServerLayoutIni = FString())
		: Server(Server)
		, MultiUserServerLayoutIni(MoveTemp(MultiUserServerLayoutIni))
	{}
};

/** Responsible for creating the Slate window for the server. Implements controller in the model-view-controller pattern. */
class FConcertServerWindowController : public TSharedFromThis<FConcertServerWindowController>
{
public:
	
	FConcertServerWindowController(const FConcertServerWindowInitParams& Params);
	void CreateWindow();

	/** Opens or draws attention to the tab for the given live session ID */
	void OpenSessionTab(const FGuid& SessionId);
	
private:

	/** The ini file to use for saving the layout */
	FString MultiUserServerLayoutIni;
	/** Holds the current layout for saving later. */
	TSharedPtr<FTabManager::FLayout> PersistentLayout;

	TSharedPtr<IConcertSyncServer> ServerInstance;

	/** The main window being managed */
	TSharedPtr<SWindow> RootWindow;
	TMap<FGuid, TSharedRef<FConcertServerSessionTab>> RegisteredSessions;
	
	/** Manages the session browser */
	TSharedPtr<FConcertServerSessionBrowserController> SessionBrowserController;
	
	void InitComponents();

	/** Gets the manager for a session tab if the session ID is valid */
	TSharedPtr<FConcertServerSessionTab> GetOrRegisterSessionTab(const FGuid& SessionId);

	void OnWindowClosed(const TSharedRef<SWindow>& Window);
	void SaveLayout() const;
};
