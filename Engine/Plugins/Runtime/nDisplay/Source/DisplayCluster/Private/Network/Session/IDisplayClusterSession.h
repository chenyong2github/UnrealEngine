// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


/**
 * Session interface
 */
class IDisplayClusterSession
{
public:
	virtual ~IDisplayClusterSession() = default;

public:
	// Start processing of incoming events
	virtual bool StartSession() = 0;
	// Stop  processing of incoming events
	virtual void StopSession() = 0;

	// Session name
	virtual FString GetName() const = 0;
	// Session ID
	virtual uint64 GetSessionId() const = 0;
};
