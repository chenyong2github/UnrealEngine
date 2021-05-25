// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXDisplayClusterModule.h"

#include "DMXDisplayClusterReplicator.h"
#include "IDisplayCluster.h"


void FDMXDisplayClusterModule::StartupModule()
{
	IDisplayCluster::Get().OnDisplayClusterStartSession().AddRaw(this, &FDMXDisplayClusterModule::CreateDMXDisplayClusterReplicator);
}

void FDMXDisplayClusterModule::CreateDMXDisplayClusterReplicator()
{
	// Only run when operating as cluster and not in editor
	if (IDisplayCluster::Get().GetOperationMode() == EDisplayClusterOperationMode::Cluster)
	{
		if (IDisplayClusterClusterManager* ClusterManager = IDisplayCluster::Get().GetClusterMgr())
		{
			DMXDisplayClusterReplicator = MakeShared<FDMXDisplayClusterReplicator>();
		}
	}
}

void FDMXDisplayClusterModule::ShutdownModule()
{}

IMPLEMENT_MODULE(FDMXDisplayClusterModule, DMXDisplayCluster)
