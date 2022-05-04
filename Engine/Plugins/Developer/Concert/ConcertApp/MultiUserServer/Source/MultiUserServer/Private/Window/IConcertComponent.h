// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"

class FConcertServerWindowController;
class IConcertSyncServer;

struct FConcertComponentInitParams
{
	/** The server being managed */
	TSharedRef<IConcertSyncServer> Server;

	/** Manages the server slate application */
	TSharedRef<FConcertServerWindowController> WindowController;

	/** The root area of the main window layout. Add tabs to this. */
	TSharedRef<FTabManager::FArea> MainWindowArea;

	FConcertComponentInitParams(TSharedRef<IConcertSyncServer> Server, TSharedRef<FConcertServerWindowController> WindowController, TSharedRef<FTabManager::FArea> MainWindowArea)
		: Server(MoveTemp(Server))
		, WindowController(MoveTemp(WindowController))
		, MainWindowArea(MoveTemp(MainWindowArea))
	{}
};

/** Provides the base interface for elements in the concert server UI. */
class IConcertComponent
{
public:

	virtual ~IConcertComponent() = default;

	/** Initialises the component, e.g. registering tab spawners, etc. */
	virtual void Init(const FConcertComponentInitParams& Params) {}
};
