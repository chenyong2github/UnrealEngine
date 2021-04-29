// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/Postprocess/DisplayClusterViewportPostProcessManager.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"

#include "HAL/IConsoleManager.h"

#include "Render/Viewport/DisplayClusterViewportManagerProxy.h"
#include "Render/Viewport/DisplayClusterViewportProxy.h"

#include "Render/IPDisplayClusterRenderManager.h"
#include "Misc/DisplayClusterGlobals.h"

//#include "Render/Viewport/Configuration/DisplayClusterViewportConfiguration.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// Round 1: VIEW before warp&blend
//////////////////////////////////////////////////////////////////////////////////////////////

// Enable/disable nDisplay post-process
static TAutoConsoleVariable<int32> CVarCustomPPEnabled(
	TEXT("nDisplay.render.postprocess"),
	1,
	TEXT("Custom post-process (0 = disabled)\n"),
	ECVF_RenderThreadSafe
);

// Enable/disable PP round 1
static TAutoConsoleVariable<int32> CVarPostprocessViewBeforeWarpBlend(
	TEXT("nDisplay.render.postprocess.ViewBeforeWarpBlend"),
	1,
	TEXT("Enable PP per view before warp&blend (0 = disabled)\n"),
	ECVF_RenderThreadSafe
);

// Enable/disable PP round 4
static TAutoConsoleVariable<int32> CVarPostprocessViewAfterWarpBlend(
	TEXT("nDisplay.render.postprocess.ViewAfterWarpBlend"),
	1,
	TEXT("Enable PP per view after warp&blend (0 = disabled)\n"),
	ECVF_RenderThreadSafe
);

// Enable/disable PP round 5
static TAutoConsoleVariable<int32> CVarPostprocessFrameAfterWarpBlend(
	TEXT("nDisplay.render.postprocess.FrameAfterWarpBlend"),
	1,
	TEXT("Enable PP per eye frame after warp&blend (0 = disabled)\n"),
	ECVF_RenderThreadSafe
);

void FDisplayClusterViewportPostProcessManager::PerformPostProcessBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const
{
	bool bIsCustomPPEnabled = (CVarCustomPPEnabled.GetValueOnRenderThread() != 0);

	// Get registered PP operations map
	const TMap<FString, IDisplayClusterRenderManager::FDisplayClusterPPInfo> PPOperationsMap = GDisplayCluster->GetRenderMgr()->GetRegisteredPostprocessOperations();

	// Post-process before warp&blend
	if (bIsCustomPPEnabled)
	{
		// Get operations array (sorted already by the rendering manager)
		PPOperationsMap.GenerateValueArray(FDisplayClusterViewportPostProcessManager::PPOperations);

		// PP round 1: post-process for each view region before warp&blend
		ImplPerformPostProcessViewBeforeWarpBlend_RenderThread(RHICmdList, InViewportManagerProxy);
	}
}

void FDisplayClusterViewportPostProcessManager::PerformPostProcessAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const
{
	bool bIsCustomPPEnabled = (CVarCustomPPEnabled.GetValueOnRenderThread() != 0);

	// Post-process after warp&blend
	if (bIsCustomPPEnabled)
	{
		// PP round 4: post-process for each view region after warp&blend
		ImplPerformPostProcessViewAfterWarpBlend_RenderThread(RHICmdList, InViewportManagerProxy);
		// PP round 5: post-process for each eye frame after warp&blend
		ImplPerformPostProcessFrameAfterWarpBlend_RenderThread(RHICmdList, InViewportManagerProxy);
	}
}

bool FDisplayClusterViewportPostProcessManager::ShouldUseAdditionalFrameTargetableResource_PostProcess() const
{
	const bool bEnabled = (CVarPostprocessViewBeforeWarpBlend.GetValueOnAnyThread() != 0);

	if (bEnabled)
	{
		for (const IDisplayClusterRenderManager::FDisplayClusterPPInfo& CurPP : PPOperations)
		{
			if (CurPP.Operation->IsPostProcessViewBeforeWarpBlendRequired())
			{
				if (CurPP.Operation->ShouldUseAdditionalFrameTargetableResource())
				{
					return true;
				}
			}
		}
	}

	return false;
}

