// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"

class FDisplayClusterRenderTargetResource;
class FDisplayClusterTextureResource;
struct FDisplayClusterViewportResourceSettings;
class FViewport;


class FDisplayClusterRenderTargetResourcesPool
{
public:
	FDisplayClusterRenderTargetResourcesPool();
	~FDisplayClusterRenderTargetResourcesPool();

public:
	bool BeginReallocateRenderTargetResources(FViewport* InViewport);
	FDisplayClusterRenderTargetResource* AllocateRenderTargetResource(const FIntPoint& InSize, EPixelFormat CustomPixelFormat);
	void FinishReallocateRenderTargetResources();

public:
	bool BeginReallocateTextureResources(FViewport* InViewport);
	FDisplayClusterTextureResource* AllocateTextureResource(const FIntPoint& InSize, bool bIsRenderTargetable, EPixelFormat CustomPixelFormat, int NumMips = 1);
	void FinishReallocateTextureResources();

protected:
	void ReleaseRenderTargetResources(TArray<FDisplayClusterRenderTargetResource*>& InOutResources);
	void ReleaseTextureResources(TArray<FDisplayClusterTextureResource*>& InOutResources);

	bool IsTextureSizeValid(const FIntPoint& InSize) const;

private:
	// Current render target settings (initialize from BeginReallocateRenderTargetResources)
	FDisplayClusterViewportResourceSettings* pRenderTargetResourceSettings = nullptr;
	FDisplayClusterViewportResourceSettings* pTextureResourceSettings = nullptr;

	TArray<FDisplayClusterRenderTargetResource*> RenderTargetResources;
	TArray<FDisplayClusterRenderTargetResource*> UnusedRenderTargetResources;
	TArray<FDisplayClusterRenderTargetResource*> CreatedRenderTargetResources;

	TArray<FDisplayClusterTextureResource*> TextureResources;
	TArray<FDisplayClusterTextureResource*> UnusedTextureResources;
	TArray<FDisplayClusterTextureResource*> CreatedTextureResources;
};
