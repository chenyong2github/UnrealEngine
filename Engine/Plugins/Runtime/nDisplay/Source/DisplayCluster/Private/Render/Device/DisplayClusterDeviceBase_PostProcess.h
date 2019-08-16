// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Device/DisplayClusterRenderViewport.h"
#include "Render/PostProcess/IDisplayClusterPostProcess.h"


/**
 * Helper class to collect post-process code and to easy the FDisplayClusterDeviceBase
 */
class FDisplayClusterDeviceBase_PostProcess
	: public IDisplayClusterPostProcess
{
public:
	FDisplayClusterDeviceBase_PostProcess(TArray<FDisplayClusterRenderViewport>& InRenderViewports, int InViewsPerViewport)
		: RenderViewportsRef(InRenderViewports)
		, ViewsPerViewport(InViewsPerViewport)
	{
		check(ViewsPerViewport > 0);
	}

	virtual ~FDisplayClusterDeviceBase_PostProcess() = default;


protected:
	virtual void PerformPostProcessViewBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewRect) const override final;
	virtual void PerformPostProcessFrameBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& FrameRect) const override final;
	virtual void PerformPostProcessRenderTargetBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture) const override final;
	virtual void PerformPostProcessViewAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewRect) const override final;
	virtual void PerformPostProcessFrameAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& FrameRect) const override final;
	virtual void PerformPostProcessRenderTargetAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture) const override final;

	mutable TArray<IDisplayClusterRenderManager::FDisplayClusterPPInfo> PPOperations;

private:
	TArray<FDisplayClusterRenderViewport>& RenderViewportsRef;
	const int ViewsPerViewport;

private:
	virtual bool IsPostProcessViewBeforeWarpBlendRequired() override final
	{ return true; }

	virtual bool IsPostProcessViewAfterWarpBlendRequired() override final
	{ return true; }

	virtual bool IsPostProcessFrameBeforeWarpBlendRequired(uint32 FramesAmount) override final
	{ return true; }

	virtual bool IsPostProcessFrameAfterWarpBlendRequired(uint32 FramesAmount) override final
	{ return true; }

	virtual bool IsPostProcessRenderTargetBeforeWarpBlendRequired() override final
	{ return true; }

	virtual bool IsPostProcessRenderTargetAfterWarpBlendRequired() override final
	{ return true; }
};
