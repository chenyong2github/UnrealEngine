// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterViewportLightCardManagerProxy.h"

#include "IDisplayClusterShaders.h"

#include "SceneInterface.h"
#include "RenderingThread.h"

///////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterViewportLightCardManagerProxy
///////////////////////////////////////////////////////////////////////////////////////////////
FDisplayClusterViewportLightCardManagerProxy::~FDisplayClusterViewportLightCardManagerProxy()
{
	ImplReleaseUVLightCardResource_RenderThread();
}

///////////////////////////////////////////////////////////////////////////////////////////////
FRHITexture* FDisplayClusterViewportLightCardManagerProxy::GetUVLightCardRHIResource_RenderThread() const
{
	return UVLightCardMapResource.IsValid() ? UVLightCardMapResource->GetTextureRHI().GetReference() : nullptr;
}

void FDisplayClusterViewportLightCardManagerProxy::UpdateUVLightCardResource(const TSharedPtr<FDisplayClusterViewportLightCardResource, ESPMode::ThreadSafe>& InUVLightCardMapResource)
{
	ENQUEUE_RENDER_COMMAND(DisplayClusterViewportLightCardManagerProxy_UpdateUVLightCardResource)(
		[InProxyData = SharedThis(this), UVLightCardMapResource = InUVLightCardMapResource](FRHICommandListImmediate& RHICmdList)
		{
			InProxyData->ImplUpdateUVLightCardResource_RenderThread(UVLightCardMapResource);
		});
}

void FDisplayClusterViewportLightCardManagerProxy::ReleaseUVLightCardResource()
{
	ENQUEUE_RENDER_COMMAND(DisplayClusterViewportLightCardManagerProxy_ReleaseUVLightCardResource)(
		[InProxyData = SharedThis(this)](FRHICommandListImmediate& RHICmdList)
		{
			InProxyData->ImplReleaseUVLightCardResource_RenderThread();
		});
}

void FDisplayClusterViewportLightCardManagerProxy::RenderUVLightCard(FSceneInterface* InSceneInterface, const float UVPlaneDefaultSize, const bool bRenderFinalColor) const
{
	ENQUEUE_RENDER_COMMAND(DisplayClusterViewportLightCardManagerProxy_RenderUVLightCard)(
		[InProxyData = SharedThis(this), SceneInterface = InSceneInterface, UVPlaneDefaultSize, bRenderFinalColor](FRHICommandListImmediate& RHICmdList)
		{
			InProxyData->ImplRenderUVLightCard_RenderThread(RHICmdList, SceneInterface, UVPlaneDefaultSize, bRenderFinalColor);
		});
}

void FDisplayClusterViewportLightCardManagerProxy::ImplUpdateUVLightCardResource_RenderThread(const TSharedPtr<FDisplayClusterViewportLightCardResource, ESPMode::ThreadSafe>& InUVLightCardMapResource)
{
	if (UVLightCardMapResource != InUVLightCardMapResource)
	{
		ImplReleaseUVLightCardResource_RenderThread();

		// Update resource ptr
		UVLightCardMapResource = InUVLightCardMapResource;
		if (UVLightCardMapResource.IsValid())
		{
			UVLightCardMapResource->InitResource();
		}
	}
}

void FDisplayClusterViewportLightCardManagerProxy::ImplReleaseUVLightCardResource_RenderThread()
{
	// Release the texture's resources and delete the texture object from the rendering thread
	if (UVLightCardMapResource.IsValid())
	{
		UVLightCardMapResource->ReleaseResource();

		UVLightCardMapResource.Reset();
	}
}

void FDisplayClusterViewportLightCardManagerProxy::ImplRenderUVLightCard_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneInterface* InSceneInterface, const float InUVPlaneDefaultSize, const bool bRenderFinalColor) const
{
	if(InSceneInterface && UVLightCardMapResource.IsValid())
	{
		IDisplayClusterShaders& ShadersAPI = IDisplayClusterShaders::Get();
		ShadersAPI.RenderPreprocess_UVLightCards(RHICmdList, InSceneInterface, UVLightCardMapResource.Get(), InUVPlaneDefaultSize, bRenderFinalColor);
	}
}
