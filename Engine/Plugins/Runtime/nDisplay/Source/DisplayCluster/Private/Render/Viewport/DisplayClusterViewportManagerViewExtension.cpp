// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterViewportManagerViewExtension.h"
#include "DisplayClusterSceneViewExtensions.h"
#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewportManagerProxy.h"
#include "SceneRendering.h"

#include "IDisplayCluster.h"
#include "IDisplayClusterCallbacks.h"


FDisplayClusterViewportManagerViewExtension::FDisplayClusterViewportManagerViewExtension(const FAutoRegister& AutoRegister, const IDisplayClusterViewportManager* InViewportManager)
	: FSceneViewExtensionBase(AutoRegister)
	, ViewportManager(InViewportManager)
{ }

void FDisplayClusterViewportManagerViewExtension::PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
	if (InView.bIsViewInfo)
	{
		FViewInfo& ViewInfo = static_cast<FViewInfo&>(InView);

		// UE-145088: VR HMD device post-processing cannot be applied to nDisplay rendering
		ViewInfo.bHMDHiddenAreaMaskActive = false;
	}
}

bool FDisplayClusterViewportManagerViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	static const FDisplayClusterSceneViewExtensionContext DCViewExtensionContext;
	if (Context.IsA(MoveTempIfPossible(DCViewExtensionContext)))
	{
		const FDisplayClusterSceneViewExtensionContext& DisplayContext = static_cast<const FDisplayClusterSceneViewExtensionContext&>(Context);
		if (DisplayContext.ViewportManager == ViewportManager)
		{
			// Apply only for DC viewports
			return true;
		}
	}

	return false;
}

void FDisplayClusterViewportManagerViewExtension::PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	// Since FSceneViewFamily has no direct references to the nD viewport it relates to,
	// we find a proper viewport ID by comparing RenderTarget addresses.
	if (ViewportManager)
	{
		const IDisplayClusterViewportManagerProxy* const ViewportMgrPoxy = ViewportManager->GetProxy();
		if (ViewportMgrPoxy)
		{
			// Get all available viewport proxies
			const TArrayView<IDisplayClusterViewportProxy*> ViewportProxies = ViewportMgrPoxy->GetViewports_RenderThread();

			// Filter those viewports that refer to the RenderTarget used in the ViewFamily
			TArray<IDisplayClusterViewportProxy*> FoundViewportProxies = ViewportProxies.FilterByPredicate([&InViewFamily](const IDisplayClusterViewportProxy* ViewportProxy)
			{
				TArray<FRHITexture*> RenderTargets;
				if (ViewportProxy->GetResources_RenderThread(EDisplayClusterViewportResourceType::InternalRenderTargetResource, RenderTargets))
				{
					if (RenderTargets.Contains(InViewFamily.RenderTarget->GetRenderTargetTexture()))
					{
						return true;
					}
				}

				return false;
			});

			AddPass(GraphBuilder, RDG_EVENT_NAME("ViewportProxy_PostRenderViewFamily"), [&InViewFamily, FoundViewportProxies = MoveTemp(FoundViewportProxies)] (FRHICommandListImmediate& RHICmdList)
			{
				// Now we can perform per-viewport notification
				for (const IDisplayClusterViewportProxy* ViewportProxy : FoundViewportProxies)
				{
					IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostRenderViewFamily_RenderThread().Broadcast(RHICmdList, InViewFamily, ViewportProxy);
				}
			});
		}
	}
}
