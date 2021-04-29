// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/PostProcess/IDisplayClusterPostProcess.h"
#include "Render/IDisplayClusterRenderManager.h"

class FDisplayClusterViewportManagerProxy;

/**
 * Helper class to collect post-process code and to easy the FDisplayClusterDeviceBase
 */

class FDisplayClusterViewportPostProcessManager
	: public IDisplayClusterPostProcess
{
public:
	FDisplayClusterViewportPostProcessManager()
	{}

	virtual ~FDisplayClusterViewportPostProcessManager() = default;

public:
	bool ShouldUseAdditionalFrameTargetableResource_PostProcess() const;

	void PerformPostProcessBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const;
	void PerformPostProcessAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const;

protected:
	void ImplPerformPostProcessViewBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const;
	void ImplPerformPostProcessViewAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList,  const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const;
	void ImplPerformPostProcessFrameAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const;


	/*
	virtual void PerformPostProcessViewBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const class IDisplayClusterViewportProxy* ViewportProxy) const override final
	{ }
	virtual void PerformPostProcessViewAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const class IDisplayClusterViewportProxy* ViewportProxy) const override final
	{ }
	virtual void PerformPostProcessFrameAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const TArray<FRHITexture2D*>* InFrameTargets = nullptr, const TArray<FRHITexture2D*>* InAdditionalFrameTargets = nullptr) const override final
	{ }
	*/

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
};
