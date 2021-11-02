// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/DisplayClusterViewportManagerProxy.h"

#include "ClearQuad.h"
#include "IDisplayCluster.h"
#include "Misc/DisplayClusterLog.h"

#include "RHIContext.h"
#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"

#include "Render/Viewport/DisplayClusterViewportManager.h"
#include "Render/Viewport/DisplayClusterViewportProxy.h"
#include "Render/Viewport/DisplayClusterViewportStrings.h"

#include "Render/Viewport/RenderTarget/DisplayClusterRenderTargetManager.h"
#include "Render/Viewport/RenderTarget/DisplayClusterRenderTargetResource.h"
#include "Render/Viewport/Postprocess/DisplayClusterViewportPostProcessManager.h"
#include "Render/Viewport/Containers/DisplayClusterViewportProxyData.h"

#include "Render/Viewport/Configuration/DisplayClusterViewportConfiguration.h"

// Enable/disable warp&blend
static TAutoConsoleVariable<int32> CVarWarpBlendEnabled(
	TEXT("nDisplay.render.WarpBlendEnabled"),
	1,
	TEXT("Warp & Blend status\n")
	TEXT("0 : disabled\n")
	TEXT("1 : enabled\n")
	,
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarCrossGPUTransfersEnabled(
	TEXT("nDisplay.render.CrossGPUTransfers"),
	1,
	TEXT("(0 = disabled)\n"),
	ECVF_RenderThreadSafe
);

// Enable/Disable ClearTexture for Frame RTT
static TAutoConsoleVariable<int32> CVarClearFrameRTTEnabled(
	TEXT("nDisplay.render.ClearFrameRTTEnabled"),
	1,
	TEXT("Enables FrameRTT clearing before viewport resolving.\n")
	TEXT("0 : disabled\n")
	TEXT("1 : enabled\n")
	,
	ECVF_RenderThreadSafe
);

///////////////////////////////////////////////////////////////////////////////////////
//          FDisplayClusterViewportManagerProxy
///////////////////////////////////////////////////////////////////////////////////////

FDisplayClusterViewportManagerProxy::FDisplayClusterViewportManagerProxy(FDisplayClusterViewportManager& InViewportManager)
{
	RenderTargetManager = InViewportManager.RenderTargetManager;
	PostProcessManager = InViewportManager.PostProcessManager;
}

FDisplayClusterViewportManagerProxy::~FDisplayClusterViewportManagerProxy()
{
	// Delete viewport proxy objects
	for (FDisplayClusterViewportProxy* ViewportProxy : ViewportProxies)
	{
		if (ViewportProxy)
		{
			delete ViewportProxy;
		}
	}

	ViewportProxies.Empty();
}

void FDisplayClusterViewportManagerProxy::ImplSafeRelease()
{
	check(IsInGameThread());

	// Remove viewport manager proxy on render_thread
	ENQUEUE_RENDER_COMMAND(DisplayClusterVMProxy_SafeRelease)(
		[ViewportManagerProxy = this](FRHICommandListImmediate& RHICmdList)
	{
		delete ViewportManagerProxy;
	});
}

void FDisplayClusterViewportManagerProxy::ImplCreateViewport(FDisplayClusterViewportProxy* InViewportProxy)
{
	check(IsInGameThread());

	if (InViewportProxy)
	{
		ENQUEUE_RENDER_COMMAND(DisplayClusterVMProxy_CreateViewport)(
			[ViewportManagerProxy = this, ViewportProxy = InViewportProxy](FRHICommandListImmediate& RHICmdList)
		{
			ViewportManagerProxy->ViewportProxies.Add(ViewportProxy);
		});
	}
}

void FDisplayClusterViewportManagerProxy::ImplDeleteViewport(FDisplayClusterViewportProxy* InViewportProxy)
{
	check(IsInGameThread());

	// Remove viewport sceneproxy on renderthread
	ENQUEUE_RENDER_COMMAND(DisplayClusterVMProxy_DeleteViewport)(
		[ViewportManagerProxy = this, ViewportProxy = InViewportProxy](FRHICommandListImmediate& RHICmdList)
	{
		// Remove viewport obj from manager
		int ViewportProxyIndex = ViewportManagerProxy->ViewportProxies.Find(ViewportProxy);
		if (ViewportProxyIndex != INDEX_NONE)
		{
			ViewportManagerProxy->ViewportProxies[ViewportProxyIndex] = nullptr;
			ViewportManagerProxy->ViewportProxies.RemoveAt(ViewportProxyIndex);
		}

		delete ViewportProxy;
	});
}

void FDisplayClusterViewportManagerProxy::ImplUpdateSettings(const FDisplayClusterViewportConfiguration& InConfiguration)
{
	check(IsInGameThread());

	FDisplayClusterRenderFrameSettings*   NewRenderFrameSettings = new FDisplayClusterRenderFrameSettings(InConfiguration.GetRenderFrameSettingsConstRef());
	FDisplayClusterTextureShareSettings* NewTextureShareSettings = new FDisplayClusterTextureShareSettings(InConfiguration.GetTextureShareSettingsConstRef());

	// Send frame settings to renderthread
	ENQUEUE_RENDER_COMMAND(DisplayClusterVMProxy_UpdateSettings)(
		[ViewportManagerProxy = this, NewRenderFrameSettings, NewTextureShareSettings](FRHICommandListImmediate& RHICmdList)
	{
		ViewportManagerProxy->RenderFrameSettings = *NewRenderFrameSettings;
		delete NewRenderFrameSettings;

		ViewportManagerProxy->TextureShareSettings = *NewTextureShareSettings;
		delete NewTextureShareSettings;
	});
}

void FDisplayClusterViewportManagerProxy::ImplUpdateViewports(const TArray<FDisplayClusterViewport*>& InViewports)
{
	check(IsInGameThread());

	TArray<FDisplayClusterViewportProxyData*> ViewportProxiesData;
	for (FDisplayClusterViewport* ViewportIt : InViewports)
	{
		ViewportProxiesData.Add(new FDisplayClusterViewportProxyData(ViewportIt));
	}

	// Send viewports settings to renderthread
	ENQUEUE_RENDER_COMMAND(DisplayClusterVMProxy_UpdateViewports)(
		[ProxiesData = std::move(ViewportProxiesData)](FRHICommandListImmediate& RHICmdList)
	{
		for (FDisplayClusterViewportProxyData* It : ProxiesData)
		{
			It->UpdateProxy_RenderThread();
			delete It;
		}
	});
}


DECLARE_GPU_STAT_NAMED(nDisplay_ViewportManager_RenderFrame, TEXT("nDisplay ViewportManager::RenderFrame"));

void FDisplayClusterViewportManagerProxy::ImplRenderFrame(FViewport* InViewport)
{
	ENQUEUE_RENDER_COMMAND(DisplayClusterVMProxy_RenderFrame)(
		[ViewportManagerProxy = this, InViewport](FRHICommandListImmediate& RHICmdList)
	{
		SCOPED_GPU_STAT(RHICmdList, nDisplay_ViewportManager_RenderFrame);
		SCOPED_DRAW_EVENT(RHICmdList, nDisplay_ViewportManager_RenderFrame);

		bool bWarpBlendEnabled = ViewportManagerProxy->RenderFrameSettings.bAllowWarpBlend && CVarWarpBlendEnabled.GetValueOnRenderThread() != 0;

		// mGPU not used for in-editor rendering
		if(ViewportManagerProxy->RenderFrameSettings.bIsRenderingInEditor == false)
		{
			// Move all render target cross gpu
			ViewportManagerProxy->DoCrossGPUTransfers_RenderThread(RHICmdList);
			// Now all resources on GPU#0
		}

		// Update viewports resources: overlay, vp-overla, blur, nummips, etc
		ViewportManagerProxy->UpdateDeferredResources_RenderThread(RHICmdList);

		// Update the frame resources: post-processing, warping, and finally resolving everything to the frame resource
		ViewportManagerProxy->UpdateFrameResources_RenderThread(RHICmdList, bWarpBlendEnabled);

		if (InViewport)
		{
			if (FRHITexture2D* FrameOutputRTT = InViewport->GetRenderTargetTexture())
			{
				// For quadbuf stereo copy only left eye, right copy from OutputFrameTarget
				//@todo Copy QuadBuf_LeftEye/(mono,sbs,tp) to separate rtt, before UI and debug rendering
				//@todo QuadBuf_LeftEye copied latter, before present
				switch (ViewportManagerProxy->RenderFrameSettings.RenderMode)
				{
				case EDisplayClusterRenderFrameMode::SideBySide:
				case EDisplayClusterRenderFrameMode::TopBottom:
					ViewportManagerProxy->ResolveFrameTargetToBackBuffer_RenderThread(RHICmdList, 1, 0, FrameOutputRTT, FrameOutputRTT->GetSizeXY());
					break;
				default:
					break;
				}

				ViewportManagerProxy->ResolveFrameTargetToBackBuffer_RenderThread(RHICmdList, 0, 0, FrameOutputRTT, FrameOutputRTT->GetSizeXY());
			}
		}
	});
}

void FDisplayClusterViewportManagerProxy::UpdateDeferredResources_RenderThread(FRHICommandListImmediate& RHICmdList) const
{
	check(IsInRenderingThread());

	// Synchronize global frame for TextureShare
	if (TextureShareSettings.bIsEnabled)
	{
		FDisplayClusterViewport_TextureShare::EndSyncFrame(TextureShareSettings);
	}

	TArray<FDisplayClusterViewportProxy*> OverridedViewports;
	OverridedViewports.Reserve(ViewportProxies.Num());

	for (FDisplayClusterViewportProxy* ViewportProxy : ViewportProxies)
	{
		if (ViewportProxy->RenderSettings.OverrideViewportId.IsEmpty())
		{
			ViewportProxy->UpdateDeferredResources(RHICmdList);
		}
		else
		{
			// Update after all
			OverridedViewports.Add(ViewportProxy);
		}
	}

	// Update deferred viewports after all
	for (FDisplayClusterViewportProxy* ViewportProxy : OverridedViewports)
	{
		ViewportProxy->UpdateDeferredResources(RHICmdList);
	}
}

static void ImplClearRenderTargetResource_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InRenderTargetTexture)
{
	FRHIRenderPassInfo RPInfo(InRenderTargetTexture, ERenderTargetActions::DontLoad_Store);
	TransitionRenderPassTargets(RHICmdList, RPInfo);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("nDisplay_ClearRTT"));
	{
		const FIntPoint Size = InRenderTargetTexture->GetSizeXY();
		RHICmdList.SetViewport(0, 0, 0.0f, Size.X, Size.Y, 1.0f);
		DrawClearQuad(RHICmdList, FLinearColor::Black);
	}
	RHICmdList.EndRenderPass();
}

