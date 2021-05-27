// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


/**
 * DisplayCluster TCP server interface
 */
class IDisplayClusterServer
{
public:
	virtual ~IDisplayClusterServer() = default;

public:
	// Start server
	virtual bool Start(const FString& Address, int32 Port) = 0;
	// Stop server
	virtual void Shutdown() = 0;
	// Returns current server state
	virtual bool IsRunning() const = 0;
	// Returns server name
	virtual FString GetName() const = 0;
	// Returns server address
	virtual FString GetAddress() const = 0;
	// Returns server port
	virtual int32 GetPort() const = 0;
};
