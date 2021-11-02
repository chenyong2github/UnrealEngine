// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Render/Viewport/IDisplayClusterViewportManagerProxy.h"

#include "Render/Viewport/DisplayClusterViewportProxy.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_Enums.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameSettings.h"
#include "Render/Viewport/Containers/DisplayClusterTextureShareSettings.h"

class FDisplayClusterRenderTargetManager;
class FDisplayClusterViewportPostProcessManager;
class FDisplayClusterViewportManager;
class IDisplayClusterProjectionPolicy;
class FDisplayClusterViewportConfiguration;
class FViewport;


class FDisplayClusterViewportManagerProxy
	: public IDisplayClusterViewportManagerProxy
{
public:
	FDisplayClusterViewportManagerProxy(FDisplayClusterViewportManager& InViewportManager);
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

	virtual IDisplayClusterViewportProxy* FindViewport_RenderThread(const EStereoscopicPass StereoPassType, uint32* OutContextNum = nullptr) const override;

	virtual const TArrayView<IDisplayClusterViewportProxy*> GetViewports_RenderThread() const override
	{
		return TArrayView<IDisplayClusterViewportProxy*>((IDisplayClusterViewportProxy**)(ViewportProxies.GetData()), ViewportProxies.Num());
	}

	virtual bool GetFrameTargets_RenderThread(TArray<FRHITexture2D*>& OutFrameResources, TArray<FIntPoint>& OutTargetOffsets, TArray<FRHITexture2D*>* OutAdditionalFrameResources = nullptr) const override;
	virtual bool ResolveFrameTargetToBackBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, const uint32 InContextNum, const int DestArrayIndex, FRHITexture2D* DestTexture, FVector2D WindowSize) const override;

	// internal use only
	const FDisplayClusterRenderFrameSettings& GetRenderFrameSettings_RenderThread() const
	{
		check(IsInRenderingThread());

		return RenderFrameSettings;
	}

	const TArray<FDisplayClusterViewportProxy*>& ImplGetViewportProxies_RenderThread() const
	{
		check(IsInRenderingThread());

		return ViewportProxies;
	}

	FDisplayClusterViewportProxy* ImplFindViewport_RenderThread(const FString& InViewportId) const;

	void ImplCreateViewport(FDisplayClusterViewportProxy* InViewportProxy);
	void ImplDeleteViewport(FDisplayClusterViewportProxy* InViewportProxy);
	void ImplUpdateSettings(const FDisplayClusterViewportConfiguration& InConfiguration);
	void ImplUpdateViewports(const TArray<FDisplayClusterViewport*>& InViewports);
	void ImplSafeRelease();

	void ImplRenderFrame(FViewport* InViewport);
	void ImplClearFrameTargets_RenderThread(FRHICommandListImmediate& RHICmdList) const;

private:
	TSharedPtr<FDisplayClusterRenderTargetManager, ESPMode::ThreadSafe>        RenderTargetManager;
	TSharedPtr<FDisplayClusterViewportPostProcessManager, ESPMode::ThreadSafe> PostProcessManager;

	FDisplayClusterRenderFrameSettings RenderFrameSettings;
	FDisplayClusterTextureShareSettings TextureShareSettings;

	TArray<FDisplayClusterViewportProxy*> ViewportProxies;
};