void FDisplayClusterViewportManagerProxy::ImplClearFrameTargets_RenderThread(FRHICommandListImmediate& RHICmdList) const
{
	TArray<FRHITexture2D*> FrameResources;
	TArray<FRHITexture2D*> AdditionalFrameResources;
	TArray<FIntPoint> TargetOffset;
	if (GetFrameTargets_RenderThread(FrameResources, TargetOffset, &AdditionalFrameResources))
	{
		for (FRHITexture2D* It : FrameResources)
		{
			ImplClearRenderTargetResource_RenderThread(RHICmdList, It);
		}
	}
}

enum class EWarpPass : uint8
{
	Begin = 0,
	Render,
	End,
	COUNT
};

void FDisplayClusterViewportManagerProxy::UpdateFrameResources_RenderThread(FRHICommandListImmediate& RHICmdList, bool bWarpBlendEnabled) const
{
	check(IsInRenderingThread());

	// Do postprocess before warp&blend
	PostProcessManager->PerformPostProcessBeforeWarpBlend_RenderThread(RHICmdList, this);

	// Support viewport overlap order sorting:
	TArray<FDisplayClusterViewportProxy*> SortedViewportProxy = ImplGetViewportProxies_RenderThread();
	SortedViewportProxy.Sort(
		[](const FDisplayClusterViewportProxy& VP1, const FDisplayClusterViewportProxy& VP2)
		{
			return  VP1.GetRenderSettings_RenderThread().OverlapOrder < VP2.GetRenderSettings_RenderThread().OverlapOrder;
		}
	);

	// Clear Frame RTT resources before viewport resolving
	const bool bClearFrameRTTEnabled = CVarClearFrameRTTEnabled.GetValueOnRenderThread() != 0;
	if (bClearFrameRTTEnabled)
	{
		ImplClearFrameTargets_RenderThread(RHICmdList);
	}

	// Handle warped viewport projection policy logic:
	for (uint8 WarpPass = 0; WarpPass < (uint8)EWarpPass::COUNT; WarpPass++)
	{
		// Update deffered resources for viewports
		for (IDisplayClusterViewportProxy* ViewportProxy : SortedViewportProxy)
		{
			if (ViewportProxy)
			{
				// Iterate over visible viewports:
				if (ViewportProxy->GetRenderSettings_RenderThread().bVisible)
				{
					const TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& PrjPolicy = ViewportProxy->GetProjectionPolicy_RenderThread();

					bool bShouldApplyWarpBlend = bWarpBlendEnabled;
					if (bShouldApplyWarpBlend)
					{
						// Support warp blend logic
						if (ViewportProxy->GetPostRenderSettings_RenderThread().Replace.IsEnabled())
						{
							// When used override texture, disable warp blend
							bShouldApplyWarpBlend = false;
						}
						else
						{
							
							// Projection policy must support warp blend op
							bShouldApplyWarpBlend = PrjPolicy.IsValid() && PrjPolicy->IsWarpBlendSupported();
						}
					}

					if (bShouldApplyWarpBlend)
					{
						switch ((EWarpPass)WarpPass)
						{
						case EWarpPass::Begin:
							PrjPolicy->BeginWarpBlend_RenderThread(RHICmdList, ViewportProxy);
							break;

						case EWarpPass::Render:
							PrjPolicy->ApplyWarpBlend_RenderThread(RHICmdList, ViewportProxy);
							break;

						case EWarpPass::End:
							PrjPolicy->EndWarpBlend_RenderThread(RHICmdList, ViewportProxy);
							break;

						default:
							break;
						}
					}
					else
					{
						switch ((EWarpPass)WarpPass)
						{
						case EWarpPass::Render:
							// just resolve not warped viewports to frame target texture
							ViewportProxy->ResolveResources(RHICmdList, EDisplayClusterViewportResourceType::InputShaderResource, ViewportProxy->GetOutputResourceType());
							break;

						default:
							break;
						}
					}
				}
			}
		}
	}

	PostProcessManager->PerformPostProcessAfterWarpBlend_RenderThread(RHICmdList, this);
}

