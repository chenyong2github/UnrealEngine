// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "UObject/ScriptInterface.h"
#include "DisplayClusterEnums.h"


struct FDisplayClusterClusterEvent;
class IDisplayClusterClusterEventListener;
class IDisplayClusterClusterSyncObject;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnClusterEvent, const FDisplayClusterClusterEvent& /* Event */);
typedef FOnClusterEvent::FDelegate FOnClusterEventListener;


/**
 * Public cluster manager interface
 */
class IDisplayClusterClusterManager
{
public:
	virtual ~IDisplayClusterClusterManager() = 0
	{ }

	virtual bool IsMaster()         const = 0;
	virtual bool IsSlave()          const = 0;
	virtual bool IsStandalone()     const = 0;
	virtual bool IsCluster()        const = 0;
	virtual FString GetNodeId()     const = 0;
	virtual uint32 GetNodesAmount() const = 0;

	virtual void RegisterSyncObject(IDisplayClusterClusterSyncObject* pSyncObj, EDisplayClusterSyncGroup SyncGroup) = 0;
	virtual void UnregisterSyncObject(IDisplayClusterClusterSyncObject* pSyncObj) = 0;

	virtual void AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) = 0;
	virtual void RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) = 0;

	virtual void AddClusterEventListener(const FOnClusterEventListener& Listener) = 0;
	virtual void RemoveClusterEventListener(const FOnClusterEventListener& Listener) = 0;

	virtual void EmitClusterEvent(const FDisplayClusterClusterEvent& Event, bool MasterOnly) = 0;
};
