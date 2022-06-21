// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Render/Viewport/IDisplayClusterViewportManagerProxy.h"

#include "Render/Viewport/DisplayClusterViewportProxy.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_Enums.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameSettings.h"

class FDisplayClusterRenderTargetManager;
class FDisplayClusterViewportPostProcessManager;
class FDisplayClusterViewportManager;
class FDisplayClusterViewportResource;
class IDisplayClusterProjectionPolicy;

class FViewport;


class FDisplayClusterViewportManagerProxy
	: public IDisplayClusterViewportManagerProxy
{
public:
	FDisplayClusterViewportManagerProxy();
	virtual ~FDisplayClusterViewportManagerProxy();

public:
	void DoCrossGPUTransfers_RenderThread(FRHICommandListImmediate& RHICmdList) const;
	void UpdateDeferredResources_RenderThread(FRHICommandListImmediate& RHICmdList) const;
	void UpdateFrameResources_RenderThread(FRHICommandListImmediate& RHICmdList, bool bWarpBlendEnabled) const;

	/** Render thread funcs */
	virtual IDisplayClusterViewportProxy* FindViewport_RenderThread(const FString& InViewportId) const override
	{
		return ImplFindViewport_RenderThread(InViewportId);
	}

	virtual IDisplayClusterViewportProxy* FindViewport_RenderThread(const int32 StereoViewIndex, uint32* OutContextNum = nullptr) const override;

	virtual const TArrayView<IDisplayClusterViewportProxy*> GetViewports_RenderThread() const override
	{
		return TArrayView<IDisplayClusterViewportProxy*>((IDisplayClusterViewportProxy**)(ClusterNodeViewportProxies.GetData()), ClusterNodeViewportProxies.Num());
	}

	virtual bool GetFrameTargets_RenderThread(TArray<FRHITexture2D*>& OutFrameResources, TArray<FIntPoint>& OutTargetOffsets, TArray<FRHITexture2D*>* OutAdditionalFrameResources = nullptr) const override;
	virtual bool ResolveFrameTargetToBackBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, const uint32 InContextNum, const int32 DestArrayIndex, FRHITexture2D* DestTexture, FVector2D WindowSize) const override;

	// internal use only
	void DeleteResource_RenderThread(FDisplayClusterViewportResource* InDeletedResourcePtr);
	void Initialize(FDisplayClusterViewportManager& InViewportManager);

	const FDisplayClusterRenderFrameSettings& GetRenderFrameSettings_RenderThread() const
	{
		check(IsInRenderingThread());

		return RenderFrameSettings;
	}

	const TArray<FDisplayClusterViewportProxy*>& ImplGetViewportProxies_RenderThread() const
	{
		check(IsInRenderingThread());

		return ClusterNodeViewportProxies;
	}

	FDisplayClusterViewportProxy* ImplFindViewport_RenderThread(const FString& InViewportId) const;

	void ImplCreateViewport(FDisplayClusterViewportProxy* InViewportProxy);
	void ImplDeleteViewport(FDisplayClusterViewportProxy* InViewportProxy);
	void ImplUpdateRenderFrameSettings(const FDisplayClusterRenderFrameSettings& InRenderFrameSettings);
	void ImplUpdateViewports(const TArray<FDisplayClusterViewport*>& InViewports);
	void ImplSafeRelease();

	void ImplRenderFrame(FViewport* InViewport);
	void ImplClearFrameTargets_RenderThread(FRHICommandListImmediate& RHICmdList) const;

private:
	void ImplUpdateClusterNodeViewportProxies();

private:
	TSharedPtr<FDisplayClusterRenderTargetManager, ESPMode::ThreadSafe>        RenderTargetManager;
	TSharedPtr<FDisplayClusterViewportPostProcessManager, ESPMode::ThreadSafe> PostProcessManager;

	FDisplayClusterRenderFrameSettings RenderFrameSettings;

	TArray<FDisplayClusterViewportProxy*> ViewportProxies;
	TArray<FDisplayClusterViewportProxy*> ClusterNodeViewportProxies;
};