void FDisplayClusterViewportManagerProxy::DoCrossGPUTransfers_RenderThread(FRHICommandListImmediate& RHICmdList) const
{
	check(IsInRenderingThread());

#if WITH_MGPU

	if ((CVarCrossGPUTransfersEnabled.GetValueOnRenderThread() == 0))
	{
		return;
	}

	// Copy the view render results to all GPUs that are native to the viewport.
	TArray<FTransferTextureParams> TransferResources;

	for (FDisplayClusterViewportProxy* ViewportProxy : ViewportProxies)
	{
		for (FDisplayClusterViewport_Context& ViewportContext : ViewportProxy->Contexts)
		{
			if (ViewportContext.bAllowGPUTransferOptimization && ViewportContext.GPUIndex >= 0)
			{
				// Use optimized cross GPU transfer for this context

				FRenderTarget* RenderTarget = ViewportProxy->RenderTargets[ViewportContext.ContextNum];
				FRHITexture2D* TextureRHI = ViewportProxy->RenderTargets[ViewportContext.ContextNum]->GetRenderTargetTexture();

				FRHIGPUMask RenderTargetGPUMask = (GNumExplicitGPUsForRendering > 1 && RenderTarget) ? RenderTarget->GetGPUMask(RHICmdList) : FRHIGPUMask::GPU0();
				{
					static auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PathTracing.GPUCount"));
					if (CVar && CVar->GetInt() > 1)
					{
						RenderTargetGPUMask = FRHIGPUMask::All(); // Broadcast to all GPUs 
					}
				}

				FRHIGPUMask ContextGPUMask = FRHIGPUMask::FromIndex(ViewportContext.GPUIndex);

				if (ContextGPUMask != RenderTargetGPUMask)
				{
					// Clamp the view rect by the rendertarget rect to prevent issues when resizing the viewport.
					const FIntRect TransferRect = ViewportContext.RenderTargetRect;

					if (TransferRect.Width() > 0 && TransferRect.Height() > 0)
					{
						for (uint32 RenderTargetGPUIndex : RenderTargetGPUMask)
						{
							if (!ContextGPUMask.Contains(RenderTargetGPUIndex))
							{
								FTransferTextureParams ResourceParams(TextureRHI, TransferRect, ContextGPUMask.GetFirstIndex(), RenderTargetGPUIndex, true, ViewportContext.bEnabledGPUTransferLockSteps);
								TransferResources.Add(ResourceParams);
							}
						}
					}
				}
			}
		}
	}

	if (TransferResources.Num() > 0)
	{
		RHICmdList.TransferTextures(TransferResources);
	}

#endif // WITH_MGPU
}

