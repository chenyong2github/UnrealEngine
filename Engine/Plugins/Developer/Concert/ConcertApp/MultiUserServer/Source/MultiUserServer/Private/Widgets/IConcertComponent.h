// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FConcertServerWindowController;
class IConcertSyncServer;

struct FConcertComponentInitParams
{
	/** The server being managed */
	TSharedRef<IConcertSyncServer> Server;

	/** Manages the server slate application */
	TSharedRef<FConcertServerWindowController> WindowController;

	FConcertComponentInitParams(TSharedRef<IConcertSyncServer> Server, TSharedRef<FConcertServerWindowController> WindowController)
		: Server(MoveTemp(Server))
		, WindowController(MoveTemp(WindowController))
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
