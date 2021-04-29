// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Render/Viewport/IDisplayClusterViewportProxy.h"

class DISPLAYCLUSTER_API IDisplayClusterViewportManagerProxy
{
public:
	virtual ~IDisplayClusterViewportManagerProxy() = 0
	{ }

public:
	/** Transfer view results accross GPUs. This is done only once all views have been rendered for RenderFrame
	* [Rendering thread func]
	*/
	//virtual void DoCrossGPUTransfers_RenderThread(FViewport* InViewport, FRHICommandListImmediate& RHICmdList) const = 0;

	/** Resolve render targets to unique viewport inputshader contexts resources
	* Apply postprocess, generate mips, etc from settings in FDisplayClusterViewporDeferredUpdateSettings
	* [Rendering thread func]
	*/
	//virtual void UpdateDeferredResources_RenderThread(FRHICommandListImmediate& RHICmdList) const = 0;

	/** Apply Warp&Blend
	* [Rendering thread func]
	*/
	//virtual void UpdateFrameResources_RenderThread(FRHICommandListImmediate& RHICmdList, bool bWarpBlendEnabled) const = 0;

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	/**
	* Find viewport render thread proxy object by name
	* [Rendering thread func]
	*
	* @param ViewportId - Viewport name
	*
	* @return - viewport proxy object ref
	*/
	virtual IDisplayClusterViewportProxy* FindViewport_RenderThread(const FString& InViewportId) const = 0;

	/**
	* Find viewport render thread proxy object and context number by stereoscopic pass index
	* [Rendering thread func]
	*
	* @param StereoPassType - stereoscopic pass index
	* @param OutContextNum - context number
	*
	* @return - viewport render thread proxy object ref
	*/
	virtual IDisplayClusterViewportProxy* FindViewport_RenderThread(const enum EStereoscopicPass StereoPassType, uint32* OutContextNum = nullptr) const = 0;

	/**
	* Return all exist viewports render thread proxy objects
	* [Rendering thread func]
	*
	* @return - arrays with viewport render thread proxy objects refs
	*/
	virtual const TArrayView<IDisplayClusterViewportProxy*> GetViewports_RenderThread() const = 0;

	/**
	* Return render frame targets for current frame
	* [Rendering thread func]
	*
	* @param OutRenderFrameTargets - frame RTTs (left, right)
	* @param OutTargetOffsets - frames offset on backbuffer
	* @param OutAdditionalFrameResources - (optional) array with additional render targetable resources (requested externally FDisplayClusterRenderFrameSettings::bShouldUseAdditionalTargetableFrameResource)
	*
	* @return - true if success
	*/
	virtual bool GetFrameTargets_RenderThread(TArray<FRHITexture2D*>& OutFrameResources, TArray<FIntPoint>& OutTargetOffsets, TArray<FRHITexture2D*>* OutAdditionalFrameResources=nullptr) const = 0;

	/**
	* Resolve to backbuffer
	* [Rendering thread func]
	*
	* @param InContextNum - renderframe source context num
	* @param DestArrayIndex - dest array index on backbuffer
	* @param WindowSize - dest backbuffer window size
	*
	* @return - true if success
	*/
	virtual bool ResolveFrameTargetToBackBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, const uint32 InContextNum, const int DestArrayIndex, FRHITexture2D* DstBackBuffer, FVector2D WindowSize) const = 0;
};