bool FDisplayClusterViewportManagerProxy::GetFrameTargets_RenderThread(TArray<FRHITexture2D*>& OutFrameResources, TArray<FIntPoint>& OutTargetOffsets, TArray<FRHITexture2D*>* OutAdditionalFrameResources) const
{
	check(IsInRenderingThread());

	// Get any defined frame targets from first visible viewport
	for (FDisplayClusterViewportProxy* ViewportProxy : ViewportProxies)
	{
		if (ViewportProxy)
		{
			const TArray<FDisplayClusterTextureResource*>& Frames = ViewportProxy->OutputFrameTargetableResources;
			const TArray<FDisplayClusterTextureResource*>& AdditionalFrames = ViewportProxy->AdditionalFrameTargetableResources;

			if (Frames.Num() > 0)
			{
				OutFrameResources.Reserve(Frames.Num());
				OutTargetOffsets.Reserve(Frames.Num());

				bool bUseAdditionalFrameResources = (OutAdditionalFrameResources != nullptr) && (AdditionalFrames.Num() == Frames.Num());

				if (bUseAdditionalFrameResources)
				{
					OutAdditionalFrameResources->AddZeroed(AdditionalFrames.Num());
				}

				for (int32 FrameIt = 0; FrameIt < Frames.Num(); FrameIt++)
				{
					OutFrameResources.Add(Frames[FrameIt]->GetTextureResource());
					OutTargetOffsets.Add(Frames[FrameIt]->BackbufferFrameOffset);

					if (bUseAdditionalFrameResources)
					{
						(*OutAdditionalFrameResources)[FrameIt] = AdditionalFrames[FrameIt]->GetTextureResource();
					}
				}

				return true;
			}
		}
	}

	// no visible viewports
	return false;
}