void FDisplayClusterViewportPostProcessManager::ImplPerformPostProcessViewBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const
{
	const bool bEnabled = (CVarPostprocessViewBeforeWarpBlend.GetValueOnRenderThread() != 0);
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Postprocess VIEW before WarpBlend: %d"), bEnabled ? 1 : 0);

	if (bEnabled && InViewportManagerProxy)
	{
		for (const IDisplayClusterRenderManager::FDisplayClusterPPInfo& CurPP : PPOperations)
		{
			if (CurPP.Operation->IsPostProcessViewBeforeWarpBlendRequired())
			{
				for(IDisplayClusterViewportProxy* ViewportProxyIt : InViewportManagerProxy->GetViewports_RenderThread())
				{
					UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("Postprocess VIEW before WarpBlend - Viewport '%s'"), *ViewportProxyIt->GetId());
					CurPP.Operation->PerformPostProcessViewBeforeWarpBlend_RenderThread(RHICmdList, ViewportProxyIt);
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Round 2: VIEW after warp&blend
//////////////////////////////////////////////////////////////////////////////////////////////

void FDisplayClusterViewportPostProcessManager::ImplPerformPostProcessViewAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const
{
	const bool bEnabled = (CVarPostprocessViewAfterWarpBlend.GetValueOnRenderThread() != 0);
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Postprocess VIEW after WarpBlend: %d"), bEnabled ? 1 : 0);

	if (bEnabled != 0 && InViewportManagerProxy)
	{
		for (const IDisplayClusterRenderManager::FDisplayClusterPPInfo& CurPP : PPOperations)
		{
			if (CurPP.Operation->IsPostProcessViewAfterWarpBlendRequired())
			{
				for (IDisplayClusterViewportProxy* ViewportProxyIt : InViewportManagerProxy->GetViewports_RenderThread())
				{
					UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("Postprocess VIEW after WarpBlend - Viewport '%s'"), *ViewportProxyIt->GetId());
					CurPP.Operation->PerformPostProcessViewAfterWarpBlend_RenderThread(RHICmdList, ViewportProxyIt);
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Round 3: FRAME after warp&blend
//////////////////////////////////////////////////////////////////////////////////////////////

void FDisplayClusterViewportPostProcessManager::ImplPerformPostProcessFrameAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const
{
	const bool bEnabled = (CVarPostprocessFrameAfterWarpBlend.GetValueOnRenderThread() != 0);
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Postprocess VIEW after WarpBlend: %d"), bEnabled ? 1 : 0);

	if (bEnabled != 0 && InViewportManagerProxy)
	{
		TArray<FRHITexture2D*> FrameResources;
		TArray<FRHITexture2D*> AdditionalFrameResources;
		TArray<FIntPoint> TargetOffset;
		if (InViewportManagerProxy->GetFrameTargets_RenderThread(FrameResources, TargetOffset, &AdditionalFrameResources))
		{
			for (const IDisplayClusterRenderManager::FDisplayClusterPPInfo& CurPP : PPOperations)
			{
				if (CurPP.Operation->IsPostProcessFrameAfterWarpBlendRequired())
				{
					UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Postprocess FRAME after WarpBlend"));

					TArray<FRHITexture2D*>* AdditionalResources = (AdditionalFrameResources.Num() > 0 && CurPP.Operation->ShouldUseAdditionalFrameTargetableResource())? &AdditionalFrameResources: nullptr;
					CurPP.Operation->PerformPostProcessFrameAfterWarpBlend_RenderThread(RHICmdList, &FrameResources, AdditionalResources);
				}
			}
		}
	}
}
