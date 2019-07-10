// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomBase.h"

#include "DisplayClusterLog.h"


FDisplayClusterDeviceTopBottomBase::FDisplayClusterDeviceTopBottomBase()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceTopBottomBase::~FDisplayClusterDeviceTopBottomBase()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}


void FDisplayClusterDeviceTopBottomBase::AdjustViewRect(enum EStereoscopicPass StereoPassType, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	const int ViewportIndex = DecodeViewportIndex(StereoPassType);
	const EStereoscopicPass Pass = DecodeStereoscopicPass(StereoPassType);
	const uint32 ViewIndex = DecodeViewIndex(StereoPassType);

	// Current viewport data
	FDisplayClusterRenderViewport& RenderViewport = RenderViewports[ViewportIndex];

	// Provide the Engine with a viewport rectangle
	const FIntRect& ViewportArea = RenderViewports[ViewportIndex].GetArea();
	if (Pass == EStereoscopicPass::eSSP_LEFT_EYE)
	{
		Y = ViewportArea.Min.Y / 2;
	}
	else if (Pass == EStereoscopicPass::eSSP_RIGHT_EYE)
	{
		Y = SizeY / 2 + ViewportArea.Min.Y / 2;
	}

	X = ViewportArea.Min.X;
	SizeX = ViewportArea.Width();
	SizeY = ViewportArea.Height() / 2;

	// Update view context
	FDisplayClusterRenderViewContext& ViewContext = RenderViewport.GetContext(ViewIndex);
	ViewContext.RenderTargetRect = FIntRect(X, Y, X + SizeX, Y + SizeY);

	const FIntRect& r = ViewContext.RenderTargetRect;
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Adjusted view rect: ViewportIdx=%d, ViewIndex=%d, [%d,%d - %d,%d]"), ViewportIndex, ViewIndex, r.Min.X, r.Min.Y, r.Max.X, r.Max.Y);
}
