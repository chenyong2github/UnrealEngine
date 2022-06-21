// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/DisplayClusterMediaInputViewport.h"
#include "Render/Viewport/IDisplayClusterViewportManagerProxy.h"

#include "IDisplayCluster.h"
#include "IDisplayClusterCallbacks.h"

#include "RHICommandList.h"
#include "RHIResources.h"


FDisplayClusterMediaInputViewport::FDisplayClusterMediaInputViewport(const FString& InMediaId, const FString& InClusterNodeId, const FString& InViewportId, UMediaSource* InMediaSource, UMediaPlayer* InMediaPlayer, UMediaTexture* InMediaTexture)
	: FDisplayClusterMediaInputBase(InMediaId, InClusterNodeId, InMediaSource, InMediaPlayer, InMediaTexture)
	, ViewportId(InViewportId)
{
}


bool FDisplayClusterMediaInputViewport::Play()
{
	// If playback has started successfully, subscribe for rendering callbacks
	if (FDisplayClusterMediaInputBase::Play())
	{
		IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPreFrameRender_RenderThread().AddRaw(this, &FDisplayClusterMediaInputViewport::PreFrameRender_RenderThread);
		return true;
	}

	return false;
}

void FDisplayClusterMediaInputViewport::Stop()
{
	// Stop receiving notifications
	IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPreFrameRender_RenderThread().RemoveAll(this);
	// Stop playing
	FDisplayClusterMediaInputBase::Stop();
}

void FDisplayClusterMediaInputViewport::PreFrameRender_RenderThread(FRHICommandListImmediate& RHICmdList, const IDisplayClusterViewportManagerProxy* ViewportManagerProxy, FViewport* Viewport)
{
	checkSlow(ViewportManagerProxy);

	if (const IDisplayClusterViewportProxy* const CaptureViewport = ViewportManagerProxy->FindViewport_RenderThread(GetViewportId()))
	{
		TArray<FRHITexture*> Textures;
		TArray<FIntRect>     Regions;

		if (CaptureViewport->GetResourcesWithRects_RenderThread(EDisplayClusterViewportResourceType::InternalRenderTargetResource, Textures, Regions))
		{
			if (Textures.Num() > 0 && Regions.Num() > 0)
			{
				FMediaTextureInfo TextureInfo{ Textures[0], Regions[0] };
				ImportMediaData(RHICmdList, TextureInfo);
			}
		}
	}
}
