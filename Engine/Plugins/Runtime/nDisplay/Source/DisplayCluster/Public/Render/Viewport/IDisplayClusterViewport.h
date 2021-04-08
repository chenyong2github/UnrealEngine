// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Viewport/IDisplayClusterViewport_CustomPostProcessSettings.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_Context.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_RenderSettings.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_RenderSettingsICVFX.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_PostRenderSettings.h"

class FSceneViewFamily;

/**
 * Rendering viewport (sub-region of the main viewport)
 */
class DISPLAYCLUSTER_API IDisplayClusterViewport
{
public:
	virtual ~IDisplayClusterViewport() = 0
	{ }

public:
	virtual FString GetId() const = 0;

	virtual FDisplayClusterViewport_RenderSettings& GetRenderSettings() = 0;
	virtual FDisplayClusterViewport_RenderSettingsICVFX& GetRenderSettingsICVFX() = 0;
	virtual FDisplayClusterViewport_PostRenderSettings& GetPostRenderSettings()  = 0;

	virtual const FDisplayClusterViewport_RenderSettings& GetRenderSettings() const = 0;
	virtual const FDisplayClusterViewport_RenderSettingsICVFX& GetRenderSettingsICVFX() const = 0;
	virtual const FDisplayClusterViewport_PostRenderSettings& GetPostRenderSettings() const = 0;

	virtual const TSharedPtr<class IDisplayClusterProjectionPolicy>& GetProjectionPolicy() const = 0;

	virtual       TArray<FDisplayClusterViewport_Context>& GetContexts() = 0;
	virtual const TArray<FDisplayClusterViewport_Context>& GetContexts() const = 0;

	// Override postprocess settings for this viewport
	virtual       IDisplayClusterViewport_CustomPostProcessSettings& GetViewport_CustomPostProcessSettings() = 0;
	virtual const IDisplayClusterViewport_CustomPostProcessSettings& GetViewport_CustomPostProcessSettings() const = 0;

	// Setup scene view for rendering specified Context
	virtual void SetupSceneView(uint32 ContextNum, class UWorld* World, FSceneViewFamily& InViewFamily, FSceneView& InView) const = 0;

	virtual class IDisplayClusterViewportManager& GetOwner() const = 0;
};
