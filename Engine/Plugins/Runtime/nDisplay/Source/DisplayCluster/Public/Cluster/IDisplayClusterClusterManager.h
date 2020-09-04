// Copyright Epic Games, Inc. All Rights Reserved.

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

	/** Returns true if current node is master. */
	virtual bool IsMaster()         const = 0;
	/** Returns true if current node is slave. */
	virtual bool IsSlave()          const = 0;
	/** Returns true if current operation mode is standalone. */
	virtual bool IsStandalone()     const = 0;
	/** Returns true if current operation mode is cluster. */
	virtual bool IsCluster()        const = 0;
	/** Returns current cluster node ID. */
	virtual FString GetNodeId()     const = 0;
	/** Returns amount of cluster nodes in the cluster. */
	virtual uint32 GetNodesAmount() const = 0;


	/** Registers object to synchronize. */
	virtual void RegisterSyncObject(IDisplayClusterClusterSyncObject* SyncObj, EDisplayClusterSyncGroup SyncGroup) = 0;
	/** Unregisters synchronization object. */
	virtual void UnregisterSyncObject(IDisplayClusterClusterSyncObject* SyncObj) = 0;

	/** Registers cluster event listener. */
	virtual void AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) = 0;
	/** Unregisters cluster event listener. */
	virtual void RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) = 0;

	/** Registers cluster event listener. */
	virtual void AddClusterEventListener(const FOnClusterEventListener& Listener) = 0;
	/** Unregisters cluster event listener. */
	virtual void RemoveClusterEventListener(const FOnClusterEventListener& Listener) = 0;

	/** Emits cluster event. */
	virtual void EmitClusterEvent(const FDisplayClusterClusterEvent& Event, bool MasterOnly) = 0;
};
