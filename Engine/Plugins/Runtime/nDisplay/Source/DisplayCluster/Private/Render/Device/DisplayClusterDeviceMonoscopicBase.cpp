// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/DisplayClusterDeviceMonoscopicBase.h"

#include "Misc/DisplayClusterLog.h"


FDisplayClusterDeviceMonoscopicBase::FDisplayClusterDeviceMonoscopicBase()
	: FDisplayClusterDeviceBase(1)
{
}

FDisplayClusterDeviceMonoscopicBase::~FDisplayClusterDeviceMonoscopicBase()
{
}


void FDisplayClusterDeviceMonoscopicBase::AdjustViewRect(enum EStereoscopicPass StereoPassType, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
	const int ViewportIndex = DecodeViewportIndex(StereoPassType);
	const uint32 ViewIndex = DecodeViewIndex(StereoPassType);

	// Current viewport data
	FDisplayClusterRenderViewport& RenderViewport = RenderViewports[ViewportIndex];
	
	// Provide the Engine with a viewport rectangle
	const FIntRect& ViewportRect = RenderViewport.GetRect();
	X = ViewportRect.Min.X;
	Y = ViewportRect.Min.Y;
	SizeX = ViewportRect.Width();
	SizeY = ViewportRect.Height();

	// Update view context
	FDisplayClusterRenderViewContext& ViewContext = RenderViewport.GetContext(ViewIndex);
	ViewContext.RenderTargetRect = FIntRect(X, Y, X + SizeX, Y + SizeY);

	const FIntRect& r = ViewContext.RenderTargetRect;
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Adjusted view rect: ViewportIdx=%d, ViewIndex=%d, [%d,%d - %d,%d]"), ViewportIndex, ViewIndex, r.Min.X, r.Min.Y, r.Max.X, r.Max.Y);
}
