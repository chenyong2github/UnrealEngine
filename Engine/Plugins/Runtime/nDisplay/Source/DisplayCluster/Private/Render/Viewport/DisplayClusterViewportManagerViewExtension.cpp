// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterViewportManagerViewExtension.h"
#include "DisplayClusterSceneViewExtensions.h"
#include "SceneRendering.h"

FDisplayClusterViewportManagerViewExtension::FDisplayClusterViewportManagerViewExtension(const FAutoRegister& AutoRegister, const IDisplayClusterViewportManager* InViewportManager)
	: FSceneViewExtensionBase(AutoRegister)
	, ViewportManager(InViewportManager)
{ }

void FDisplayClusterViewportManagerViewExtension::PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
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
