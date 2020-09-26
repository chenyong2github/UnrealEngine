// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Network/IDisplayClusterClient.h"
#include "Network/Transport/DisplayClusterSocketOperations.h"
#include "Network/Transport/DisplayClusterSocketOperationsHelper.h"

#include "Misc/DisplayClusterConstants.h"


/**
 * Base DisplayCluster TCP client
 */
class FDisplayClusterClientBase
	: public IDisplayClusterClient
	, public FDisplayClusterSocketOperations
{
public:
	FDisplayClusterClientBase(const FString& InName)
		: FDisplayClusterSocketOperations(CreateSocket(InName), DisplayClusterConstants::net::PacketBufferSize, InName)
	{ }

	virtual ~FDisplayClusterClientBase()
	{
		Disconnect();
	}

public:
	// Connects to a server
	bool Connect(const FString& Address, const int32 Port, const int32 TriesAmount, const float TryDelay);
	// Terminates current connection
	void Disconnect();

	// Provides with net unit name
	virtual FString GetName() const override
	{
		return GetConnectionName();
	}

	virtual bool IsConnected() const override
	{
		return IsOpen();
	}

protected:
	// Creates client socket
	FSocket* CreateSocket(const FString& InName);
};


template <typename TPacketType, bool bExitOnCommError>
class FDisplayClusterClient
	: public    FDisplayClusterClientBase
	, protected FDisplayClusterSocketOperationsHelper<TPacketType, bExitOnCommError>
{
public:
	FDisplayClusterClient(const FString& InName)
		: FDisplayClusterClientBase(InName)
		, FDisplayClusterSocketOperationsHelper<TPacketType, bExitOnCommError>(*this, InName)
	{ }
};
