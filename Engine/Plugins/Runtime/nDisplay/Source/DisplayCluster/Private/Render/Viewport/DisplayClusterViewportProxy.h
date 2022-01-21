// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Viewport/IDisplayClusterViewportProxy.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_OverscanSettings.h"

class FDisplayClusterViewportRenderTargetResource;
class FDisplayClusterViewportTextureResource;
class FDisplayClusterViewportProxyData;
class IDisplayClusterViewportManagerProxy;
class FDisplayClusterViewportManagerProxy;
class FDisplayClusterViewport;
class IDisplayClusterShaders;
class IDisplayClusterProjectionPolicy;
class IDisplayClusterRender_MeshComponent;

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
	virtual bool ResolveResources_RenderThread(FRHICommandListImmediate& RHICmdList, const EDisplayClusterViewportResourceType InputResourceType, const EDisplayClusterViewportResourceType OutputResourceType) const override;

	virtual EDisplayClusterViewportResourceType GetOutputResourceType_RenderThread() const override;

	virtual const IDisplayClusterViewportManagerProxy& GetOwner_RenderThread() const override;


	///////////////////////////////
	// ~IDisplayClusterViewportProxy
	///////////////////////////////

	bool ImplResolveResources_RenderThread(FRHICommandListImmediate& RHICmdList, FDisplayClusterViewportProxy const* SourceProxy, const EDisplayClusterViewportResourceType InputResourceType, const EDisplayClusterViewportResourceType OutputResourceType) const;

	void ImplViewportRemap_RenderThread(FRHICommandListImmediate& RHICmdList) const;

	inline bool FindContext_RenderThread(const int32 ViewIndex, uint32* OutContextNum)
	{
		check(IsInRenderingThread());

		for (int32 ContextNum = 0; ContextNum < Contexts.Num(); ContextNum++)
		{
			if (ViewIndex == Contexts[ContextNum].StereoViewIndex)
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

	TSharedPtr<IDisplayClusterRender_MeshComponent, ESPMode::ThreadSafe> RemapMesh;

	// Projection policy instance that serves this viewport
	TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> ProjectionPolicy;

	// Viewport contexts (left/center/right eyes)
	TArray<FDisplayClusterViewport_Context> Contexts;

	// View family render to this resources
	TArray<FDisplayClusterViewportRenderTargetResource*> RenderTargets;

	// Projection policy output resources
	TArray<FDisplayClusterViewportTextureResource*> OutputFrameTargetableResources;
	TArray<FDisplayClusterViewportTextureResource*> AdditionalFrameTargetableResources;

#if WITH_EDITOR
	FTextureRHIRef OutputPreviewTargetableResource;
#endif

	// unique viewport resources
	TArray<FDisplayClusterViewportTextureResource*> InputShaderResources;
	TArray<FDisplayClusterViewportTextureResource*> AdditionalTargetableResources;
	TArray<FDisplayClusterViewportTextureResource*> MipsShaderResources;

	const FDisplayClusterViewportManagerProxy& Owner;
	IDisplayClusterShaders& ShadersAPI;
};

