// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/SidebySide/DisplayClusterDeviceSideBySideBase.h"

#include "Misc/DisplayClusterLog.h"


FDisplayClusterDeviceSideBySideBase::FDisplayClusterDeviceSideBySideBase()
{
}

FDisplayClusterDeviceSideBySideBase::~FDisplayClusterDeviceSideBySideBase()
{
}


void FDisplayClusterDeviceSideBySideBase::AdjustViewRect(enum EStereoscopicPass StereoPassType, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
	const int ViewportIndex = DecodeViewportIndex(StereoPassType);
	const EStereoscopicPass Pass = DecodeStereoscopicPass(StereoPassType);
	const uint32 ViewIndex = DecodeViewIndex(StereoPassType);
	
	// Current viewport data
	FDisplayClusterRenderViewport& RenderViewport = RenderViewports[ViewportIndex];

	// Provide the Engine with a viewport rectangle
	const FIntRect& ViewportRect = RenderViewports[ViewportIndex].GetRect();
	if (Pass == EStereoscopicPass::eSSP_LEFT_EYE)
	{
		X = ViewportRect.Min.X / 2;
	}
	else if (Pass == EStereoscopicPass::eSSP_RIGHT_EYE)
	{
		X = SizeX / 2 + ViewportRect.Min.X / 2;
	}

	Y = ViewportRect.Min.Y;
	SizeX = ViewportRect.Width() / 2;
	SizeY = ViewportRect.Height();

	// Update view context
	FDisplayClusterRenderViewContext& ViewContext = RenderViewport.GetContext(ViewIndex);
	ViewContext.RenderTargetRect = FIntRect(X, Y, X + SizeX, Y + SizeY);

	const FIntRect& r = ViewContext.RenderTargetRect;
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Adjusted view rect: ViewportIdx=%d, ViewIndex=%d, [%d,%d - %d,%d]"), ViewportIndex, ViewIndex, r.Min.X, r.Min.Y, r.Max.X, r.Max.Y);
}
