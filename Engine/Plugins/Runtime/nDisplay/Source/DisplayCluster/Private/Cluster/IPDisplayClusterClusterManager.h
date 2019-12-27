// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Timecode.h"
#include "Misc/FrameRate.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "IPDisplayClusterManager.h"

#include "Network/DisplayClusterMessage.h"

class IPDisplayClusterNodeController;
class FJsonObject;


/**
 * Cluster manager private interface
 */
class IPDisplayClusterClusterManager :
	public IDisplayClusterClusterManager,
	public IPDisplayClusterManager
{
public:
	virtual ~IPDisplayClusterClusterManager()
	{ }

	virtual IPDisplayClusterNodeController* GetController() const = 0;

	virtual void ExportSyncData(FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup) const = 0;
	virtual void ImportSyncData(const FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup) = 0;

	virtual void ExportEventsData(FDisplayClusterMessage::DataType& EventsData) const = 0;
	virtual void ImportEventsData(const FDisplayClusterMessage::DataType& EventsData) = 0;

	virtual void SyncObjects(EDisplayClusterSyncGroup SyncGroup) = 0;
	virtual void SyncInput()   = 0;
	virtual void SyncEvents()  = 0;
	
	virtual void ProvideNativeInputData(const TMap<FString, FString>& NativeInputData) = 0;
	virtual void SyncNativeInput(TMap<FString, FString>& NativeInputData) = 0;
};
