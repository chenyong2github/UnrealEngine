// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Viewport/Containers/DisplayClusterViewport_RenderSettings.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_RenderSettingsICVFX.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_PostRenderSettings.h"

class IDisplayClusterProjectionPolicy;
class FDisplayClusterTextureResource;
class FDisplayClusterRenderTargetResource;
class FDisplayClusterViewport;
class FDisplayClusterViewportProxy;
class FDisplayClusterViewport_Context;


//
// Container for data exchange game->render threads
//
class FDisplayClusterViewportProxy_ExchangeContainer
{
public:
	FDisplayClusterViewportProxy_ExchangeContainer(const FDisplayClusterViewport* SrcViewport);
	~FDisplayClusterViewportProxy_ExchangeContainer() = default;

public:
	void CopyTo(FDisplayClusterViewportProxy* DstViewportProxy) const;

private:
	// Viewport render params
	FDisplayClusterViewport_RenderSettings       RenderSettings;
	FDisplayClusterViewport_RenderSettingsICVFX  RenderSettingsICVFX;
	FDisplayClusterViewport_PostRenderSettings   PostRenderSettings;

	// Projection policy instance that serves this viewport
	TSharedPtr<IDisplayClusterProjectionPolicy> ProjectionPolicy;

	// Viewport contexts (left/center/right eyes)
	TArray<FDisplayClusterViewport_Context>      Contexts;

	// View family render to this resources
	TArray<FDisplayClusterRenderTargetResource*> RenderTargets;

	// Projection policy output resources
	TArray<FDisplayClusterTextureResource*> OutputFrameTargetableResources;
	TArray<FDisplayClusterTextureResource*> AdditionalFrameTargetableResources;

	// unique viewport resources
	TArray<FDisplayClusterTextureResource*> InputShaderResources;
	TArray<FDisplayClusterTextureResource*> AdditionalTargetableResources;
	TArray<FDisplayClusterTextureResource*> MipsShaderResources;
};

