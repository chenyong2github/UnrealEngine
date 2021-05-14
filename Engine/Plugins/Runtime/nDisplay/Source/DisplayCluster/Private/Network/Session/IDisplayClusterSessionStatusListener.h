// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


/**
 * Session status listener interface
 */
class IDisplayClusterSessionStatusListener
{
public:
	virtual ~IDisplayClusterSessionStatusListener() = default;

public:
	// Called when a session has started
	virtual void NotifySessionOpen(uint64 SessionId)
	{ }

	// Called when a session is going to be closed
	virtual void NotifySessionClose(uint64 SessionId)
	{ }
};
