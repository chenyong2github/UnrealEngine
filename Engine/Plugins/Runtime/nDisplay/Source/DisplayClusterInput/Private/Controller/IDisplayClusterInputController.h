// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


class IDisplayClusterInputController
{
public:
	virtual ~IDisplayClusterInputController()
	{ }

public:
	virtual void ProcessStartSession()
	{ }

	virtual void ProcessEndSession()
	{ }

	virtual void ProcessPreTick()
	{ }

	virtual bool HasDevice(const FString DeviceName) const = 0;
};
