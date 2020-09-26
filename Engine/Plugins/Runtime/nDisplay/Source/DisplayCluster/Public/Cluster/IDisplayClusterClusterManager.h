// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "UObject/ScriptInterface.h"
#include "DisplayClusterEnums.h"

// Deprecated
struct FDisplayClusterClusterEvent;
class IDisplayClusterClusterEventListener;

struct FDisplayClusterClusterEventJson;
struct FDisplayClusterClusterEventBinary;
class IDisplayClusterClusterEventJsonListener;
class IDisplayClusterClusterEventBinaryListener;
class IDisplayClusterClusterSyncObject;

// Deprecated
DECLARE_MULTICAST_DELEGATE_OneParam(FOnClusterEvent, const FDisplayClusterClusterEvent& /* Event */);
typedef FOnClusterEvent::FDelegate FOnClusterEventListener;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnClusterEventJson, const FDisplayClusterClusterEventJson& /* Event */);
typedef FOnClusterEventJson::FDelegate FOnClusterEventJsonListener;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnClusterEventBinary, const FDisplayClusterClusterEventBinary& /* Event */);
typedef FOnClusterEventBinary::FDelegate FOnClusterEventBinaryListener;


/**
 * Public cluster manager interface
 */
class IDisplayClusterClusterManager
{
public:
	virtual ~IDisplayClusterClusterManager() = 0
	{ }

	/** Returns true if current node is master. */
	virtual bool IsMaster() const = 0;
	/** Returns true if current node is slave. */
	virtual bool IsSlave()  const = 0;

	/** Returns true if current operation mode is standalone. */
	UE_DEPRECATED(4.26, "This feature is no longer supported.")
	virtual bool IsStandalone() const
	{
		return false;
	}

	/** Returns true if current operation mode is cluster. */
	UE_DEPRECATED(4.26, "This feature is no longer supported.")
	virtual bool IsCluster() const
	{
		return false;
	}

	/** Returns current cluster node ID. */
	virtual FString GetNodeId() const = 0;
	/** Returns amount of cluster nodes in the cluster. */
	virtual uint32 GetNodesAmount() const = 0;


	/** Registers object to synchronize. */
	virtual void RegisterSyncObject(IDisplayClusterClusterSyncObject* SyncObj, EDisplayClusterSyncGroup SyncGroup) = 0;
	/** Unregisters synchronization object. */
	virtual void UnregisterSyncObject(IDisplayClusterClusterSyncObject* SyncObj) = 0;

	//////////////////////////////////////////////////////////////////////////////////////////////
	// JSON cluster events
	//////////////////////////////////////////////////////////////////////////////////////////////

	/** Registers cluster event listener. */
	virtual void AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) = 0;

	/** Unregisters cluster event listener. */
	virtual void RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) = 0;

	/** Registers json cluster event listener. */
	virtual void AddClusterEventJsonListener(const FOnClusterEventJsonListener& Listener) = 0;

	/** Unregisters json cluster event listener. */
	virtual void RemoveClusterEventJsonListener(const FOnClusterEventJsonListener& Listener) = 0;

	/** Registers binary cluster event listener. */
	virtual void AddClusterEventBinaryListener(const FOnClusterEventBinaryListener& Listener) = 0;

	/** Unregisters binary cluster event listener. */
	virtual void RemoveClusterEventBinaryListener(const FOnClusterEventBinaryListener& Listener) = 0;

	/** Emits cluster event. */
	UE_DEPRECATED(4.26, "This function is deprecated. Please use EmitClusterEventJson.")
	virtual void EmitClusterEvent(const FDisplayClusterClusterEvent& Event, bool bMasterOnly)
	{ }

	/** Emits JSON cluster event. */
	virtual void EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event, bool bMasterOnly) = 0;

	/** Emits binary cluster event. */
	virtual void EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly) = 0;
};
