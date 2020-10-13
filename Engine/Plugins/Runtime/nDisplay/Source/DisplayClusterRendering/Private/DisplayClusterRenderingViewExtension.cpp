// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRenderingViewExtension.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "SceneView.h"


FDisplayClusterRenderingViewExtension::FDisplayClusterRenderingViewExtension(const FAutoRegister& AutoRegister, FTextureRenderTargetResource* InLinkedRTT, IDisplayClusterProjectionPolicy* InProjectionPolicy)
	: FSceneViewExtensionBase(AutoRegister)
	, LinkedRTT(InLinkedRTT)
	, ProjectionPolicy(InProjectionPolicy)
{
}

void FDisplayClusterRenderingViewExtension::PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
	if (ProjectionPolicy && ProjectionPolicy->IsWarpBlendSupported())
	{
		FTexture2DRHIRef BackBufferTexture = InViewFamily.RenderTarget->GetRenderTargetTexture();
		ProjectionPolicy->ApplyWarpBlend_RenderThread(0, RHICmdList, BackBufferTexture, FIntRect(FIntPoint(0, 0), BackBufferTexture->GetSizeXY()));
	}
}
