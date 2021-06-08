// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Viewport/IDisplayClusterViewportProxy.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_OverscanSettings.h"

class FDisplayClusterRenderTargetResource;
class FDisplayClusterTextureResource;
class FDisplayClusterViewportProxyData;
class IDisplayClusterViewportManagerProxy;
class FDisplayClusterViewportManagerProxy;
class FDisplayClusterViewport;
class IDisplayClusterShaders;
class IDisplayClusterProjectionPolicy;

class FDisplayClusterViewportProxy
	: public IDisplayClusterViewportProxy
{
public:
	FDisplayClusterViewportProxy(const FDisplayClusterViewportManagerProxy& Owner, const FDisplayClusterViewport& RenderViewport);
	virtual ~FDisplayClusterViewportProxy();

public:
	///////////////////////////////
	// IDisplayClusterViewportProxy
	///////////////////////////////
	virtual FString GetId() const override
	{
		check(IsInRenderingThread());
		return ViewportId;
	}

	virtual const FDisplayClusterViewport_RenderSettings& GetRenderSettings_RenderThread() const override
	{
		check(IsInRenderingThread());
		return RenderSettings;
	}

	virtual const FDisplayClusterViewport_RenderSettingsICVFX& GetRenderSettingsICVFX_RenderThread() const override
	{
		check(IsInRenderingThread());
		return RenderSettingsICVFX;
	}

	virtual const FDisplayClusterViewport_PostRenderSettings& GetPostRenderSettings_RenderThread() const override
	{
		check(IsInRenderingThread());
		return PostRenderSettings;
	}

	virtual const TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& GetProjectionPolicy_RenderThread() const override
	{
		check(IsInRenderingThread());
		return ProjectionPolicy;
	}
	
	virtual const TArray<FDisplayClusterViewport_Context>& GetContexts_RenderThread() const override
	{
		check(IsInRenderingThread());
		return Contexts;
	}

	// Apply postprocess, generate mips, etc from settings in FDisplayClusterViewporDeferredUpdateSettings
	void UpdateDeferredResources(FRHICommandListImmediate& RHICmdList) const;

	//  Return viewport scene proxy resources by type
	virtual bool GetResources_RenderThread(const EDisplayClusterViewportResourceType InResourceType, TArray<FRHITexture2D*>& OutResources) const override;
	virtual bool GetResourcesWithRects_RenderThread(const EDisplayClusterViewportResourceType InResourceType, TArray<FRHITexture2D*>& OutResources, TArray<FIntRect>& OutRects) const override;

	// Resolve resource contexts
	virtual bool ResolveResources(FRHICommandListImmediate& RHICmdList, const EDisplayClusterViewportResourceType InputResourceType, const EDisplayClusterViewportResourceType OutputResourceType) const override;

	virtual const IDisplayClusterViewportManagerProxy& GetOwner() const override;
	virtual EDisplayClusterViewportResourceType GetOutputResourceType() const override;


	///////////////////////////////
	// ~IDisplayClusterViewportProxy
	///////////////////////////////

	bool ImplResolveResources(FRHICommandListImmediate& RHICmdList, FDisplayClusterViewportProxy const* SourceProxy, const EDisplayClusterViewportResourceType InputResourceType, const EDisplayClusterViewportResourceType OutputResourceType) const;


	inline bool FindContext_RenderThread(const enum EStereoscopicPass StereoPassType, uint32* OutContextNum)
	{
		check(IsInRenderingThread());

		for (int ContextNum = 0; ContextNum < Contexts.Num(); ContextNum++)
		{
			if (StereoPassType == Contexts[ContextNum].StereoscopicPass)
			{
				if (OutContextNum != nullptr)
				{
					*OutContextNum = ContextNum;
				}

				return true;
			}
		}

		return false;
	}

protected:
	friend FDisplayClusterViewportProxyData;
	friend FDisplayClusterViewportManagerProxy;

	// Unique viewport name
	const FString ViewportId;

	// Viewport render params
	FDisplayClusterViewport_RenderSettings       RenderSettings;
	FDisplayClusterViewport_RenderSettingsICVFX  RenderSettingsICVFX;
	FDisplayClusterViewport_PostRenderSettings   PostRenderSettings;

	// Additional parameters
	FDisplayClusterViewport_OverscanSettings     OverscanSettings;

	// Projection policy instance that serves this viewport
	TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> ProjectionPolicy;

	// Viewport contexts (left/center/right eyes)
	TArray<FDisplayClusterViewport_Context> Contexts;

	// View family render to this resources
	TArray<FDisplayClusterRenderTargetResource*> RenderTargets;

	// Projection policy output resources
	TArray<FDisplayClusterTextureResource*> OutputFrameTargetableResources;
	TArray<FDisplayClusterTextureResource*> AdditionalFrameTargetableResources;

#if WITH_EDITOR
	FTextureRHIRef OutputPreviewTargetableResource;
#endif

	// unique viewport resources
	TArray<FDisplayClusterTextureResource*> InputShaderResources;
	TArray<FDisplayClusterTextureResource*> AdditionalTargetableResources;
	TArray<FDisplayClusterTextureResource*> MipsShaderResources;

	const FDisplayClusterViewportManagerProxy& Owner;
	IDisplayClusterShaders& ShadersAPI;
};

