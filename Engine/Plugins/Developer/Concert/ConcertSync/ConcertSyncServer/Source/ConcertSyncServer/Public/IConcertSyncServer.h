// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IConcertModule.h"
#include "ConcertSyncSessionFlags.h"

class UConcertServerConfig;

/**
 * Interface for a Concert Sync Server.
 */
class IConcertSyncServer
{
public:
	virtual ~IConcertSyncServer() = default;

	/** Start this Concert Sync Server using the given config */
	virtual void Startup(const UConcertServerConfig* InServerConfig, const EConcertSyncSessionFlags InSessionFlags) = 0;

	/** Stop this Concert Sync Server */
	virtual void Shutdown() = 0;

	/** Get the current server */
	virtual IConcertServerRef GetConcertServer() const = 0;
};
