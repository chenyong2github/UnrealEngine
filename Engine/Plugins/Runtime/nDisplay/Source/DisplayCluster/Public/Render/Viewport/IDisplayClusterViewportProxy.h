// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_Enums.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_Context.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_RenderSettings.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_RenderSettingsICVFX.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_PostRenderSettings.h"


class DISPLAYCLUSTER_API IDisplayClusterViewportProxy
{
public:
	virtual ~IDisplayClusterViewportProxy() = default;

public:
	virtual FString GetId() const = 0;

	virtual const TSharedPtr<class IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& GetProjectionPolicy_RenderThread() const = 0;

	virtual const FDisplayClusterViewport_RenderSettings& GetRenderSettings_RenderThread() const = 0;
	virtual const FDisplayClusterViewport_RenderSettingsICVFX& GetRenderSettingsICVFX_RenderThread() const = 0;
	virtual const FDisplayClusterViewport_PostRenderSettings& GetPostRenderSettings_RenderThread() const = 0;

	virtual const TArray<FDisplayClusterViewport_Context>& GetContexts_RenderThread() const = 0;

	//  Return viewport scene proxy resources by type
	virtual bool GetResources_RenderThread(const EDisplayClusterViewportResourceType InResourceType, TArray<FRHITexture2D*>& OutResources) const = 0;
	virtual bool GetResourcesWithRects_RenderThread(const EDisplayClusterViewportResourceType InResourceType, TArray<FRHITexture2D*>& OutResources, TArray<FIntRect>& OutRects) const = 0;

	// Resolve resource contexts
	virtual bool ResolveResources(FRHICommandListImmediate& RHICmdList, const EDisplayClusterViewportResourceType InputResourceType, const EDisplayClusterViewportResourceType OutputResourceType) const = 0;

	virtual const class IDisplayClusterViewportManagerProxy& GetOwner() const = 0;
	virtual EDisplayClusterViewportResourceType   GetOutputResourceType() const = 0;
};
