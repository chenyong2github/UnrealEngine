// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

/**
 *	IDMXProtocolDevice - Physical Device. Holds the configuration for ports, ip, mac addresses, etc.
 */
class DMXPROTOCOL_API IDMXProtocolDevice
{
public:
	virtual ~IDMXProtocolDevice() {}

	virtual TSharedPtr<FJsonObject> GetSettings() const = 0;
	virtual TWeakPtr<IDMXProtocolInterface> GetCachedProtocolInterface() const = 0;
	virtual IDMXProtocol* GetProtocol() const = 0;
	virtual uint32 GetDeviceID() const = 0;

	virtual bool AllowLooping() const = 0;
};
