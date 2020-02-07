// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXProtocolCommon.h"

/**
 */
class IDMXNetworkInterface
{
public:
	virtual ~IDMXNetworkInterface() {}

	/**
	 * Listen for interface IP changes
	 * @param InInterfaceIPAddress			String IP address
	 */
	virtual void OnNetworkInterfaceChanged(const FString& InInterfaceIPAddress) = 0;

	/**
	 * Restart sockets and listeners
	 * @param InInterfaceIPAddress			String IP address
	 * @param OutErrorMessage				String error message
	 */
	virtual bool RestartNetworkInterface(const FString& InInterfaceIPAddress, FString& OutErrorMessage) = 0;

	/**  Release network interfaces */
	virtual void ReleaseNetworkInterface() = 0;
};