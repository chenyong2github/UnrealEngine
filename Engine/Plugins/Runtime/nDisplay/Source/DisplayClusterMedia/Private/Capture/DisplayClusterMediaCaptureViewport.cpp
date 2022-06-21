// Copyright Epic Games, Inc. All Rights Reserved.

#include "Capture/DisplayClusterMediaCaptureViewport.h"

#include "IDisplayCluster.h"
#include "IDisplayClusterCallbacks.h"

#include "Render/Viewport/IDisplayClusterViewportManagerProxy.h"
#include "RHICommandList.h"
#include "RHIResources.h"


FDisplayClusterMediaCaptureViewport::FDisplayClusterMediaCaptureViewport(const FString& InMediaId, const FString& InClusterNodeId, const FString& InViewportId, UMediaOutput* InMediaOutput, UTextureRenderTarget2D* InRenderTarget)
	: FDisplayClusterMediaCaptureBase(InMediaId, InClusterNodeId, InMediaOutput, InRenderTarget)
	, ViewportId(InViewportId)
{
}


bool FDisplayClusterMediaCaptureViewport::StartCapture()
{
	// If capturing has started successfully, subscribe for rendering callbacks
	if (FDisplayClusterMediaCaptureBase::StartCapture())
	{
		IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostRenderViewFamily_RenderThread().AddRaw(this, &FDisplayClusterMediaCaptureViewport::OnPostRenderViewFamily_RenderThread);
		return true;
	}

	return false;
}

void FDisplayClusterMediaCaptureViewport::StopCapture()
{
	// Stop rendering notifications
	IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostRenderViewFamily_RenderThread().RemoveAll(this);
	// Stop capturing
	FDisplayClusterMediaCaptureBase::StopCapture();
}

void FDisplayClusterMediaCaptureViewport::OnPostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, const FSceneViewFamily& ViewFamily, const IDisplayClusterViewportProxy* ViewportProxy)
{
	ensure(ViewportProxy);

	if (ViewportProxy && ViewportProxy->GetId().Equals(GetViewportId(), ESearchCase::IgnoreCase))
	{
		TArray<FRHITexture*> Textures;
		TArray<FIntRect>     Regions;

		// Get RHI texture
		if (ViewportProxy->GetResourcesWithRects_RenderThread(EDisplayClusterViewportResourceType::InternalRenderTargetResource, Textures, Regions))
		{
			if (Textures.Num() > 0 && Regions.Num() > 0)
			{
				FMediaTextureInfo TextureInfo{ Textures[0], Regions[0] };
				ExportMediaData(RHICmdList, TextureInfo);
			}
		}
	}
}
