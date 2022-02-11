// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IConcertSyncServer;

struct FConcertComponentInitParams
{
	TSharedRef<IConcertSyncServer> Server;
};

/** Provides the base interface for elements in the concert server UI. */
class IConcertComponent
{
public:

	virtual ~IConcertComponent() = default;

	/** Initialises the component, e.g. registering tab spawners, etc. */
	virtual void Init(const FConcertComponentInitParams& Params) {}
};
