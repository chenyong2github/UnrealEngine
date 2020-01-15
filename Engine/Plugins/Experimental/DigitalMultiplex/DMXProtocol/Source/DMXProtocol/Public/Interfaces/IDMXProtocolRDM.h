// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

/**
 */
class DMXPROTOCOL_API IDMXProtocolRDM
{
public:
	virtual ~IDMXProtocolRDM() {}

	/**
	 * Cross-protocol RDM command sending interface
	 * @param CMD Key->Value command to send
	 */
	virtual void SendRDMCommand(const TSharedPtr<FJsonObject>& CMD) = 0;

	/**
	 * Cross-protocol RDM discovery sending interface
	 * @param CMD Key->Value command to send
	 */
	virtual void RDMDiscovery(const TSharedPtr<FJsonObject>& CMD) = 0;
};

