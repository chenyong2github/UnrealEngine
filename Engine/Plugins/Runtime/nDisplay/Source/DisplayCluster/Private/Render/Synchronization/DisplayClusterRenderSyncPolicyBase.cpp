// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyBase.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Cluster/Controller/IPDisplayClusterNodeController.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


FDisplayClusterRenderSyncPolicyBase::FDisplayClusterRenderSyncPolicyBase()
{
}

FDisplayClusterRenderSyncPolicyBase::~FDisplayClusterRenderSyncPolicyBase()
{
}


void FDisplayClusterRenderSyncPolicyBase::SyncBarrierRenderThread()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRenderSync);

	if (GDisplayCluster->GetOperationMode() == EDisplayClusterOperationMode::Disabled)
	{
		return;
	}

	double tTime = 0.f;
	double bTime = 0.f;

	IPDisplayClusterNodeController* const pController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	if (pController)
	{
		pController->WaitForSwapSync(&tTime, &bTime);
	}

	UE_LOG(LogDisplayClusterRenderSync, Verbose, TEXT("Render barrier wait: t=%lf b=%lf"), tTime, bTime);
}
