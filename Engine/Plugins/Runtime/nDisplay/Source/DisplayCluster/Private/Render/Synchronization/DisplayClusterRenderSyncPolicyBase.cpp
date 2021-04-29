// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyBase.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Cluster/Controller/IDisplayClusterNodeController.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

#include "RHIResources.h"


void FDisplayClusterRenderSyncPolicyBase::SyncBarrierRenderThread()
{
	if (GDisplayCluster->GetOperationMode() == EDisplayClusterOperationMode::Disabled)
	{
		return;
	}

	IDisplayClusterNodeController* const pController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	if (pController)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(nDisplay SyncPolicyBase::SyncBarrier);
		pController->WaitForSwapSync();
	}
}

void FDisplayClusterRenderSyncPolicyBase::WaitForFrameCompletion()
{
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		FRHIViewport* const Viewport = GEngine->GameViewport->Viewport->GetViewportRHI().GetReference();
		check(Viewport);

		Viewport->IssueFrameEvent();
		Viewport->WaitForFrameEventCompletion();
	}
}