bool FDisplayClusterViewportManagerProxy::ResolveFrameTargetToBackBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, const uint32 InContextNum, const int DestArrayIndex, FRHITexture2D* DestTexture, FVector2D WindowSize) const
{
	check(IsInRenderingThread());

	TArray<FRHITexture2D*>   FrameResources;
	TArray<FIntPoint>        TargetOffsets;
	if (GetFrameTargets_RenderThread(FrameResources, TargetOffsets))
	{
		// Use internal frame textures as source
		int ContextNum = InContextNum;

		FRHITexture2D* FrameTexture = FrameResources[ContextNum];
		FIntPoint DstOffset = TargetOffsets[ContextNum];

		if (FrameTexture)
		{
			const FIntPoint SrcSize = FrameTexture->GetSizeXY();
			const FIntPoint DstSize = DestTexture->GetSizeXY();;

			FIntRect DstRect(DstOffset, DstOffset + SrcSize);

			// Fit to backbuffer size
			DstRect.Max.X = FMath::Min(DstSize.X, DstRect.Max.X);
			DstRect.Max.Y = FMath::Min(DstSize.Y, DstRect.Max.Y);

			FResolveParams CopyParams;

			CopyParams.SourceArrayIndex = 0;
			CopyParams.DestArrayIndex = DestArrayIndex;

			CopyParams.Rect.X1 = 0;
			CopyParams.Rect.Y1 = 0;
			CopyParams.Rect.X2 = DstRect.Width();
			CopyParams.Rect.Y2 = DstRect.Height();

			CopyParams.DestRect.X1 = DstRect.Min.X;
			CopyParams.DestRect.Y1 = DstRect.Min.Y;
			CopyParams.DestRect.X2 = DstRect.Max.X;
			CopyParams.DestRect.Y2 = DstRect.Max.Y;

			RHICmdList.CopyToResolveTarget(FrameTexture, DestTexture, CopyParams);

			return true;
		}
	}

	return false;
}

FDisplayClusterViewportProxy* FDisplayClusterViewportManagerProxy::ImplFindViewport_RenderThread(const FString& ViewportId) const
{
	check(IsInRenderingThread());

	// Ok, we have a request for a particular viewport. Let's find it.
	FDisplayClusterViewportProxy* const* DesiredViewport = ViewportProxies.FindByPredicate([ViewportId](const FDisplayClusterViewportProxy* ItemViewport)
	{
		return ViewportId.Equals(ItemViewport->GetId(), ESearchCase::IgnoreCase);
	});

	return (DesiredViewport != nullptr) ? *DesiredViewport : nullptr;
}

IDisplayClusterViewportProxy* FDisplayClusterViewportManagerProxy::FindViewport_RenderThread(const enum EStereoscopicPass StereoPassType, uint32* OutContextNum) const
{
	check(IsInRenderingThread());

	for (FDisplayClusterViewportProxy* ViewportProxy : ViewportProxies)
	{
		if (ViewportProxy && ViewportProxy->FindContext_RenderThread(StereoPassType, OutContextNum))
		{
			return ViewportProxy;
		}
	}

	// Viewport proxy not found
	return nullptr;
}
