// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Network/Protocol/IDisplayClusterProtocolEventsJson.h"
#include "Network/Protocol/IDisplayClusterProtocolEventsBinary.h"
#include "Network/Protocol/IDisplayClusterProtocolClusterSync.h"
#include "Network/Protocol/IDisplayClusterProtocolRenderSync.h"


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
	virtual ~IDisplayClusterNodeController()
	{ }

public:
	virtual bool Initialize() = 0;
	virtual void Release() = 0;

	virtual void ClearCache()
	{ }

	virtual bool IsMaster() const = 0;
	virtual bool IsSlave() const = 0;
	virtual FString GetNodeId() const = 0;
	virtual FString GetControllerName() const = 0;
};
