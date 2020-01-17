// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

/**
 * Port interface
 *
 * Ports represent a single universe of DMX512. They are either input (receive
 * DMX) or output (send DMX) but not both.
 *
 * Every port is part of a Device. Ports can be associated (patched) to a
 * universe.
 */
class DMXPROTOCOL_API IDMXProtocolPort
{
public:
	virtual ~IDMXProtocolPort() {}

	virtual IDMXProtocol* GetProtocol() const = 0;

	virtual uint8 GetPortID() const = 0;
	virtual uint8 GetPriority() const = 0;
	virtual void SetPriotiy(uint8 InPriority) = 0;
	virtual bool IsSupportRDM() const = 0;
	virtual TWeakPtr<IDMXProtocolDevice> GetCachedDevice() const = 0;
	virtual TWeakPtr<IDMXProtocolUniverse> GetCachedUniverse() const = 0;
	virtual void SetUniverse(const TSharedPtr<IDMXProtocolUniverse>& InUniverse) = 0;
	virtual EDMXPortCapability GetPortCapability() const = 0;
	virtual EDMXPortDirection GetPortDirection() const = 0;
	virtual bool WriteDMX(const TSharedPtr<FDMXBuffer>& DMXBuffer) = 0;
	virtual bool ReadDMX() = 0;
	virtual TSharedPtr<FJsonObject> GetSettings() const = 0;
	virtual uint16 GetUniverseID() const = 0;
	virtual uint8 GetPortAddress() const { return 0x00; };
};
