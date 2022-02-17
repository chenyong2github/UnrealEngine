// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"

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

private:

	/** The ini file to use for saving the layout */
	FString MultiUserServerLayoutIni;
	
	/** Holds the current layout for saving later. */
	TSharedPtr<FTabManager::FLayout> PersistentLayout;

	/** Manages the session browser */
	TSharedPtr<FConcertServerSessionBrowserController> SessionBrowserController;
	
	void InitComponents(const FConcertServerWindowInitParams& WindowInitParams) const;

	void OnWindowClosed(const TSharedRef<SWindow>& Window);
	void SaveLayout() const;
};