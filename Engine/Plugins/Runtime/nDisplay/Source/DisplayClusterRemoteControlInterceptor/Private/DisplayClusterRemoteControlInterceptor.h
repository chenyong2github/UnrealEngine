// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRemoteControlInterceptionFeature.h"
#include "Cluster/IDisplayClusterClusterManager.h"

struct FDisplayClusterClusterEventBinary;


/**
 * Remote control interceptor feature
 */
class FDisplayClusterRemoteControlInterceptor :
	public IRemoteControlInterceptionFeatureInterceptor
{
public:
	FDisplayClusterRemoteControlInterceptor();
	virtual ~FDisplayClusterRemoteControlInterceptor();

public:
	// IRemoteControlInterceptionCommands interface
	virtual ERCIResponse SetObjectProperties(FRCIPropertiesMetadata& InProperties) override;
	virtual ERCIResponse ResetObjectProperties(FRCIObjectMetadata& InObject) override;
	// ~IRemoteControlInterceptionCommands interface

private:
	// Helper function to perform events emission
	void EmitReplicationEvent(int32 EventId, TArray<uint8>& Buffer, const FString& EventName);
	// Cluster events handler/dispatcher
	void OnClusterEventBinaryHandler(const FDisplayClusterClusterEventBinary& Event);

private:
	// Process SetObjectProperties command replication data
	void OnReplication_SetObjectProperties   (const TArray<uint8>& Buffer);
	// Process ResetObjectProperties command replication data
	void OnReplication_ResetObjectProperties (const TArray<uint8>& Buffer);

private:
	// CVar value MasterOnly
	const bool bInterceptOnMasterOnly;
	// Cluster events listener
	FOnClusterEventBinaryListener EventsListener;
};
