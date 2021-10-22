// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/PostProcess/IDisplayClusterPostProcess.h"
#include "Render/IDisplayClusterRenderManager.h"

class FDisplayClusterViewportManager;
class FDisplayClusterViewportManagerProxy;
class FDisplayClusterViewportPostProcessOutputRemap;
struct FDisplayClusterConfigurationPostprocess;

/**
 * Helper class to collect post-process code and to easy the FDisplayClusterDeviceBase
 */

class FDisplayClusterViewportPostProcessManager
{
public:
	FDisplayClusterViewportPostProcessManager(FDisplayClusterViewportManager& InViewportManager);
	virtual ~FDisplayClusterViewportPostProcessManager() = default;

public:
	bool IsPostProcessViewBeforeWarpBlendRequired(const TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe>& PostprocessInstance) const;
	bool IsPostProcessViewAfterWarpBlendRequired(const TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe>& PostprocessInstance) const;
	bool IsPostProcessFrameAfterWarpBlendRequired(const TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe>& PostprocessInstance) const;

	bool IsAnyPostProcessRequired(const TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe>& PostprocessInstance) const;

	bool ShouldUseAdditionalFrameTargetableResource_PostProcess() const;
	bool ShouldUseFullSizeFrameTargetableResource() const;

	void PerformPostProcessBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const;
	void PerformPostProcessAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const;

	void Tick();

	bool HandleStartScene();
	void HandleEndScene();

	// Send data to render thread
	void FinalizeNewFrame();

	TSharedPtr<FDisplayClusterViewportPostProcessOutputRemap, ESPMode::ThreadSafe>& GetOutputRemap()
	{ return OutputRemap; }

public:
	bool CreatePostprocess(const FString& InPostprocessId, const FDisplayClusterConfigurationPostprocess* InConfigurationPostprocess);
	bool RemovePostprocess(const FString& InPostprocessId);
	bool UpdatePostprocess(const FString& InPostprocessId, const FDisplayClusterConfigurationPostprocess* InConfigurationPostprocess);

	const TArray<FString> GetPostprocess() const;
	TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe> FindPostProcess(const FString& InPostprocessId) const
	{
		return ImplFindPostProcess(InPostprocessId);
	}

private:
	TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe> ImplFindPostProcess(const FString& InPostprocessId) const;

protected:
	void ImplPerformPostProcessViewBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const;
	void ImplPerformPostProcessViewAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList,  const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const;
	void ImplPerformPostProcessFrameAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterViewportManagerProxy* InViewportManagerProxy) const;


protected:
	// Game thread instances
	TArray<TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe>> Postprocess;

private:
	// Render thread instances
	TArray<TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe>> PostprocessProxy;

	FDisplayClusterViewportManager& ViewportManager;
	TSharedPtr<FDisplayClusterViewportPostProcessOutputRemap, ESPMode::ThreadSafe> OutputRemap;
};
