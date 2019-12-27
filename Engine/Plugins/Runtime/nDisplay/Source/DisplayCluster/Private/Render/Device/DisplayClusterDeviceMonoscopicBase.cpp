// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/DisplayClusterDeviceMonoscopicBase.h"

#include "DisplayClusterLog.h"


FDisplayClusterDeviceMonoscopicBase::FDisplayClusterDeviceMonoscopicBase()
	: FDisplayClusterDeviceBase(1)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceMonoscopicBase::~FDisplayClusterDeviceMonoscopicBase()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}


void FDisplayClusterDeviceMonoscopicBase::AdjustViewRect(enum EStereoscopicPass StereoPassType, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	const int ViewportIndex = DecodeViewportIndex(StereoPassType);
	const uint32 ViewIndex = DecodeViewIndex(StereoPassType);

	// Current viewport data
	FDisplayClusterRenderViewport& RenderViewport = RenderViewports[ViewportIndex];
	
	// Provide the Engine with a viewport rectangle
	const FIntRect& ViewportArea = RenderViewport.GetArea();
	X = ViewportArea.Min.X;
	Y = ViewportArea.Min.Y;
	SizeX = ViewportArea.Width();
	SizeY = ViewportArea.Height();

	// Update view context
	FDisplayClusterRenderViewContext& ViewContext = RenderViewport.GetContext(ViewIndex);
	ViewContext.RenderTargetRect = FIntRect(X, Y, X + SizeX, Y + SizeY);

	const FIntRect& r = ViewContext.RenderTargetRect;
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Adjusted view rect: ViewportIdx=%d, ViewIndex=%d, [%d,%d - %d,%d]"), ViewportIndex, ViewIndex, r.Min.X, r.Min.Y, r.Max.X, r.Max.Y);
}
