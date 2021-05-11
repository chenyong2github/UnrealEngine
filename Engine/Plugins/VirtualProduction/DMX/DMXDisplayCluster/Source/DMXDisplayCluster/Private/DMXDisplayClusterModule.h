// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "IDisplayCluster.h"
#include "DMXProtocolTypes.h"
#include "IO/DMXPortManager.h"
#include "IO/DMXInputPort.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "Cluster/DisplayClusterClusterEvent.h"


class DMXDISPLAYCLUSTER_API FDMXDisplayClusterModule : public IModuleInterface
{

public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

protected:

	/** Delegate for DisplayCluster DMX events */
	void OnClusterEventReceived(const FDisplayClusterClusterEventBinary& Event);

	/** Listen for DisplayCluster DMX events */
	FOnClusterEventBinaryListener BinaryListener;

	/** Setup delegates for DisplayCluster */
	void SetupDisplayCluster();

	/** Forward a DMX signal to all of the configured DMX Slaves */
	void SendToSlaves(FDMXInputPortSharedRef DMXInputPortRef, FDMXSignalSharedRef DMXSignalRef);

	/** Force the node to be a DMX Master */
	bool bForceMaster;

	/** Force the node to be a DMX Slave */
	bool bForceSlave;

	/** Automatically map Master/Slave configuration to the DisplayCluster one */
	bool bAutoMode;
};
