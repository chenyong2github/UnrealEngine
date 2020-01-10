// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

#include "DisplayClusterUtils/DisplayClusterCommonHelpers.h"


namespace DisplayClusterHelpers
{
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Network helpers
	//////////////////////////////////////////////////////////////////////////////////////////////
	namespace net
	{
		static bool GenIPv4Endpoint(const FString& addr, const int32 port, FIPv4Endpoint& ep)
		{
			FIPv4Address ipAddr;
			if (!FIPv4Address::Parse(addr, ipAddr))
				return false;

			ep = FIPv4Endpoint(ipAddr, port);
			return true;
		}
	};
};
