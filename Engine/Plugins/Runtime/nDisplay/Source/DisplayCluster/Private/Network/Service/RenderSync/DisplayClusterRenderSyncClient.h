// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Network/DisplayClusterClient.h"
#include "Network/Protocol/IDisplayClusterProtocolRenderSync.h"
#include "Network/Packet/DisplayClusterPacketInternal.h"


/**
 * Rendering synchronization TCP client
 */
class FDisplayClusterRenderSyncClient
	: public FDisplayClusterClient<FDisplayClusterPacketInternal, true>
	, public IDisplayClusterProtocolRenderSync
{
public:
	FDisplayClusterRenderSyncClient();
	FDisplayClusterRenderSyncClient(const FString& InName);

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolRenderSync
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void WaitForSwapSync() override;
};
