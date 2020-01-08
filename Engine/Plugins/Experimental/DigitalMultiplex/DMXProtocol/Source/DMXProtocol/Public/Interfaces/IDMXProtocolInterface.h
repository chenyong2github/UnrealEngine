// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

/**
 * IO interfacing protocol interface
 */
class DMXPROTOCOL_API IDMXProtocolInterface
{
public:
	virtual ~IDMXProtocolInterface() {}

	virtual IDMXProtocol* GetProtocol() const = 0;
};

/**
 * Holds serial io parameters
 */
class DMXPROTOCOL_API IDMXProtocolInterfaceUSB
	: public IDMXProtocolInterface
{
};

/**
 * Holds network io parameters
 */
class DMXPROTOCOL_API IDMXProtocolInterfaceEthernet
	: public IDMXProtocolInterface
{
};

