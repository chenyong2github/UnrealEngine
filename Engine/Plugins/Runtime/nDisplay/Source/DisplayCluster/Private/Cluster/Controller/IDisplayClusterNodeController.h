// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Network/Protocol/IDisplayClusterProtocolEventsJson.h"
#include "Network/Protocol/IDisplayClusterProtocolEventsBinary.h"
#include "Network/Protocol/IDisplayClusterProtocolClusterSync.h"
#include "Network/Protocol/IDisplayClusterProtocolRenderSync.h"

#include "DisplayClusterEnums.h"


/**
 * Node controller interface
 */
class IDisplayClusterNodeController
	: public IDisplayClusterProtocolEventsJson
	, public IDisplayClusterProtocolEventsBinary
	, public IDisplayClusterProtocolClusterSync
	, public IDisplayClusterProtocolRenderSync
{
public:
	virtual ~IDisplayClusterNodeController() = default;

public:
	/** Initialize controller instance */
	virtual bool Initialize() = 0;

	/** Release controller instance */
	virtual void Release() = 0;

	/** Clear per-frame cache */
	virtual void ClearCache()
	{ }

	/** Returns cluster role of the controller */
	virtual EDisplayClusterNodeRole GetClusterRole() const = 0;

	/** Return node ID */
	virtual FString GetNodeId() const = 0;

	/** Return controller name */
	virtual FString GetControllerName() const = 0;

	/** Send binary event to a specific target outside of the cluster */
	virtual void SendClusterEventTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly)
	{ }

	/** Send JSON event to a specific target outside of the cluster */
	virtual void SendClusterEventTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventJson& Event, bool bMasterOnly)
	{ }
};
