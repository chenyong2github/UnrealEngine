// Copyright Epic Games, Inc. All Rights Reserved.

#include "Capture/DisplayClusterMediaCaptureNode.h"

#include "Config/IDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationTypes.h"
#include "IDisplayCluster.h"
#include "IDisplayClusterCallbacks.h"

#include "RenderGraphBuilder.h"
#include "Render/Viewport/IDisplayClusterViewportManagerProxy.h"
#include "RHICommandList.h"
#include "RHIResources.h"

#include "UnrealClient.h"


FDisplayClusterMediaCaptureNode::FDisplayClusterMediaCaptureNode(const FString& InMediaId, const FString& InClusterNodeId, UMediaOutput* InMediaOutput)
	: FDisplayClusterMediaCaptureBase(InMediaId, InClusterNodeId, InMediaOutput)
{
}


bool FDisplayClusterMediaCaptureNode::StartCapture()
{
	// If capturing initialized and started successfully, subscribe for rendering callbacks
	if (FDisplayClusterMediaCaptureBase::StartCapture())
	{
		IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostFrameRender_RenderThread().AddRaw(this, &FDisplayClusterMediaCaptureNode::OnPostFrameRender_RenderThread);
		return true;
	}

	return false;
}

void FDisplayClusterMediaCaptureNode::StopCapture()
{
	// Stop rendering notifications
	IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostFrameRender_RenderThread().RemoveAll(this);
	// Stop capturing
	FDisplayClusterMediaCaptureBase::StopCapture();
}

FIntPoint FDisplayClusterMediaCaptureNode::GetCaptureSize() const
{
	if (IDisplayCluster::Get().GetConfigMgr() && IDisplayCluster::Get().GetConfigMgr()->GetLocalNode())
	{
		const FDisplayClusterConfigurationRectangle& Window = IDisplayCluster::Get().GetConfigMgr()->GetLocalNode()->WindowRect;
		return { Window.W, Window.H };
	}

	return FIntPoint();
}

void FDisplayClusterMediaCaptureNode::OnPostFrameRender_RenderThread(FRHICommandListImmediate& RHICmdList, const IDisplayClusterViewportManagerProxy* ViewportManagerProxy, FViewport* Viewport)
{
	ensure(ViewportManagerProxy);

	TArray<FRHITexture*> Textures;
	TArray<FIntPoint>    Regions;

	if (ViewportManagerProxy && ViewportManagerProxy->GetFrameTargets_RenderThread(Textures, Regions))
	{
		if (Textures.Num() > 0 && Regions.Num() > 0 && Textures[0])
		{
			FMediaTextureInfo TextureInfo{ Textures[0], FIntRect(Regions[0], Regions[0] + Textures[0]->GetDesc().Extent) };

			FRDGBuilder Builder(RHICmdList);
			ExportMediaData(Builder, TextureInfo);
			Builder.Execute();
		}
	}
}
