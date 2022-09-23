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
	// FSceneViewFamily::ProfileDescription is used currently as a viewport ID holder, so we use it to find a proper viewport proxy.
	if (ViewportManager)
	{
		if (const IDisplayClusterViewportManagerProxy* const ViewportMgrPoxy = ViewportManager->GetProxy())
		{
			if (!InViewFamily.ProfileDescription.IsEmpty())
			{
				if (IDisplayClusterViewportProxy* ViewportProxy = ViewportMgrPoxy->FindViewport_RenderThread(InViewFamily.ProfileDescription))
				{
					// Notify about viewport rendering finish
					IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostRenderViewFamily_RenderThread().Broadcast(GraphBuilder, InViewFamily, ViewportProxy);
				}
			}
		}
	}
}
