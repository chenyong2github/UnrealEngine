// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "SceneViewExtension.h"

class IDisplayClusterViewportManager;
/** 
 * View extension applying an DC Viewport features
 */
class FDisplayClusterViewportManagerViewExtension : public FSceneViewExtensionBase
{
public:
	FDisplayClusterViewportManagerViewExtension(const FAutoRegister& AutoRegister, const IDisplayClusterViewportManager* InViewportManager);

	//~ Begin ISceneViewExtension interface
	virtual int32 GetPriority() const override { return -1; }

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}

	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {};

	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;
	//~End ISceneVIewExtension interface

private:
	const IDisplayClusterViewportManager* ViewportManager;
};
