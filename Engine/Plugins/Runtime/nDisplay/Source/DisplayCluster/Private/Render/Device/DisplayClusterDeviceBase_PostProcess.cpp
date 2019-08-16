// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/DisplayClusterDeviceBase_PostProcess.h"
#include "Render/Device/DisplayClusterRenderViewport.h"

#include "Misc/DisplayClusterHelpers.h"
#include "DisplayClusterLog.h"

#include "HAL/IConsoleManager.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// Round 1: VIEW before warp&blend
//////////////////////////////////////////////////////////////////////////////////////////////

// Enable/disable PP round 1
static TAutoConsoleVariable<int32> CVarPostprocessViewBeforeWarpBlend(
	TEXT("nDisplay.render.postprocess.ViewBeforeWarpBlend"),
	1,
	TEXT("Enable PP per view before warp&blend (0 = disabled)\n"),
	ECVF_RenderThreadSafe
);

void FDisplayClusterDeviceBase_PostProcess::PerformPostProcessViewBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewRect) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	const bool bEnabled = (CVarPostprocessViewBeforeWarpBlend.GetValueOnRenderThread() != 0);
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Postprocess VIEW before WarpBlend: %d"), bEnabled ? 1 : 0);

	if (bEnabled)
	{
		for (const IDisplayClusterRenderManager::FDisplayClusterPPInfo& CurPP : PPOperations)
		{
			if (CurPP.Operation->IsPostProcessViewBeforeWarpBlendRequired())
			{
				for (int ViewportIdx = 0; ViewportIdx < RenderViewportsRef.Num(); ++ViewportIdx)
				{
					for (int ViewIdx = 0; ViewIdx < ViewsPerViewport; ++ViewIdx)
					{
						UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("Postprocess VIEW before WarpBlend - Viewport %s, ViewportIdx %d, ViewIdx"), *RenderViewportsRef[ViewportIdx].GetId(), ViewportIdx, ViewIdx);
						CurPP.Operation->PerformPostProcessViewBeforeWarpBlend_RenderThread(RHICmdList, SrcTexture, RenderViewportsRef[ViewportIdx].GetContext(ViewIdx).RenderTargetRect);
					}
				}
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Round 2: FRAME before warp&blend
//////////////////////////////////////////////////////////////////////////////////////////////

// Enable/disable PP round 2
static TAutoConsoleVariable<int32> CVarPostprocessFrameBeforeWarpBlend(
	TEXT("nDisplay.render.postprocess.FrameBeforeWarpBlend"),
	1,
	TEXT("Enable PP per eye frame before warp&blend (0 = disabled)\n"),
	ECVF_RenderThreadSafe
);

void FDisplayClusterDeviceBase_PostProcess::PerformPostProcessFrameBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewRect) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	const bool bEnabled = (CVarPostprocessFrameBeforeWarpBlend.GetValueOnRenderThread() != 0);
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Postprocess FRAME before WarpBlend: %d"), bEnabled ? 1 : 0);

	if (bEnabled != 0)
	{
		for (const IDisplayClusterRenderManager::FDisplayClusterPPInfo& CurPP : PPOperations)
		{
			if (CurPP.Operation->IsPostProcessFrameBeforeWarpBlendRequired((uint32)ViewsPerViewport))
			{
				for (int ViewportIdx = 0; ViewportIdx < RenderViewportsRef.Num(); ++ViewportIdx)
				{
					for (int ViewIdx = 0; ViewIdx < ViewsPerViewport; ++ViewIdx)
					{
						UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("Postprocess FRAME before WarpBlend - Viewport %s, ViewportIdx %d, ViewIdx"), *RenderViewportsRef[ViewportIdx].GetId(), ViewportIdx, ViewIdx);
						CurPP.Operation->PerformPostProcessFrameBeforeWarpBlend_RenderThread(RHICmdList, SrcTexture, RenderViewportsRef[ViewportIdx].GetContext(ViewIdx).RenderTargetRect);
					}
				}
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Round 3: RENDER TARGET before warp&blend
//////////////////////////////////////////////////////////////////////////////////////////////

// Enable/disable PP round 3
static TAutoConsoleVariable<int32> CVarPostprocessTargetBeforeWarpBlend(
	TEXT("nDisplay.render.postprocess.TargetBeforeWarpBlend"),
	1,
	TEXT("Enable PP for the whole render target before warp&blend (0 = disabled)\n"),
	ECVF_RenderThreadSafe
);

void FDisplayClusterDeviceBase_PostProcess::PerformPostProcessRenderTargetBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	const bool bEnabled = (CVarPostprocessTargetBeforeWarpBlend.GetValueOnRenderThread() != 0);
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Postprocess TARGET before WarpBlend: %d"), bEnabled ? 1 : 0);

	if (bEnabled != 0)
	{
		for (const IDisplayClusterRenderManager::FDisplayClusterPPInfo& CurPP : PPOperations)
		{
			if (CurPP.Operation->IsPostProcessRenderTargetBeforeWarpBlendRequired())
			{
				for (int ViewportIdx = 0; ViewportIdx < RenderViewportsRef.Num(); ++ViewportIdx)
				{
					for (int ViewIdx = 0; ViewIdx < ViewsPerViewport; ++ViewIdx)
					{
						UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("Postprocess TARGET before WarpBlend - Viewport %s, ViewportIdx %d, ViewIdx"), *RenderViewportsRef[ViewportIdx].GetId(), ViewportIdx, ViewIdx);
						CurPP.Operation->PerformPostProcessRenderTargetBeforeWarpBlend_RenderThread(RHICmdList, SrcTexture);
					}
				}
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Round 4: VIEW after warp&blend
//////////////////////////////////////////////////////////////////////////////////////////////

// Enable/disable PP round 4
static TAutoConsoleVariable<int32> CVarPostprocessViewAfterWarpBlend(
	TEXT("nDisplay.render.postprocess.ViewAfterWarpBlend"),
	1,
	TEXT("Enable PP per view after warp&blend (0 = disabled)\n"),
	ECVF_RenderThreadSafe
);

void FDisplayClusterDeviceBase_PostProcess::PerformPostProcessViewAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& FrameRect) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	const bool bEnabled = (CVarPostprocessViewAfterWarpBlend.GetValueOnRenderThread() != 0);
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Postprocess VIEW after WarpBlend: %d"), bEnabled ? 1 : 0);

	if (bEnabled != 0)
	{
		for (const IDisplayClusterRenderManager::FDisplayClusterPPInfo& CurPP : PPOperations)
		{
			if (CurPP.Operation->IsPostProcessViewAfterWarpBlendRequired())
			{
				for (int ViewportIdx = 0; ViewportIdx < RenderViewportsRef.Num(); ++ViewportIdx)
				{
					for (int ViewIdx = 0; ViewIdx < ViewsPerViewport; ++ViewIdx)
					{
						UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("Postprocess VIEW after WarpBlend - Viewport %s, ViewportIdx %d, ViewIdx"), *RenderViewportsRef[ViewportIdx].GetId(), ViewportIdx, ViewIdx);
						CurPP.Operation->PerformPostProcessViewAfterWarpBlend_RenderThread(RHICmdList, SrcTexture, RenderViewportsRef[ViewportIdx].GetContext(ViewIdx).RenderTargetRect);
					}
				}
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Round 5: FRAME after warp&blend
//////////////////////////////////////////////////////////////////////////////////////////////

// Enable/disable PP round 5
static TAutoConsoleVariable<int32> CVarPostprocessFrameAfterWarpBlend(
	TEXT("nDisplay.render.postprocess.FrameAfterWarpBlend"),
	1,
	TEXT("Enable PP per eye frame after warp&blend (0 = disabled)\n"),
	ECVF_RenderThreadSafe
);

void FDisplayClusterDeviceBase_PostProcess::PerformPostProcessFrameAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& FrameRect) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	const bool bEnabled = (CVarPostprocessFrameAfterWarpBlend.GetValueOnRenderThread() != 0);
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Postprocess VIEW after WarpBlend: %d"), bEnabled ? 1 : 0);

	if (bEnabled != 0)
	{
		for (const IDisplayClusterRenderManager::FDisplayClusterPPInfo& CurPP : PPOperations)
		{
			if (CurPP.Operation->IsPostProcessFrameAfterWarpBlendRequired((uint32)ViewsPerViewport))
			{
				for (int ViewportIdx = 0; ViewportIdx < RenderViewportsRef.Num(); ++ViewportIdx)
				{
					for (int ViewIdx = 0; ViewIdx < ViewsPerViewport; ++ViewIdx)
					{
						UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("Postprocess FRAME after WarpBlend - Viewport %s, ViewportIdx %d, ViewIdx"), *RenderViewportsRef[ViewportIdx].GetId(), ViewportIdx, ViewIdx);
						CurPP.Operation->PerformPostProcessFrameAfterWarpBlend_RenderThread(RHICmdList, SrcTexture, RenderViewportsRef[ViewportIdx].GetContext(ViewIdx).RenderTargetRect);
					}
				}
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Round 6: RENDER TARGET after warp&blend
//////////////////////////////////////////////////////////////////////////////////////////////

// Enable/disable PP round 6
static TAutoConsoleVariable<int32> CVarPostprocessTargetAfterWarpBlend(
	TEXT("nDisplay.render.postprocess.TargetAfterWarpBlend"),
	1,
	TEXT("Enable PP for the whole render target after warp&blend (0 = disabled)\n"),
	ECVF_RenderThreadSafe
);

void FDisplayClusterDeviceBase_PostProcess::PerformPostProcessRenderTargetAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	const bool bEnabled = (CVarPostprocessTargetAfterWarpBlend.GetValueOnRenderThread() != 0);
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Postprocess TARGET after WarpBlend: %d"), bEnabled ? 1 : 0);

	if (bEnabled != 0)
	{
		for (const IDisplayClusterRenderManager::FDisplayClusterPPInfo& CurPP : PPOperations)
		{
			if (CurPP.Operation->IsPostProcessRenderTargetAfterWarpBlendRequired())
			{
				for (int ViewportIdx = 0; ViewportIdx < RenderViewportsRef.Num(); ++ViewportIdx)
				{
					for (int ViewIdx = 0; ViewIdx < ViewsPerViewport; ++ViewIdx)
					{
						UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("Postprocess TARGET after WarpBlend - Viewport %s, ViewportIdx %d, ViewIdx"), *RenderViewportsRef[ViewportIdx].GetId(), ViewportIdx, ViewIdx);
						CurPP.Operation->PerformPostProcessRenderTargetAfterWarpBlend_RenderThread(RHICmdList, SrcTexture);
					}
				}
			}
		}
	}
}
