// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/IDisplayClusterRenderManager.h"
#include "Render/PostProcess/IDisplayClusterPostProcess.h"

class FDisplayClusterViewportManager;

/**
 * Helper class to collect post-process code and to easy the FDisplayClusterDeviceBase
 */
class FDisplayClusterViewportManager_PostProcess
	: public IDisplayClusterPostProcess
{
public:
	FDisplayClusterViewportManager_PostProcess(FDisplayClusterViewportManager& InViewportManager)
		: ViewportManager(InViewportManager)
	{}

	virtual ~FDisplayClusterViewportManager_PostProcess() = default;

public:
	bool ShouldUseAdditionalFrameTargetableResource_PostProcess() const;

	void PerformPostProcessBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList) const;
	void PerformPostProcessAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList) const;

protected:
	virtual void PerformPostProcessViewBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const class IDisplayClusterViewportProxy* ViewportProxy) const override final;
	virtual void PerformPostProcessViewAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList,  const class IDisplayClusterViewportProxy* ViewportProxy) const override final;
	virtual void PerformPostProcessFrameAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const TArray<FRHITexture2D*>* InFrameTargets = nullptr, const TArray<FRHITexture2D*>* InAdditionalFrameTargets = nullptr) const override final;

	mutable TArray<IDisplayClusterRenderManager::FDisplayClusterPPInfo> PPOperations;

private:
	virtual bool IsPostProcessViewBeforeWarpBlendRequired() const override final
	{ return true; }

	virtual bool IsPostProcessViewAfterWarpBlendRequired() const override final
	{ return true; }

	virtual bool IsPostProcessFrameAfterWarpBlendRequired() const override final
	{ return true; }

	virtual bool ShouldUseAdditionalFrameTargetableResource() const override final
	{ return true; }

private:
	FDisplayClusterViewportManager& ViewportManager;
};
