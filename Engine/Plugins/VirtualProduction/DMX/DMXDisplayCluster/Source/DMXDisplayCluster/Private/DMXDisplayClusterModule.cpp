// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXDisplayClusterModule.h"
#include "Serialization/ArrayWriter.h"
#include "Serialization/MemoryReader.h"

#define LOCTEXT_NAMESPACE "DMXDisplayClusterModule"

namespace
{
	/**
	 * Event ID for the Cluster event used to replicate DMX for nDisplay
	 *
	 * As per 4.26 there is no registry for system events, hence
	 * 0xDDDD0000 is used for DMX ndisplay replication system event by agreement 4.26
	 */
	constexpr int32 DMXnDisplayReplicationEventID = 0xDDDD0000;
}

void FDMXDisplayClusterModule::StartupModule()
{
	bForceMaster = FParse::Param(FCommandLine::Get(), TEXT("dc_dmx_master"));
	bForceSlave = FParse::Param(FCommandLine::Get(), TEXT("dc_dmx_slave"));
	bAutoMode = FParse::Param(FCommandLine::Get(), TEXT("dc_dmx"));

	IDisplayCluster::Get().OnDisplayClusterStartSession().AddRaw(this, &FDMXDisplayClusterModule::SetupDisplayCluster);
}

void FDMXDisplayClusterModule::SendToSlaves(FDMXInputPortSharedRef DMXInputPortRef, FDMXSignalSharedRef DMXSignalRef)
{
	if (IDisplayClusterClusterManager* ClusterManager = IDisplayCluster::Get().GetClusterMgr())
	{
		FArrayWriter ArrayWriter;
		DMXSignalRef->Serialize(ArrayWriter);

		FDisplayClusterClusterEventBinary ClusterEvent;
		ClusterEvent.bIsSystemEvent = true;
		ClusterEvent.bShouldDiscardOnRepeat = false;
		ClusterEvent.EventId = DMXnDisplayReplicationEventID;
		ClusterEvent.EventData = MoveTemp(ArrayWriter);
		constexpr bool bEmitFromMasterOnly = false;
		ClusterManager->EmitClusterEventBinary(ClusterEvent, bEmitFromMasterOnly);
	}
}

void FDMXDisplayClusterModule::SetupDisplayCluster()
{
	if (IDisplayClusterClusterManager* ClusterManager = IDisplayCluster::Get().GetClusterMgr())
	{
		const bool bIsSlave = bForceSlave || (bAutoMode && ClusterManager->IsSlave());
		if (bIsSlave)
		{
			BinaryListener = FOnClusterEventBinaryListener::CreateRaw(this, &FDMXDisplayClusterModule::OnClusterEventReceived);
			ClusterManager->AddClusterEventBinaryListener(BinaryListener);
			// end here as we do not want to accidentally have a node acting both as a master and a slave
			return;
		}

		const bool bIsMaster = bForceMaster || (bAutoMode && ClusterManager->IsMaster());
		if (bIsMaster)
		{
			FDMXPortManager::Get().OnPortInputDequeued.AddRaw(this, &FDMXDisplayClusterModule::SendToSlaves);
		}
	}
}

void FDMXDisplayClusterModule::ShutdownModule()
{
	if (IDisplayClusterClusterManager* ClusterManager = IDisplayCluster::Get().GetClusterMgr())
	{
		if (BinaryListener.IsBound())
		{
			ClusterManager->RemoveClusterEventBinaryListener(BinaryListener);
		}
	}
}

void FDMXDisplayClusterModule::OnClusterEventReceived(const FDisplayClusterClusterEventBinary& Event)
{
	if (Event.EventId == DMXnDisplayReplicationEventID)
	{
		FMemoryReader Reader(Event.EventData);
		FDMXSignalSharedRef DMXSignal = MakeShared<FDMXSignal, ESPMode::ThreadSafe>();
		DMXSignal->Serialize(Reader);

		// forward the signal to all of the InputPorts
		for (const FDMXInputPortSharedRef& DMXInputPortRef : FDMXPortManager::Get().GetInputPorts())
		{
			DMXInputPortRef->SingleProducerInputDMXSignal(DMXSignal);
		}
	}
}

IMPLEMENT_MODULE(FDMXDisplayClusterModule, DMXDisplayCluster)


#undef LOCTEXT_NAMESPACE
