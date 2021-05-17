// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cluster/IDisplayClusterClusterManager.h"
#include "IPDisplayClusterManager.h"

#include "Misc/Timecode.h"
#include "Misc/FrameRate.h"

class IDisplayClusterNodeController;
class FJsonObject;


/**
 * Cluster manager private interface
 */
class IPDisplayClusterClusterManager :
	public IDisplayClusterClusterManager,
	public IPDisplayClusterManager
{
public:
	virtual ~IPDisplayClusterClusterManager() = default;

public:
	virtual IDisplayClusterNodeController* GetController() const = 0;

	virtual void ExportSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup) const = 0;
	virtual void ImportSyncData(const TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup) = 0;

	virtual void ExportEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& BinaryEvents) = 0;
	virtual void ImportEventsData(const TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& JsonEvents, const TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& BinaryEvents) = 0;

	virtual void SyncObjects(EDisplayClusterSyncGroup SyncGroup) = 0;
	virtual void SyncEvents() = 0;
	
	virtual void ProvideNativeInputData(const TMap<FString, FString>& NativeInputData) = 0;
	virtual void SyncNativeInput(TMap<FString, FString>& NativeInputData) = 0;
};
